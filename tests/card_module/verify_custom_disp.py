#!/usr/bin/env python3
"""Host-test the actual mask helper against both accepted firmware initializers."""
from pathlib import Path
import argparse, json, struct, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware',type=Path,action='append',default=[],help='optional accepted MAIN initializer to check (repeatable, read only)')
images=parser.parse_args().firmware
profile=json.loads((ROOT/'src/card_module/custom_disp/platform.json').read_text())
# Synthetic descriptor masks allow all host checks without a private firmware file.
initializers=[[[((i+j)%16)*0x111111 for j in range(12)] for i in range(64)]]
initializers[0][18]=[0xffffff]*12
initializers[0][37][1]=1

for image in images:
 data=image.read_bytes()
 def read(address,size):
  if address>=0xc0000000:
   address=profile['ram_init']['source']+address-profile['ram_init']['destination']
  return data[address-profile['base']:address-profile['base']+size]
 groups=[]
 for i in range(64):
  descriptor=struct.unpack('<I',read(0x10748504+i*4,4))[0]
  groups.append(struct.unpack('<12I',read(descriptor,48)))
 # Battery group 18: native first rectangle (2,396)-(62,428), shared by both pages.
 descriptor=struct.unpack('<I',read(0x10748504+18*4,4))[0]
 payload=struct.unpack('<I',read(descriptor+44,4))[0]
 assert struct.unpack('<4H',read(payload+10,8))==(2,396,62,428)
 assert all((groups[18][j]&3)==3 for j in (1,3,4,6,7,9))
 initializers.append(groups)
source=(ROOT/'src/card_module/custom_disp_runtime.inc').read_text().split('IMG_FN static void custom_label')[0]
source=source.replace('u32 descriptor = ((volatile const u32 *)0x10748504u)[group];','uintptr_t descriptor = descriptors[group];')
for address,name in [('0xC18119A0u','app'),('0xC18289DCu','ui'),('0xC2028442u','page')]:
 source=source.replace('*(volatile u8 *)'+address,name)
source=source.replace('*(volatile u32 *)0xC18217C0u','busy').replace('FW(read_fn, 0x1042C00Cu)(12u)','0u').replace('FW(read_fn, 0x1042DF92u)(0u)','0u').replace('((void (*)(void))0x103422AFu)();','invalidations++;')
preamble='''#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
#define IMG_FN
#define RAM_LO 0u
#define RAM_HI UINTPTR_MAX
static uintptr_t descriptors[64];
static u32 words[64][12], invalidations,busy,icons[64][0x70/4];
static u8 app=3,ui=0,page=0;
typedef struct {u32 custom_flags; uintptr_t custom_state,vui_ic;
 u32 vui_mtd_ok,vui_mtd_go,vui_bat_go,vui_strip_end,vui_bat_k;} cm_t;
static const u32 initializers[][64][12]='''+json.dumps(initializers).replace('[','{').replace(']','}')+';\n'
harness=r'''
static void check(u32 hide, const u32 original[64][12]) {
 for(u32 i=0;i<64;i++)for(u32 j=0;j<12;j++) {
  u32 expected=original[i][j];
  if(hide&&j>=1&&j<=9&&j%3!=2) {
   for(u32 k=0;k<6;k++) {
    u32 bit=1u<<(4*k);
    expected=(expected&~bit)|((original[i][j]&(bit<<1))>>1);
   }
  }
  assert(words[i][j]==expected);
 }
}
int main(void) {
 custom_disp_state s={0}; cm_t c={.custom_flags=15,.custom_state=(uintptr_t)&s,.vui_ic=(uintptr_t)icons};
 for(u32 i=0;i<64;i++)descriptors[i]=(uintptr_t)words[i];
 for(u32 fw=0;fw<sizeof(initializers)/sizeof(initializers[0]);fw++)for(u32 flags=0;flags<16;flags++) {
  memcpy(words,initializers[fw],sizeof words);
  for(u32 i=0;i<64;i++)icons[i][3]=123;
  c.custom_flags=flags;custom_apply(&c);
  u32 hide=(flags&1)&&!(flags&8);check(hide,initializers[fw]);
  assert(!memcmp(words[18],initializers[fw][18],sizeof words[18]));
  if(hide)for(u32 i=0;i<64;i++)assert(!icons[i][3]);
  u32 count=invalidations;custom_apply(&c);assert(invalidations==count);
  c.custom_flags=15;custom_apply(&c);check(0,initializers[fw]);
  for(u32 i=0;i<12;i++)assert(!s.owned[i]);
 }
 /* Every possible nibble, including modes where simple has extra native controls. */
 for(u32 nibble=0;nibble<16;nibble++) {
  u32 original[64][12];
  for(u32 i=0;i<64;i++)for(u32 j=0;j<12;j++)original[i][j]=words[i][j]=nibble*0x111111u;
  c.custom_flags=7;custom_apply(&c);check(1,original);
  c.custom_flags=15;custom_apply(&c);check(0,original);
 }
 memcpy(words,initializers[0],sizeof words);
 c.custom_flags=7;custom_apply(&c);words[37][1]=0x12345678u;
 c.custom_flags=15;custom_apply(&c);assert(words[37][1]==0x12345678u);
 words[37][1]=initializers[0][37][1];c.custom_flags=7;custom_apply(&c);
 ui=3;custom_apply(&c);check(0,initializers[0]);
 ui=0;page=1;custom_apply(&c);check(0,initializers[0]);
 page=0;custom_apply(&c);check(1,initializers[0]);
 app=1;custom_apply(&c);check(0,initializers[0]);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-custom-host-') as temp:
 path=Path(temp);(path/'check.c').write_text(preamble+source+harness)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'check.c'),'-o',str(path/'check')],check=True)
 subprocess.run([str(path/'check')],check=True)
print(f'PASS: synthetic masks + {len(images)} firmware initializers, 16 settings, all mode nibbles, simple-page parity, battery preserved, other fields/pages preserved, restore, foreign changes and icon metadata reset')
