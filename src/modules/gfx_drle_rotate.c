#include <types.h>
#include <matrix.h>
#include <timers.h>
#include <random.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define FPS 60
#define FRAMETIME (T_SECOND / FPS)
#define FRAMES (TIME_LONG * FPS)
#define LIMIT(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))

typedef struct
{
  double x;
  double y;
  double z;
} Vec3d;

static int screenW;
static int screenH;
static int frame;
static double t;
static uint16_t iw, ih, is;
static uint8_t *idata;

void read_image(void)
{
  FILE *fin = fopen("chaosknoten.drle", "rb");
  fread(&iw, 2, 1, fin);
  fread(&ih, 2, 1, fin);
  is = MAX(iw, ih);
  
  uint8_t col = 0;
  uint16_t len = 0;
  idata = malloc(iw * ih);
  uint8_t *seek = idata;

  int ok = fread(&len, 2, 1, fin);
  while (ok)
  {
    memset(seek, col, len);
    seek += len;
    col = !col;
    ok = fread(&len, 2, 1, fin);
  }
}

static double frand(double max)
{
  return max * rand() / RAND_MAX;
}

static void fadeby(double f)
{
  for (int y = 0; y < screenH; ++y)
  {
    for (int x = 0; x < screenW; ++x)
    {
      RGB col = matrix_get(x, y);
      col.red *= f;
      col.green *= f;
      col.blue *= f;
      matrix_set(x, y, col);
    }
  }
}

int init(int moduleid, char* argstr)
{
   screenW = matrix_getx();
   screenH = matrix_gety();
   read_image();
   return 0;
}

void reset(int moduleid)
{
  t = frand(1e6);
}

Vec3d itom(int ix, int iy)
{
  Vec3d m;
  m.x = (double) (ix + (is - iw) / 2) / is - 0.5;
  m.y = (double) (iy + (is - ih) / 2) / is - 0.5;
  m.z = 0;
  
  return m;
}

void mtov(Vec3d m, int *x, int *y)
{
  *x = (m.x * (1.0 + m.z * 0.7) + 0.5) * screenW;
  *y = (m.y * (1.0 + m.z * 0.7) + 0.5) * screenH;
}

Vec3d rotateY(Vec3d m, double a)
{
  Vec3d o;
  o.x = cos(a) * m.x + sin(a) * m.z;
  o.y = m.y;
  o.z = -sin(a) * m.x + cos(a) * m.z;
  
  return o;
}

int draw(int moduleid, int argc, char* argv[])
{
  oscore_time now = udate();
  
  /*
  uint16_t is = MAX(iw, ih);
  for (int y = 0; y < screenH; ++y)
  {
    for (int x = 0; x < screenW; ++x)
    {
      double cx = ((double)x - screenW / 2) / (screenW / 2);
      double cy = ((double)y - screenH / 2) / (screenH / 2);
      uint16_t ix = ((cx + 1) * is - (is - iw)) / 2;
      uint16_t iy = ((cy + 1) * is - (is - ih)) / 2;
      uint8_t data = (ix >= 0 && ix < iw && iy >= 0 & iy < ih) ? idata[iy * iw + ix] : 0;
      data = data ? 255 : 0;
      matrix_set(x, y, RGB(data, data, data));
    }
  }
  */
  
  matrix_clear();
  
  int is = MAX(iw, ih);
  for (int iy = 0; iy < ih; ++iy)
  {
    for (int ix = 0; ix < iw; ++ix)
    {
      if (!idata[iy * iw + ix]) continue;
      int x, y;
      Vec3d m = itom(ix, iy);
      m = rotateY(m, t);
      int col = (m.z + 0.75) * 255;
      col = LIMIT(col, 0, 255);
      mtov(m, &x, &y);
      matrix_set(x, y, RGB(col, col, col));
    }
  }
  
  t += 0.01;

  matrix_render();
  if (frame++ >= FRAMES)
  {
    frame = 0;
    return 1;
  }
  oscore_time nexttick = now + T_SECOND / FPS;
  timer_add(nexttick, moduleid, 0, NULL);
  return 0;
}

void deinit(int moduleid)
{
}
