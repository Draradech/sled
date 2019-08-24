// UDP output.
// Follows the protocol of Kai's LED matrix.
// Quite a big mess. It works, however.
//
// Copyright (c) 2019, Adrian "vifino" Pistol <vifino@tty.sh>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <types.h>
#include <timers.h>
#include <stdlib.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <util.h>
#include <assert.h>

#define BUFLEN 512
#define X_SIZE 41
#define Y_SIZE 24
#define PACKSIZE (4 * X_SIZE * 3 + 1)

static int sock = -1;
struct sockaddr_in sio;
static int port;
static int limit;

// Message will be:
// 6 packets of <line> <R,G,B bytes..> for 4 lines
static byte message[6][PACKSIZE];

int init (int moduleno, char* argstr) {
	// Partially initialize the socket.
	if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		perror("out_udp: Failed to initialize socket");
		return 2;
	}
	memset((char *) &sio, 0, sizeof(struct sockaddr_in));
	sio.sin_family = AF_INET;

	// Parse string. This sucks.
	if (argstr == NULL) {
		eprintf("UDP argstring not set. Example: -o udp:192.168.69.42:1234\n");
		return 3;
	}
	char* data = argstr;
	char* ip = data;
	char* portstr;
	if (strsep(&data, ":") == NULL) {
		eprintf("UDP argstring doesn't contain a port seperator. Example: -o udp:192.168.69.42:1234,20\n");
		return 3;
	}
	if ((portstr = strsep(&data, ",")) == NULL) { // can't find anything after : before ,
		eprintf("UDP argstring doesn't contain port. Example: -o udp:192.168.69.42:1234,20\n");
		return 3;
	}
	if (inet_aton(ip, &sio.sin_addr) == 0) {
		eprintf("UDP argstring doesn't contain a valid IP. Example: -o udp:192.168.69.42:1234,20\n");
		return 4;
	}
	port = util_parse_int(portstr);
	if (port == 0) {
		eprintf("UDP argstring doesn't contain a valid port. Example: -o udp:192.168.69.42:1234,20\n");
		return 4;
	}
	sio.sin_port = htons(port);

	// parse tiletype
	limit = util_parse_int(data);
	if (limit <= 0 || limit > 255) {
		eprintf("UDP argstring doesn't contain a valid brightness limit. Example: -o upd:192.168.69.42:1234,20\n");
		return 4;
	}

	// Free stuff.
	free(argstr);
	
	for(int pack = 0; pack < 6; ++pack) message[pack][0] = pack * 4;

	return 0;
}

int getx(int _modno) {
	return X_SIZE;
}
int gety(int _modno) {
	return Y_SIZE;
}

int ppack(int y) {
	assert(y >= 0);
	assert(y < Y_SIZE);

  return y / 4;
}

int ppos(int x, int y) {
	assert(x >= 0);
	assert(y >= 0);
	assert(x < X_SIZE);
	assert(y < Y_SIZE);
	
	y = y - ppack(y) * 4;

	return (y * X_SIZE + x) * 3 + 1;
}

int set(int _modno, int x, int y, RGB color) {
	assert(x >= 0);
	assert(y >= 0);
	assert(x < X_SIZE);
	assert(y < Y_SIZE);

  int pack = ppack(y);
	int pos = ppos(x, y);
	message[pack][pos + 0] = color.blue * (limit / 2) / 255;
	message[pack][pos + 1] = color.green * limit / 255;
	message[pack][pos + 2] = color.red * limit / 255;
	return 0;
}

RGB get(int _modno, int x, int y) {
	assert(x >= 0);
	assert(y >= 0);
	assert(x < X_SIZE);
	assert(y < Y_SIZE);

  int pack = ppack(y);
	int pos = ppos(x, y);
	return RGB(message[pack][pos + 2] * 255 / limit, message[pack][pos + 1] * 255 / limit, message[pack][pos + 0] *255 / (limit / 2));
}

int clear(int _modno) {
	for(int pack = 0; pack < 6; ++pack) memset(&message[pack][1], '\0', PACKSIZE - 1);
	return 0;
};

int render(void) {
	// send udp packets.
	for(int pack = 0; pack < 6; ++pack)
	{
	  if (sendto(sock, message[pack], PACKSIZE, 0, (struct sockaddr*) &sio, sizeof(sio)) == -1)
	  {
		  perror("out_udp: Failed to send UDP packet");
	  	return 5;
  	}
  	usleep(4000);
	}

	return 0;
}

oscore_time wait_until(int _modno, oscore_time desired_usec) {
	// Hey, we can just delegate work to someone else. Yay!
	return timers_wait_until_core(desired_usec);
}

void wait_until_break(int _modno) {
	return timers_wait_until_break_core();
}

void deinit(int _modno) {
	close(sock);
}
