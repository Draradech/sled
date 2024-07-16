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
static uint8_t cscheme;

char *fnames[] =
{
  "resources/cccffm.drle",
  "resources/chaosknoten.drle"
};

static int irand(int max_excl)
{
  return rand() % max_excl;
}

static double frand(double max)
{
  return max * rand() / RAND_MAX;
}

void read_image(void)
{
  int index = irand(sizeof(fnames)/sizeof(fnames[0]));
  FILE *fin = fopen(fnames[index], "rb");
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

int init(int moduleid, char* argstr)
{
   screenW = matrix_getx();
   screenH = matrix_gety();
   msample = malloc(screenW * screenH * MULTISAMPLE * MULTISAMPLE * sizeof(RGB));
   return 0;
}

void reset(int moduleid)
{
  t = frand(6.28);
  if (idata)
  {
    free(idata);
  }
  read_image();
  cscheme = irand(2);
}

Vec3d itom(int ix, int iy)
{
  Vec3d m;
  m.x = ((double) (ix + (is - iw) / 2) / is - 0.5) * 0.98;
  m.y = ((double) (iy + (is - ih) / 2) / is - 0.5) * 0.98;
  m.z = 0;
  
  return m;
}

void mtov(Vec3d m, double scale, int *x, int *y)
{
  *x = (m.x * (1.0 + m.z * 0.7) * scale + 0.5) * screenW * MULTISAMPLE + frand(2.0);
  *y = (m.y * (1.0 + m.z * 0.7) * scale + 0.5) * screenH * MULTISAMPLE + frand(2.0);
}

Vec3d rotateY(Vec3d m, double a)
{
  Vec3d o;
  o.x = cos(a) * m.x + sin(a) * m.z;
  o.y = m.y;
  o.z = -sin(a) * m.x + cos(a) * m.z;
  
  return o;
}

RGB scheme(Vec3d m, Vec3d mr, int x, int y)
{
  RGB col;

  switch(cscheme)
  {
    case 0:
      col.red =   MIN(mr.y * 1.33 + 0.5, 1.0) * 255;
      col.green = MIN(0.5 - mr.y * 1.33, 1.0) * 255;
      col.blue =  MIN(0.75,              1.0) * 255;
      break;
    case 1:
      col.red =   255;
      col.green = 255;
      col.blue =  255;
      break;
  }
  
  return col;
}

int draw(int moduleid, int argc, char* argv[])
{
  oscore_time now = udate();
  
  memset(msample, 0, screenW * screenH * MULTISAMPLE * MULTISAMPLE * sizeof(RGB));
  for (int iy = 0; iy < ih; ++iy)
  {
    for (int ix = 0; ix < iw; ++ix)
    {
      if (!idata[iy * iw + ix]) continue;
      Vec3d m = itom(ix, iy);
      Vec3d mr = rotateY(m, t);
      double inten = (mr.z + 0.75);
      inten = MIN(inten, 1.0);
      int i, x, y;
      RGB col;
      mtov(mr, 1.0, &x, &y);
      col = scheme(m, mr, x, y);
      i = y * screenW * MULTISAMPLE + x;
      msample[i].red =   MAX(msample[i].red,   inten * col.red);
      mtov(mr, 0.99, &x, &y);
      col = scheme(m, mr, x, y);
      i = y * screenW * MULTISAMPLE + x;
      msample[i].green = MAX(msample[i].green, inten * col.green);
      mtov(mr, 0.98, &x, &y);
      col = scheme(m, mr, x, y);
      i = y * screenW * MULTISAMPLE + x;
      msample[i].blue =  MAX(msample[i].blue,  inten * col.blue);
    }
  }
  
  matrix_clear();
  for (int y = 0; y < screenH; ++y)
  {
    for (int x = 0; x < screenW; ++x)
    {
      double r = 0;
      double g = 0;
      double b = 0;
      for (int iy = 0; iy < MULTISAMPLE; ++iy)
      {
        for (int ix = 0; ix < MULTISAMPLE; ++ix)
        {
          int i = (y * MULTISAMPLE + iy) * screenW * MULTISAMPLE + x * MULTISAMPLE + ix;
          r += msample[i].red;
          g += msample[i].green;
          b += msample[i].blue;
        }
      }
      double f = 1.0 / (MULTISAMPLE * MULTISAMPLE);
      RGB col = RGB(r * f, g * f, b * f);
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
