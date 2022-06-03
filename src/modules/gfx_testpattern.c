#include <types.h>
#include <matrix.h>
#include <timers.h>
#include <random.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int w;
static int h;
static int mode;
static int tmode;
static int tlimit;

static const int fps = 60;

int init(int moduleid, char* argstr)
{
  w = matrix_getx();
  h = matrix_gety();
  return 0;
}

void reset(int moduleid)
{
  matrix_clear();
}

int draw(int moduleid, int argc, char* argv[])
{
  oscore_time now = udate();
  for (int y = 0; y < h; ++y)
  {
    for (int x = 0; x < w; ++x)
    {
      switch(mode)
      {
        case 0:
          matrix_set(x, y, RGB(xorshf96()%(tmode + 1), xorshf96()%(tmode + 1), xorshf96()%(tmode + 1)));
          tlimit = 256;
          break;
        case 1:
          matrix_set(x, y, RGB(255, 255, 255));
          tlimit = 256;
          break;
        case 2:
          matrix_set(x, y, RGB(x == tmode ? 255 : 0, x == tmode ? 255 : 0, x == tmode ? 255 : 0));
          tlimit = 256;
          break;
        case 3:
          matrix_set(x, y, RGB(y == tmode ? 255 : 0, y == tmode ? 255 : 0, y == tmode ? 255 : 0));
          tlimit = 256;
          break;
        case 4:
          matrix_set(x, y, RGB(x + y == tmode ? 255 : 0, x + y == tmode ? 255 : 0, x + y == tmode ? 255 : 0));
          tlimit = 512;
          break;
        default:
          mode = 0;
      }
    }
  }
  tmode++;
  if(tmode >= tlimit)
  {
    tmode = 0;
    mode++;
  }
  matrix_render();

  oscore_time nexttick = now + T_SECOND / fps;
  timer_add(nexttick, moduleid, 0, NULL);

  return 0;
}

void deinit(int moduleid)
{
}

