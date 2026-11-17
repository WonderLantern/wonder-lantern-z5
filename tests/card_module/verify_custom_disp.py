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
initializers[0][38]=[0x0ff1ff]*12
initializers[0][37][1]=1

for image in images:
 data=image.read_bytes()
 def read(address,size):
  if address>=0xc0000000:
   address=profile['ram_init']['source']+address-profile['ram_init']['destination']
  return data[address-profile['base']:address-profile['base']+size]
 layout=struct.unpack('<I',read(0x1209955c+4,4))[0]
 assert {3,4}.issubset({struct.unpack('<I',read(layout+i*16,4))[0] for i in range(8)})
 groups=[]
 for i in range(64):
  descriptor=struct.unpack('<I',read(0x10748504+i*4,4))[0]
  groups.append(struct.unpack('<12I',read(descriptor,48)))
 # Battery group 18: native first rectangle (2,396)-(62,428), shared by both pages.
 descriptor=struct.unpack('<I',read(0x10748504+18*4,4))[0]
 payload=struct.unpack('<I',read(descriptor+44,4))[0]
 assert struct.unpack('<4H',read(payload+10,8))==(2,396,62,428)
 assert all((groups[18][j]&3)==3 for j in (1,3,4,6,7,9))
 # White balance: stock simple page permits it; both outputs' footprint.
 descriptor=struct.unpack('<I',read(0x10748504+38*4,4))[0]
 assert descriptor==0xc1826534
 payload=struct.unpack('<I',read(descriptor+44,4))[0]
 assert payload==0xc1826474
 assert struct.unpack('<8H',read(payload+10,16))==(592,2,660,38,854,288,922,324)
 assert all(groups[38][j]==0x0ff1ff for j in (1,3,4,6,7,9))
 initializers.append(groups)
source=(ROOT/'src/card_module/custom_disp_runtime.inc').read_text().split('IMG_FN static void custom_label')[0]
source=source.replace('#include "custom_disp/route.inc"', '')
source=source.replace('IMG_FN static void custom_route_sync(cm_t *c);', 'IMG_FN static void custom_route_sync(cm_t *c) {(void)c;}')
source=source.replace('*(volatile u32 *)0xC181AD60u == 4u', 'output')
source=source.replace('u32 descriptor = ((volatile const u32 *)0x10748504u)[group];','uintptr_t descriptor = descriptors[group];')
for address,name in [('0xC18119A0u','app'),('0xC18289DCu','ui'),('0xC2028442u','page')]:
 source=source.replace('*(volatile u8 *)'+address,name)
source=source.replace('(volatile u32 *)0xC1826118u','hist_masks')
source=source.replace('*(volatile u32 *)0xC18217C0u','busy').replace('FW(read_fn, 0x1042C00Cu)(12u)','0u').replace('FW(read_fn, 0x1042DF92u)(0u)','0u').replace('((void (*)(void))0x103422AFu)();','invalidations++;')
preamble=(ROOT/'src/card_module/custom_disp/settings.h').read_text()+'''#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
#define IMG_FN
#define RAM_LO 0u
#define RAM_HI UINTPTR_MAX
static uintptr_t descriptors[64];
static u32 hist_masks[9];
static u32 words[64][12], invalidations,busy,icons[64][0x70/4];
static u8 app=3,ui=0;static u32 output;
static u32 fail_cas;
static u32 host_cas(volatile u32 *p,u32 old,u32 value){
 if(fail_cas){fail_cas=0;return 0;}if(*p!=old)return 0;*p=value;return 1;
}
#define __sync_bool_compare_and_swap host_cas
typedef struct {u32 custom_flags,custom_control,cfg_state; uintptr_t custom_state,vui_ic;
 u32 vui_mtd_ok,vui_mtd_go,vui_bat_go,vui_strip_end,vui_bat_k,vui_hm_saved,vui_hm[9];} cm_t;
static void save_cfg(cm_t *c){(void)c;}
static const u32 initializers[][64][12]='''+json.dumps(initializers).replace('[','{').replace(']','}')+';\n'
harness=r'''
static u32 expected_mask(u32 value,u32 flags,u32 group) {
 if(!(flags&1))return value;
 u32 expected=value;
 for(u32 mode=0;mode<6;mode++){
  u32 bit=1u<<(mode*4),src=mode*4+((flags&8)?0u:1u);
  if(group!=32u&&(group!=38u||(flags&8u))&&(value&(1u<<src)))expected|=bit;else expected&=~bit;
 }
 return expected;
}
static void check(u32 flags,const u32 original[64][12]) {
 for(u32 i=0;i<64;i++)for(u32 j=0;j<12;j++) {
  u32 expected=original[i][j];
  if(j>=1&&j<=9&&j%3!=2)expected=expected_mask(expected,flags,i);
  assert(words[i][j]==expected);
 }
}
int main(void) {
 custom_disp_state s={0};cm_t c={.custom_state=(uintptr_t)&s,.vui_ic=(uintptr_t)icons};
 for(u32 i=0;i<64;i++)descriptors[i]=(uintptr_t)words[i];
 for(u32 fw=0;fw<sizeof(initializers)/sizeof(initializers[0]);fw++)
 for(output=0;output<2;output++)for(u32 p=0;p<4;p++)for(u32 f=1;f<32;f+=2){
  memcpy(words,initializers[fw],sizeof words);
  c.custom_flags=f<<(p*5);c.custom_control=CUSTOM_SET_LOGICAL(3,output,p);
  assert(custom_logical_page(&c)==p);
  custom_apply(&c);check(f,initializers[fw]);
  assert(custom_hide_native(&c)==!(f&8));
  u32 count=invalidations;custom_apply(&c);assert(invalidations==count);
  c.custom_control&=~1u;custom_apply(&c);check(0,initializers[fw]);
  for(u32 i=0;i<12;i++)assert(!s.owned[i]);
 }
 /* Change logical preferences without first restoring, for every original
  * nibble. Only physical DISP0 bits change; simple-page source stays stock. */
 output=0;
 for(u32 nibble=0;nibble<16;nibble++)for(u32 details=0;details<16;details++){
  u32 original[64][12];
  for(u32 i=0;i<64;i++)for(u32 j=0;j<12;j++)original[i][j]=words[i][j]=nibble*0x111111u;
  c.custom_flags=CUSTOM_ENABLE_MASK;
  for(u32 p=0;p<4;p++)c.custom_flags|=((details>>p)&1u)<<(p*5+3);
  for(u32 p=0;p<4;p++){
   c.custom_control=CUSTOM_SET_LOGICAL(3,0,p);custom_apply(&c);
   check(CUSTOM_PAGE_FLAGS(c.custom_flags,p),original);
  }
  c.custom_control=0;custom_apply(&c);check(0,original);
 }
 memcpy(words,initializers[0],sizeof words);c.custom_control=3;c.custom_flags=7;custom_apply(&c);
 words[37][1]^=0x80000000u;c.custom_control=0;custom_apply(&c);
 assert(words[37][1]==(initializers[0][37][1]^0x80000000u));
 words[37][1]=initializers[0][37][1];c.custom_control=3;custom_apply(&c);
 ui=3;custom_apply(&c);check(0,initializers[0]);
 ui=0;custom_apply(&c);check(7,initializers[0]);
 app=1;custom_apply(&c);check(0,initializers[0]);app=3;
 memcpy(words,initializers[0],sizeof words);c.custom_control=0;custom_apply(&c);
 c.custom_control=3;fail_cas=1;custom_apply(&c);assert(!s.flags_valid);
 custom_apply(&c);check(7,initializers[0]);c.custom_control=0;custom_apply(&c);
 memcpy(words,initializers[0],sizeof words);memcpy(c.vui_hm,words[32]+1,sizeof c.vui_hm);
 c.vui_hm_saved=1;memset(hist_masks,0,sizeof hist_masks);c.custom_flags=5u<<10;c.custom_control=3|(2u<<4);
 custom_apply(&c);assert(!c.vui_hm_saved);
 for(u32 j=0;j<9;j++)assert(hist_masks[j]==c.vui_hm[j]);
 assert((custom_project_mask(0xffffff,15u,18)&0x111111u)==0x111111u);
 assert(!(custom_project_mask(0x0ff1ff,1u,38u)&0x111111u));
 assert(custom_project_mask(0x0ff1ff,9u,38u)==0x0ff1ff);
 assert(custom_project_mask(0x0ff1ff,0u,38u)==0x0ff1ff);
 c.custom_control=3|(4u<<4);assert(!custom_current_flags(&c));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-custom-host-') as temp:
 path=Path(temp);(path/'check.c').write_text(preamble+source+harness)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'check.c'),'-o',str(path/'check')],check=True)
 subprocess.run([str(path/'check')],check=True)
print(f'PASS: synthetic masks + {len(images)} firmware initializers, four logical pages on native DISP0, both outputs/16 canonical flags, all nibble/base combinations and transitions, unmodified sources, native histogram suppression, white-balance detail toggle/restoration, battery, foreign writes, portrait shadow and restoration')
