#!/usr/bin/env python3
"""Run the shared C raster with bounds-checking LCD/portrait/EVF pixel adapters."""
from pathlib import Path
import subprocess
import tempfile

HEADER = Path(__file__).resolve().parents[2] / 'src/card_module/custom_disp'
CODE = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32;
static u32 bins[256], reads[256], image[1280*960], ori, width, height, evf;
static u32 bin(u32 index) {assert(index<256);reads[index]++;return bins[index];}
static void fill(u32 x,u32 y,u32 w,u32 h,u32 pixel) {
 assert(w&&h&&x+w<=192&&y+h<=96);
 for(u32 row=y;row<y+h;row++)for(u32 col=x;col<x+w;col++) {
  u32 px,py;
  if(ori==1){px=144+row;py=335-col;}
  else if(ori==2){px=575-row;py=144+col;}
  else {px=(evf?979:476)+col;py=(evf?731:278)+row;}
  assert(px<width&&py<height);
  image[py*width+px]=pixel;
 }
}
static void paint(u32 scale) {
 assert(scale);
#define HIST_FILL(x,y,w,h,p) fill((x),(y),(w),(h),(p))
#define HIST_COLOR(i) (i)
#define HIST_BIN(i) bin(i)
#define HIST_DIV(n,d) ((n)/(d))
#include "histogram_raster.h"
#undef HIST_FILL
#undef HIST_COLOR
#undef HIST_BIN
#undef HIST_DIV
}
int main(void) {
 for(u32 pass=0;pass<4;pass++) {
  evf=pass==3;width=evf?1280:720;height=evf?960:480;ori=evf?0:pass;
  for(u32 seed=0;seed<260;seed++) {
   for(u32 i=0;i<256;i++)bins[i]=seed<256?(i==seed?128:0):(seed==256?0:seed==257?128:i);
   memset(reads,0,sizeof reads);memset(image,0,sizeof image);paint(seed==259?1:128);
   for(u32 i=0;i<256;i++)assert(reads[i]==1);
   u32 covered=0;
   for(u32 y=0;y<height;y++)for(u32 x=0;x<width;x++)if(image[y*width+x]) {
    covered++;assert(image[y*width+x]==1||image[y*width+x]==729||image[y*width+x]==730||image[y*width+x]==756);
   }
   assert(covered==192*96);
  }
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-histogram-raster-') as scratch:
    root = Path(scratch)
    (root / 'check.c').write_text(CODE)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(HEADER), str(root / 'check.c'), '-o', str(root / 'check')], check=True)
    subprocess.run([str(root / 'check')], check=True)
print('PASS: actual shared C raster, all 256 bins read once, each peak bin, zero/full/clipped data, LCD/portrait/EVF adapters and pixel bounds')
