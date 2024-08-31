// PIXELFLUT: CtrlTcp output
// This allows using SLED as a CtrlTcp output.
// Should a CtrlTcp connection connect,
//  the module will start.
// Should all CtrlTcp connections disconnect,
//  the module will end.

#ifdef __linux__
#define _GNU_SOURCE
#endif

#ifdef __APPLE__
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

#include <sys/types.h>
#include <sys/socket.h>

// Has further effects in ctrl_thread_func
#ifdef __linux__
#define CTRL_USE_EPOLL
#endif

#ifdef CTRL_USE_EPOLL
#include <sys/epoll.h>
#else
#include <sys/select.h>
#endif

#include <netinet/in.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <oscore.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "timers.h"
#include "matrix.h"
#include "main.h"
#include "mod.h"
#include "asl.h"

static int ctrl_shutdown_fd_mt, ctrl_shutdown_fd_ot;
static int ctrl_moduleno;
static unsigned int ctrl_clientcount;
static int ctrl_mx, ctrl_my;
static oscore_task ctrl_task;

#define CTRL_PORT 7533
// The maximum, including 0, size of a line.
#define CTRL_LINESIZE 0x2000

typedef struct {
	int socket;
	size_t linelen;
	char line[CTRL_LINESIZE];
} ctrl_buffer_t;

typedef struct {
	int socket; // The socket
#ifdef CTRL_USE_EPOLL
	void * prev; // The previous client
#endif
	void * next; // The next client
	ctrl_buffer_t * buffer; // The current line buffer.
} ctrl_client_t;

#ifdef __APPLE__
static inline void * memrchr(const void * mem, int c, size_t n) {
    if (n == 0)
        return 0;

    unsigned char * end = (unsigned char *) mem + n;
    do {
        if (*(--end) == (unsigned char)c)
            return (void *) end;
    } while (--n != 0);

    return 0;
}
#endif

static inline void net_send(ctrl_buffer_t * client, const char * str, size_t len) {
	send(client->socket, str, len, MSG_NOSIGNAL);
}

static inline void net_sendstr(ctrl_buffer_t * client, const char * str) {
	send(client->socket, str, strlen(str), MSG_NOSIGNAL);
}

static void ctrl_sh_skipws(const char ** line) {
	while (1) {
		char c = *((*line)++);
		if ((c > ' ') || (c == 10)) {
			// 10 can't be consumed here, otherwise newline behavior will fail (10 will be consumed by ctrl_sh_word)
			(*line)--;
			return;
		} else if (c == 0) {
			// 0 can happen on shutdown.
			return;
		}
	}
}

// A simple state machine that can parse stuff like:
// "We are all equals here, we fight for dominion tonight..."
// and "Apostrophes, such as ', are useful, but can annoy "'certain parsers that alias " and \'.'
static char * ctrl_sh_word(int * hitnl, const char ** line) {
	char * str = NULL;
	int escape = 0;
	char quotes = 0;
	int fixempty = 0;
	while (1) {
		char c = *((*line)++);
		// Again, can happen on shutdown.
		if (c == 0)
			return str;
		if ((c > ' ') || escape || quotes) {
			fixempty = 1;
			if (!escape) {
				if (c == '\\') {
					escape = 1;
					continue;
				}
				if (!quotes) {
					if (c == '\"') {
						quotes = '\"';
						continue;
					}
					if (c == '\'') {
						quotes = '\'';
						continue;
					}
				} else if (c == quotes) {
					quotes = 0;
					continue;
				}
			} else {
				escape = 0;
			}
			str = asl_growstr(str, c);
			if (!str)
				return NULL;
		} else {
			if (c == 10)
				*hitnl = 1;
			if (fixempty)
				if (!str)
					return strdup("");
			return str;
		}
	}
}

// transfers 'ownership' of args contents to this.
static void ctrl_sh_execute(char * mid, asl_av_t * args, ctrl_buffer_t * client) {
	int routing_rov = 0;
	char str[128];
	if (!strcmp(mid, "/then")) {
		// Oh, this'll be *hilarious...*
		free(mid);
		mid = asl_pnabav(args);
		routing_rov = 1;
		if (!mid) {
			// argc == 0, so argv == null (unless a NULL got into the args array somehow)
			assert(args->argv);
			return;
		}
	}
	if (!strcmp(mid, "/now")) {
		free(mid);
		int len = snprintf(str, 128, "now running: %s\n", mod_get(current_modid())->name);
		if (len > 0 && len < 128)
			net_send(client, str, len);
		return;
	}
	// "/then /blank" is a useful tool
	if (mid[0] == '/') {
		// "/blank" for example results in "control_tcp /blank"
		asl_pgrowav(args, mid);
		mid = strdup("control_tcp");
		assert(mid);
	}

	int len = snprintf(str, 128, "module: '%s', args:", mid);
	if (len > 0 && len < 128)
		net_send(client, str, len);
	len = 0;
	for (int i = 0; i < args->argc; i++)
	{
    if (i > 0) len += snprintf(str + len, 128 - len, ",");
    len += snprintf(str + len, 128 - len, " '%s'", args->argv[i]);
  }
  len += snprintf(str + len, 128 - len, "\n");
	if (len > 0 && len < 128)
		net_send(client, str, len);

	// It is because of this code that thread-safety has to be ensured with proper deinit/init barriers...
	module * modref = mod_find(mid);
	free(mid);
	if (modref) {
		int i = mod_getid(modref);
		if (routing_rov) {
			main_force_random(i, args->argc, args->argv);
		} else {
			timer_add(0, i, args->argc, args->argv);
			timers_wait_until_break();
		}
		// args memory passed into main_force_random or timer_add, not our concern anymore
		return;
	}
	asl_clearav(args);
}

// Executes the line given.
static int ctrl_buffer_executeline(const char * line, ctrl_buffer_t * client) {
  ctrl_sh_skipws(&line);
  int hitnl = 0;
  char * module = ctrl_sh_word(&hitnl, &line);
  if (!module)
    return 0;
  asl_av_t args = {0, NULL};
  while (!hitnl) {
    ctrl_sh_skipws(&line);
    char * arg = ctrl_sh_word(&hitnl, &line);
    if (!arg)
      break;
    asl_growav(&args, arg);
  }
  // Ready.
  ctrl_sh_execute(module, &args, client);
  oscore_task_yield();

	return 0;
}

static void ctrl_buffer_update(void * buf) {
	ctrl_buffer_t * buffer = buf;
	char * line = buffer->line;
	while (1) {
		char * ch = strchr(line, '\n');
		if (!ch)
			break;

		*ch = 0;
		ctrl_buffer_executeline(line, buffer);
		line = ch + 1;
	}
	free(buffer);
}

// Returns true to remove the client.
static int ctrl_client_update(ctrl_client_t * client) {
	ctrl_buffer_t * cbuf = client->buffer;
	ssize_t addlen = read(client->socket, cbuf->line + cbuf->linelen, CTRL_LINESIZE - (1 + cbuf->linelen));
	if (addlen < 0) {
		// All errors except these are assumed to mean the connection died.
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			return 0;
		return 1;
	} else if (addlen == 0) {
		// End Of File -> socket closed
		return 1;
	} else {
		// Zero-terminate the resulting string.
		cbuf->linelen += addlen;
		cbuf->line[cbuf->linelen] = 0;
	}
	char * chp = memrchr(cbuf->line, '\n', cbuf->linelen);
	if (chp) {
		// Create new buffer, put the remainder in it
		ctrl_buffer_t * nb = malloc(sizeof(ctrl_buffer_t));
		if (!nb)
			return 1;
		nb->linelen = (cbuf->line + cbuf->linelen) - (chp + 1);
		memcpy(nb->line, chp + 1, nb->linelen + 1);
		nb->socket = client->socket;
		// The new remainder buffer belongs to us...
		client->buffer = nb;
		// The old line buffer is sent to update
		chp[1] = 0;
		ctrl_buffer_update(cbuf);
	}
	return 0;
}

// Allows or closes the socket. By being called, this transfers responsibility for the socket to the list.
// If the client isn't created successfully, then the socket has to be closed.
static ctrl_client_t * ctrl_client_new(ctrl_client_t ** list, int sock) {
	ctrl_client_t * c = malloc(sizeof(ctrl_client_t));
	if (!c) {
		close(sock);
		return NULL;
	}
	c->socket = sock;
#ifdef CTRL_USE_EPOLL
	c->prev = NULL;
#endif
	c->next = NULL;
	c->buffer = malloc(sizeof(ctrl_buffer_t));
	if (!c->buffer) {
		free(c);
		close(sock);
		return NULL;
	}
	c->buffer->socket = sock;
	c->buffer->line[0] = 0;
	c->buffer->linelen = 0;
	if (*list) {
		c->next = *list;
#ifdef CTRL_USE_EPOLL
		((ctrl_client_t *)(c->next))->prev = c;
#endif
	}
	*list = c;
	ctrl_clientcount++;
	return c;
}

// Makes an FD nonblocking
static void ctrl_nbs(int sock) {
	int flags = fcntl(sock, F_GETFL, 0);
	flags |= O_NONBLOCK;
	fcntl(sock, F_SETFL, flags);
}

// The main server thread. Manages sockets. Tries not to explode.
static void * ctrl_thread_func(void * n) {
	ctrl_client_t * list = 0;
	int server;
	struct sockaddr_in sa_bpwr;
	server = socket(AF_INET, SOCK_STREAM, 0); // It's either 0 or 6...
	if (server < 0) {
		fputs("error creating socket! -- CtrlTcp\n", stderr);
		return NULL;
	}
	// more magic
	memset(&sa_bpwr, 0, sizeof(sa_bpwr));
	sa_bpwr.sin_family = AF_INET;
	sa_bpwr.sin_port = htons(CTRL_PORT);
	sa_bpwr.sin_addr.s_addr = INADDR_ANY;

	// prepare server...
	if (bind(server, (struct sockaddr *) &sa_bpwr, sizeof(sa_bpwr))) {
		fputs("error binding socket! -- CtrlTcp\n", stderr);
		close(server);
		return NULL;
	}
	if (listen(server, 32)) {
		fputs("error finalizing socket! -- CtrlTcp\n", stderr);
		close(server);
		return NULL;
	}
	ctrl_nbs(server);
	ctrl_nbs(ctrl_shutdown_fd_ot);
	// --
#ifdef CTRL_USE_EPOLL
	fputs("we are using epoll -- CtrlTcp\n", stderr);
#define CTRL_EPOLL_EVS 512
	// epoll
	int epoll_obj = epoll_create(128);
	if (epoll_obj == -1) {
		fputs("could not access epoll! -- CtrlTcp\n", stderr);
		close(server);
		return NULL;
	}
	struct epoll_event epoll_work;
	struct epoll_event epoll_events[CTRL_EPOLL_EVS];

	memset(&epoll_work, 0, sizeof(epoll_work));
	epoll_work.events = EPOLLIN;

	epoll_work.data.fd = ctrl_shutdown_fd_ot;
	epoll_ctl(epoll_obj, EPOLL_CTL_ADD, ctrl_shutdown_fd_ot, &epoll_work);

	epoll_work.data.fd = server;
	epoll_ctl(epoll_obj, EPOLL_CTL_ADD, server, &epoll_work);

	int running = 1;

	while (running) {
		// epoll is somewhat more event-based. This means trouble.
		int eventcount = epoll_wait(epoll_obj, epoll_events, CTRL_EPOLL_EVS, -1);
		if (eventcount <= 0)
			fputs("Not more bugs!\n", stderr);
		for (int i = 0; i < eventcount; i++) {
			// REGARDING USERDATA BEING A UNION!
			// There is an implicit assumption here that a valid client pointer cannot equal either core FD value.
			// If this assumption turns out to be false... yeah, can't help you there.
			if (epoll_events[i].data.fd == server) {
				int accepted = accept(server, NULL, NULL);
				if (accepted >= 0) {
					ctrl_nbs(accepted);
					ctrl_client_t * client = ctrl_client_new(&list, accepted);
					if (client) {
						epoll_work.data.ptr = client;
						epoll_ctl(epoll_obj, EPOLL_CTL_ADD, accepted, &epoll_work);
					}
				}
			} else if (epoll_events[i].data.fd == ctrl_shutdown_fd_ot) {
				running = 0;
				break;
			} else {
				ctrl_client_t * client = epoll_events[i].data.ptr;
				if (ctrl_client_update(client)) {
					// Don't pass NULL, older kernels are ticklish.
					epoll_ctl(epoll_obj, EPOLL_CTL_DEL, client->socket, &epoll_work);
					close(client->socket);
					free(client->buffer);
					ctrl_client_t * pr = (ctrl_client_t *) client->prev;
					ctrl_client_t * nx = (ctrl_client_t *) client->next;
					free(client);
					if (pr)
						pr->next = nx;
					if (nx)
						nx->prev = pr;
					ctrl_clientcount--;
				}
			}
		}
	}
#else
	// select
	fd_set rset, active_fds;
	FD_ZERO(&active_fds);
	FD_SET(ctrl_shutdown_fd_ot, &active_fds);
	FD_SET(server, &active_fds);
	while (1) {
		// select is simple to use
		rset = active_fds;
		select(FD_SETSIZE, &rset, NULL, NULL, NULL);

		if(FD_ISSET(ctrl_shutdown_fd_ot, &rset)) {
			break;
		}

		// Accept?
		if(FD_ISSET(server, &rset)) {
			int accepted = accept(server, NULL, NULL);
			if (accepted >= 0) {
				ctrl_nbs(accepted);
				if (ctrl_client_new(&list, accepted))
					FD_SET(accepted, &active_fds);
			}
		}

		// Go through all clients, holding the BGMI lock so that the status of "are we in control of the matrix" cannot change.
		ctrl_client_t ** backptr = &list;
		while (*backptr) {
			if (FD_ISSET((*backptr)->socket, &rset)) {
				if(ctrl_client_update(*backptr)) {
					// NOTE! This code doesn't handle ->prev because we don't use it!
					close((*backptr)->socket);
					FD_CLR((*backptr)->socket, &active_fds);
					free((*backptr)->buffer);
					void *on = (*backptr)->next;
					free(*backptr);
					*backptr = on;
					ctrl_clientcount--;
				}
				else {
					backptr = (ctrl_client_t**) &((*backptr)->next);
				}
			}
			else {
				backptr = (ctrl_client_t**) &((*backptr)->next);
			}
		}
	}
#endif
	// Close & Deallocate
#ifdef CTRL_USE_EPOLL
	// epoll cleanup
	close(epoll_obj);
#endif
	while (list) {
		ctrl_client_t * nxt = (ctrl_client_t*) list->next;
		free(list->buffer);
		close(list->socket);
		free(list);
		list = nxt;
	}
	close(server);
	return NULL;
}

int init(int moduleno, char* argstr) {
	// Shutdown signalling pipe
	int tmp[2];
	if (pipe(tmp) != 0)
		return 1;

	ctrl_mx = matrix_getx();
	ctrl_my = matrix_gety();
	// For whatever reason, the *receiver* is FD 0.
	ctrl_shutdown_fd_mt = tmp[1];
	ctrl_shutdown_fd_ot = tmp[0];
	ctrl_moduleno = moduleno;
	ctrl_task = oscore_task_create("bgm_control_tcp", ctrl_thread_func, NULL);

	return 0;
}

int draw(int _modno, int argc, char ** argv) {
	if (argc == 1) {
		// Utilities that shouldn't be part of rotation:
		if (!strcmp(argv[0], "/blank")) {
			// Blank (...forever)
			matrix_clear();
			matrix_render();
			char ** x = malloc(sizeof(char *));
			assert(x);
			*x = strdup("/blank");
			assert(*x);
			timer_add(udate() + T_SECOND, ctrl_moduleno, 1, x);
			return 0;
		} else if (!strcmp(argv[0], "/error42")) {
			// Trigger error 42 as a quick escape.
			return 42;
		}
	}
	return 1;
}

void reset(int _modno) {
}

void deinit(int _modno) {
	char blah = 0;
	if (write(ctrl_shutdown_fd_mt, &blah, 1) != -1)
		oscore_task_join(ctrl_task);
	close(ctrl_shutdown_fd_mt);
	close(ctrl_shutdown_fd_ot);
}
