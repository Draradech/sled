// Automatically typing terminal.
//
// Copyright (c) 2019/2020, Sebastian "basxto" Riedel <git@basxto.de>
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

#include <matrix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timers.h>
#include <types.h>

#include "foxel35.xbm"
#include "xbm_font_loader.c"

#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>

const int font_width = 4;
const int font_height = 6;

#define FRAMETIME (T_SECOND/8)
#define FRAMES (TIME_SHORT)

struct font_char {
    unsigned char c;
    RGB fg;
    RGB bg;
    int flags;
};

static oscore_time nexttick;
static int moduleno;
static int max_row;
static int max_column;
struct font_char *buffer;
static int active_shell = 0;
char **type_buffer;
int type_pos;
int type_index;
int max_index;
const RGB fg_default = RGB(247, 127, 190);
const RGB bg_default = RGB(50, 50, 50);
RGB fg = fg_default;
RGB bg = bg_default;
int current_row = 0;
int current_column = 0;

// scroll buffer up by one line
static void scroll_up() {
    int i;
    for (i = 0; i < ((max_column * (max_row - 1))); ++i) {
        buffer[i].c = buffer[i + max_column].c;
        buffer[i].fg = buffer[i + max_column].fg;
        buffer[i].bg = buffer[i + max_column].bg;
    }
    for (; i < ((max_column * max_row)); ++i) {
        buffer[i].c = ' ';
        buffer[i].fg = fg;
        buffer[i].bg = bg;
    }
}

static int csi_type(char *str, int i) {
    while ((str[i] >= '0' && str[i] <= '?')) {
        i++;
    }
    return i;
}

static RGB sgr2rgb(int code) {
    RGB color = RGB(0, 0, 0);
    int shift = 0;
    // high intensity is 8-15
    if (code > 7 && code < 16) {
        shift = 85;
        code -= 8;
    }
    switch (code % 10) {
    case 0:
        color = RGB(shift, shift, shift);
        break;
    case 1:
        color = RGB(170 + shift, shift, shift);
        break;
    case 2:
        color = RGB(shift, 170 + shift, shift);
        break;
    case 3:
        color = RGB(170 + shift, 85 + shift, shift);
        break;
    case 4:
        color = RGB(shift, shift, 170 + shift);
        break;
    case 5:
        color = RGB(170 + shift, shift, 170 + shift);
        break;
    case 6:
        color = RGB(shift, 170 + shift, 170 + shift);
        break;
    case 7:
        color = RGB(170 + shift, 170 + shift, 170 + shift);
        break;
    }
    if (code >= 16 && code < 232) { /// color cubes
        int cubemap[] = {0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
        code -= 16;
        int r = code / 36;
        int g = (code % 36) / 6;
        int b = code % 6;
        color = RGB(cubemap[r], cubemap[g], cubemap[b]);
    } else if (code >= 232) { // black to white in 24 steps
        int gray = (code - 232) * (256 / 24);
        color = RGB(gray, gray, gray);
    }
    return color;
}

// read decimal number and set i to following character
static int parse_sgr_value(char *str, int i, int *code, int def) {
    int len = strlen(str);
    if(!(i < len && (str[i] >= '0' && str[i] <= '9'))){
        return def;
    }
    for (*code = 0; i < len && (str[i] >= '0' && str[i] <= '9'); ++i) {
        *code *= 10;
        *code += str[i] - '0';
    }
    return i;
}

// csi sgr
static int interpret_sgr(char *str, int i) {
    int code = 0;
    int len = strlen(str);

    while (i < len) {
        i = parse_sgr_value(str, i, &code, 0);
        if (str[i] == ';' || str[i] == 'm') {
            if (code == 38 || code == 48) {
                RGB color = RGB(0, 0, 0);
                int tmpcode = 0;
                int tmpi;
                tmpi = parse_sgr_value(str, i + 1, &tmpcode, 0);
                i = tmpi;
                if (tmpcode == 5) {
                    i = parse_sgr_value(str, i + 1, &tmpcode, 0);
                    color = sgr2rgb(tmpcode);
                    // printf("%d = #%02x%02x%02x\n", tmpcode, color.red,
                    // color.green, color.blue);
                } else if (tmpcode == 2) {
                    // colors are simply given as rgb codes
                    int r;
                    int g;
                    int b;
                    i = parse_sgr_value(str, i + 1, &r, 0);
                    i = parse_sgr_value(str, i + 1, &g, 0);
                    i = parse_sgr_value(str, i + 1, &b, 0);
                    color = RGB(r, g, b);
                }
                if (code < 40)
                    fg = color;
                else
                    bg = color;

            } else if ((code >= 30 && code <= 47) ||
                       (code >= 90 && code <= 107)) {
                // map regular foreground color to 0-7 and high intensity to
                // 8-15
                RGB color = sgr2rgb(code % 10 + (code >= 90 ? 8 : 0));
                if (code < 40)
                    fg = color;
                else
                    bg = color;
            } else
                switch (code) {
                case 0:
                    fg = fg_default;
                    bg = bg_default;
                    break;
                // case 7: //reverse video
                //	break;
                default:
                    printf("Unhandled escape code %d\n", code);
                    break;
                }
            code = 0;
        }
        if (str[i] == 'm') {
            break;
        }
        i++;
    }
    return i;
}

// If string ends with an unfinished escape sequence it
// it returns amount of read characters of that sequence
static int write_buffer(char *str, int *row, int *column) {
    int i;
    int j = 0;
    int j_len = 0;
    int end;
    int code = 0;
    int pos = (*column) + ((*row) * max_column);
    int len = strlen(str);
    // check whether it does fit
    /*while (pos + len > max_row * max_column) {
        scroll_up();
        (*row)--;
        pos = (*column) + ((*row) * max_column);
    }*/
    for (i = 0; i < len; ++i) {
        // look for char 27 + [
        if (str[i] == 0x1B) {
            if (i + 1 < len && str[i + 1] == '[') {
                end = csi_type(str, i + 2);
                switch(str[end]){
                case 'm':
                    interpret_sgr(str, i + 2);
                    break;
                case 'H': // cursor position
                    i = parse_sgr_value(str, i + 2, &code, 1);
                    current_row = code - 1;
                    // column handled by G
                case 'G': // cursor horizontal absolute
                    parse_sgr_value(str, i + 2, &code, 1);
                    current_column = code - 1;
                    break;
                case 'K':// erase line
                    parse_sgr_value(str, i + 2, &code, 0);
                    switch(code){
                    case 0:
                        j = current_column;
                        j_len = max_column;
                        break;
                    case 1:
                        j = 0;
                        j_len = current_column;
                        break;
                    case 2:
                        j = 0;
                        j_len = max_column;
                        break;
                    }
                    for(; j <= j_len; ++j){
                        buffer[j + ((*row) * max_column)].c = ' ';
                        buffer[j + ((*row) * max_column)].fg = fg;
                        buffer[j + ((*row) * max_column)].bg = bg;
                    }
                    break;
                default:
                    if(end == len){
                        //reached end of string, but escape code is incomplete
                        return end-i;
                    }
                    printf("Unhandled CSI type %c\n", str[end]);
                }
                i = end;
            }
        } else if (str[i] != '\n') {
            if (pos >= 0) {
                buffer[pos].c = str[i];
                buffer[pos].fg = fg;
                buffer[pos].bg = bg;
            }
            (*column)++;
            pos++;
        } else { //\n
            (*row)++;
            while(*row > max_row){
                (*row)--;
                scroll_up();
            }
            *column = 0;
            pos = ((pos / max_column) + 1) * max_column;
        }
    }
    // we reached end of string and everything went well
    return 0;
}

// run in own thread
// 6* is just a hack, since offset isn’t really working yet
static void *launch(void *type_buffer) {
    char *command = (char *)type_buffer;
    FILE *cout = popen(command, "r");
    //according to man console_codes ESC [ has a maximum of 16 parameters
    // and since 255; is the maximum int value, that makes 16*4
    char *tmpbuffer = malloc(max_column*6*sizeof(char));
    int offset = 0;
    unsigned char c;
    int escape_code = 0;
    while((c = fgetc(cout)) != (unsigned char)EOF){
        if(escape_code != 0){
            tmpbuffer[escape_code - 1] = c;
            // first char always allowed
            // after that numbers , ; ?
            if(escape_code == 1 || (c >= '0' && c <= '?')){
                escape_code++;
            }else{//exit escape code parsing
                tmpbuffer[escape_code] = 0;
                if(tmpbuffer[0] == '[' && tmpbuffer[escape_code-1] == 'm'){
                    interpret_sgr(tmpbuffer, 1);
                }
                escape_code = 0;
            }
                
        }else{
            if(c == 0x1B){
                escape_code = 1;
            }else{
                tmpbuffer[0] = c;
                tmpbuffer[1] = 0;
                offset = write_buffer(tmpbuffer, &current_row, &current_column);
            }
        }
    }
    fclose(cout);
    free(tmpbuffer);
    active_shell = 0;
    printf("thread finished: %s\n", command);
    return 0;
}

static void clear_buffer() {
    int i;
    for (i = 0; i < max_row * max_column; ++i) {
        buffer[i].c = ' ';
        buffer[i].fg = fg;
        buffer[i].bg = bg;
    }
}

int init(int modno, char *argstr) {
    moduleno = modno;

    char *from_int = malloc(10 * sizeof(char));
    max_row = matrix_gety() / 6;
    max_column = matrix_getx() / 4;
    setenv("TERM", "xterm", 1);
    snprintf(from_int, 10, "%d", max_row);
    setenv("LINES", from_int, 1);
    snprintf(from_int, 10, "%d", max_column);
    setenv("COLUMNS", from_int, 1);
    max_index = 1;

    // read script
    FILE *file;
    char ch;

    file = fopen("scripts/autoterminal.sh", "r");
    if (file) {
        // last line endns with eof and not new line
        for (ch = getc(file); ch != EOF; ch = getc(file))
            if (ch == '\n')
                max_index++;
        type_buffer = malloc(max_index * sizeof(char *));
        for (type_index = 0; type_index < max_index; type_index++) {
            type_buffer[type_index] = malloc(max_column * 3 * sizeof(char));
        }
        rewind(file);
        for (type_index = 0;
             type_index < max_index &&
             fgets(type_buffer[type_index], max_column * 3, file) != NULL;
             type_index++) {
            // skip comments
            if (type_buffer[type_index][0] == '#') {
                type_index--;
            }
        };
        fclose(file);
    }

    type_pos = 0;
    max_index = type_index - 1;
    type_index = 0;

    buffer = malloc(max_row * max_column * sizeof(struct font_char));
    clear_buffer();
    if (max_index >= 0) {
        write_buffer("$ ", &current_row, &current_column);
    }

    // 2 rows
    if (matrix_getx() < font_height * 2)
        return 1; // not enough X to be usable
    // 8 characters
    if (matrix_gety() < font_width * 8)
        return 1; // not enough Y to be usable
    return 0;
}

void reset(int _modno) {
    type_index = 0;
    type_pos = 0;
    current_row = 0;
    current_column = 0;
    fg = fg_default;
    bg = bg_default;
    clear_buffer();
    if (max_index >= 0) {
        write_buffer("$ ", &current_row, &current_column);
    }
    nexttick = udate();
}

int draw(int _modno, int argc, char *argv[]) {
    int x = 0;
    int y = 0;
    int row = 0;
    int column = 0;
    int pos = 0;
    char ch[1];
    pthread_t thread_id;
    if (active_shell == 0) {
        // we are through with all commands
        if(type_index > max_index)
            return 1;
        // this happens right after other thread finished
        if(type_pos == 0){//prepare next line or exit
           write_buffer("$ ", &current_row, &current_column);
        }
        if (type_pos < strlen(type_buffer[type_index])) {
            ch[0] = type_buffer[type_index][type_pos];
            write_buffer(ch, &current_row, &current_column);
            type_pos++;
        } else {
            current_row++;
            if (current_row > max_row) {
                current_row--;
                scroll_up();
            }
            active_shell = 1;
            type_index++;
            type_pos = 0;
            pthread_create(&thread_id, NULL, launch, type_buffer[type_index-1]);
        }
    }

    for (row = 0; row < max_row; ++row)
        for (column = 0; column < max_column; ++column) {
            for (y = 0; y < 6; ++y) {
                for (x = 0; x < 4; ++x) {
                    matrix_set((column * 4) + x, (row * 6) + y,
                               (load_char(foxel35_bits, buffer[pos].c, x, y,
                                          font_width, font_height) == 1
                                    ? buffer[pos].fg
                                    : buffer[pos].bg));
                }
            }
            pos++;
        }

    matrix_render();
    nexttick += (FRAMETIME);
    timer_add(nexttick, moduleno, 0, NULL);
    return 0;
}

void deinit(int _modno) {
    // This acts conditionally on rendered being non-NULL.
    //text_free(rendered);
    free(buffer);
    for (type_index = 0; type_index < max_index; type_index++) {
        free(type_buffer[type_index]);
    }
    free(type_buffer);
}
