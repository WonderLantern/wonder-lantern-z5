#!/usr/bin/env python3
"""Host compilation of the actual save/load functions with an in-memory file."""
from pathlib import Path
import tempfile,subprocess
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/card_module/main.c').read_text()
def function(name):
 start=source.index('IMG_FN static '+('void' if name=='save_cfg' else 'u32')+' '+name+'(')
 end=source.index('{',start);depth=1;cursor=end+1
 while depth:
  depth+=(source[cursor]=='{')-(source[cursor]=='}');cursor+=1
 return source[start:cursor]
code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32;
#define IMG_FN
#define CFG_MAGIC 0x4D53355Au
#define FS_OPEN_OK 0x043A0080u
#define FS_CREATE 0x30Au
#define STYLE_STOCK 0u
#define STYLE_CROSS 2u
#define FW(type,name) name
typedef struct {u32 edited,save_err,stage,saves,on,type,vol,grid,zebra,vui,focal_on,bat_on,touch_on,custom_flags,cfg_state;char cfg_path[24];}cm_t;
static unsigned char file[16];static u32 length=8,exists=1;
static void zero_words(u32 *p,u32 n){memset(p,0,n*4);}
static void FW_BIND(u32 *f,char *path){(void)f;(void)path;}
static u32 FW_OPEN(u32 *f,u32 mode,u32 unused){(void)unused;if(!exists)return 0;f[2]=1;if(mode==FS_CREATE)length=0;return FS_OPEN_OK;}
static void FW_CLOSE(u32 *f){(void)f;}
static void FW_READAT(u32 *f,void *data,u32 offset,u32 count){(void)f;if(offset>=length)return;if(count>length-offset)count=length-offset;memcpy(data,file+offset,count);}
static void FW_WRITE(u32 handle,void *data,u32 count,u32 *out){(void)handle;memcpy(file,data,count);length=count;*out=count;}
'''+function('save_cfg')+'\n'+function('cfg_load')+r'''
int main(void){
 cm_t c={0};u32 legacy[2]={CFG_MAGIC,1u|(2u<<8)|(4u<<16)|(1u<<27)};
 memcpy(file,legacy,8);assert(!cfg_load(&c));assert(c.custom_flags==15&&c.vui==1&&c.type==2&&c.vol==4);
 for(u32 flags=0;flags<16;flags++){
  c.custom_flags=flags;save_cfg(&c);assert(length==12&&!memcmp(file,legacy,8));
  cm_t fresh={0};assert(!cfg_load(&fresh));assert(fresh.custom_flags==flags&&fresh.type==2&&fresh.vol==4);
 }
 ((u32 *)file)[2]=0x11110000;assert(!cfg_load(&c));assert(c.custom_flags==15);
 length=8;assert(!cfg_load(&c));assert(c.custom_flags==15);
 exists=0;assert(cfg_load(&c)==1&&c.custom_flags==15);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-custom-config-') as t:
 p=Path(t);(p/'check.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: legacy 8-byte settings, 16 flag combinations round-trip, native settings preserved, invalid extension and missing file')
