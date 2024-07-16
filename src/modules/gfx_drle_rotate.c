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
#define MULTISAMPLE 2

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
static RGB *msample;

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
   msample = malloc(screenW * screenH * MULTISAMPLE * MULTISAMPLE * sizeof(RGB));
   return 0;
}

void reset(int moduleid)
{
  t = frand(6.28);
}

Vec3d itom(int ix, int iy)
{
  Vec3d m;
  m.x = ((double) (ix + (is - iw) / 2) / is - 0.5) * 0.98;
  m.y = ((double) (iy + (is - ih) / 2) / is - 0.5) * 0.98;
  m.z = 0;
  
  return m;
}

void mtov(Vec3d m, int *x, int *y)
{
  *x = (m.x * (1.0 + m.z * 0.7) + 0.5) * screenW * MULTISAMPLE + frand(4.0);
  *y = (m.y * (1.0 + m.z * 0.7) + 0.5) * screenH * MULTISAMPLE + frand(4.0);
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
  
  matrix_clear();
  memset(msample, 0, screenW * screenH * MULTISAMPLE * MULTISAMPLE * sizeof(RGB));
  
  for (int iy = 0; iy < ih; ++iy)
  {
    for (int ix = 0; ix < iw; ++ix)
    {
      if (!idata[iy * iw + ix]) continue;
      int x, y;
      Vec3d m = itom(ix, iy);
      m = rotateY(m, t);
      double inten = (m.z + 0.75);
      inten = MIN(inten, 1.0);
      mtov(m, &x, &y);
      msample[y * screenW * MULTISAMPLE + x].red = MAX(msample[y * screenW * MULTISAMPLE + x].red, inten * (m.y * 1.33 + 0.5) * 255);
      msample[y * screenW * MULTISAMPLE + x].green = MAX(msample[y * screenW * MULTISAMPLE + x].green, inten * (0.5 - m.y * 1.33) * 255);
      msample[y * screenW * MULTISAMPLE + x].blue = MAX(msample[y * screenW * MULTISAMPLE + x].blue, inten * (0.75) * 255);
    }
  }
  
  double r, g, b;  
  for (int y = 0; y < screenH; ++y)
  {
    for (int x = 0; x < screenW; ++x)
    {
      r = 0;
      g = 0;
      b = 0;
      for (int iy = 0; iy < MULTISAMPLE; iy++)
      {
        for (int ix = 0; ix < MULTISAMPLE; ix++)
        {
          r += msample[(y * MULTISAMPLE + iy) * screenW * MULTISAMPLE + x * MULTISAMPLE + ix].red;
          g += msample[(y * MULTISAMPLE + iy) * screenW * MULTISAMPLE + x * MULTISAMPLE + ix].green;
          b += msample[(y * MULTISAMPLE + iy) * screenW * MULTISAMPLE + x * MULTISAMPLE + ix].blue;
        }
      }
      double f = 1.0 / (MULTISAMPLE * MULTISAMPLE);
      RGB col = RGB(MIN(r * f, 255), MIN(g * f, 255), MIN(b * f, 255));
      matrix_set(x, y, col);
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
