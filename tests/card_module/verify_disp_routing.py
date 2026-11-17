#!/usr/bin/env python3
"""Run actual retained routing C with native properties/input/GUI mocked.

Checks logical page memory and ordering; does not execute Nikon/ARM code.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/card_module/custom_disp/route.inc').read_text()
source = source.replace('((volatile u32 *)0xC18289F4u)', '(&key_slot)')
source = source.replace('(*(volatile u8 *)0xC2028442u)', '(native_page)')
for address, value in [('0xC18119A0u', 'app'), ('0xC18289DCu', 'ui'), ('0xC18217C0u', 'busy')]:
    source = source.replace('*(volatile u8 *)' + address, value)
    source = source.replace('*(volatile u32 *)' + address, value)
code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define IMG_FN
''' + (ROOT / 'src/card_module/custom_disp/settings.h').read_text() + r'''
typedef struct {u32 key_entry,route_owned,route_applied[2],route_pending,route_save,cfg_busy,pending;}custom_disp_state;
typedef struct {u32 cfg_state,custom_disp,custom_disp_rc,custom_flags,custom_control;custom_disp_state *custom_state;}cm_t;
typedef u32 (*read_fn)(u32);
static cm_t c;static custom_disp_state state;
static u32 app=3,ui,busy,output,key_slot,native_page,mode,props[2],saved_control;
static u32 blocked,prefix_block,prefix_other,switch_in_core,saves,gui,off,forwarded,sets,applies;
static cm_t *cm_get(void){return &c;}
static u32 custom_output(void){return output;}
static u32 custom_logical_page(cm_t *owner){return CUSTOM_LOGICAL(owner->custom_control,output);}
static void custom_route_sync(cm_t *owner);
static void custom_apply(cm_t *owner){applies++;custom_route_sync(owner);}
static void save_cfg(cm_t *owner){saves++;saved_control=owner->custom_control&CUSTOM_CONTROL_MASK;}
static u32 setting(u32 key){if(key==0x168c)return mode;assert(key==0x3b15||key==0x3b16);return props[key-0x3b15];}
static void set_setting(u32 key,u32 value){assert(key==0x3b15||key==0x3b16);props[key-0x3b15]=value;sets++;}
static u32 set_page(u32 value){native_page=value;set_setting(0x3b15+output,value);return 0;}
static u32 native_disp(void){
 if(blocked)return blocked==1?0:0x100;
 if(switch_in_core){output^=1;switch_in_core=0;native_page=props[output];}
 assert(native_page==0||native_page==4);
 set_page(native_page==0?1:0);return 0x100;
}
static u32 native_gui(u32 zero){
 assert(!zero);gui++;assert(native_page==0||native_page==4);
 assert(native_page==(custom_logical_page(&c)==4?4u:0u));return 123;
}
static u32 display_off(u32 object){assert(object==0xc18289e0);assert(native_page==4);off++;return 0;}
static u32 native_key(u32 key){forwarded++;return key+1000;}
static u32 zero(void){return 0;}
static u32 prefix(void){return prefix_block;}
static u32 other(u32 key){assert(key==0x29);return prefix_other;}
static u32 zero_key(u32 key){(void)key;return 0;}
static void suffix(u32 a,u32 b,u32 d){assert(!a&&!b&&!d);}
static const void *service(u32 a){
 switch(a){
 case 0x10456098:return setting;case 0x104560a4:return set_setting;
 case 0x10341a92:return set_page;case 0x10341aaa:return native_disp;
 case 0x1030d744:return native_gui;case 0x1042de0a:return display_off;
 case 0x10304109:return native_key;case 0x10368156:return prefix;
 case 0x10307184:case 0x10307236:case 0x10307250:return other;
 case 0x102f5f56:case 0x102f5f46:case 0x103489e8:case 0x1042d5fa:case 0x1042c7a4:return zero;
 case 0x104555aa:case 0x10455712:return zero_key;case 0x10455570:return suffix;
 default:assert(0);return 0;
 }
}
#define FW(type,address) ((type)service(address))
''' + source + r'''
static void reset(void){
 memset(&state,0,sizeof state);memset(&c,0,sizeof c);c.custom_state=&state;
 state.key_entry=0xc5000001;c.custom_disp=1;c.custom_flags=CUSTOM_DEFAULT_FLAGS;
 c.custom_control=1;key_slot=CUSTOM_NATIVE_KEY;native_page=props[0]=2;props[1]=3;
 mode=ui=busy=output=blocked=prefix_block=prefix_other=switch_in_core=saves=gui=off=forwarded=sets=applies=0;app=3;
 custom_route_sync(&c);assert(key_slot==state.key_entry&&native_page==0&&props[0]==0&&props[1]==0);
 assert((c.custom_control&3)==3&&state.route_owned==3);
}
int main(void){
 reset();u32 before=sets;for(u32 i=0;i<120;i++)custom_route_sync(&c);assert(sets==before);
 /* Accepted native DISP advances one logical page; GUI sees only base 0.
  * LCD includes native display-off; EVF follows its native 4-page cycle. */
 for(u32 o=0;o<2;o++){
  output=o;native_page=props[o];
  u32 count=o?4:5;
  for(u32 n=1;n<=count*3;n++){
   u32 saved=saves;custom_key_dispatch(0x29);assert(custom_logical_page(&c)==n%count);
   assert(props[o]==(n%count==4?4u:0u)&&saves==saved+1);
   assert(CUSTOM_LOGICAL(saved_control,o)==n%count);
   assert(CUSTOM_LOGICAL(c.custom_control,!o)==0);
  }
 }
 /* Output switching during a yielding native service must not advance
  * the old output through the new output's property setter. */
 reset();switch_in_core=1;custom_key_dispatch(0x29);
 assert(output==1&&CUSTOM_LOGICAL(c.custom_control,0)==0&&CUSTOM_LOGICAL(c.custom_control,1)==1);
 assert(props[0]==0&&props[1]==0&&state.route_save);
 /* Non-DISP and native prefix/inner rejections keep identity and disk. */
 reset();assert(custom_key_dispatch(7)==1007&&forwarded==1);
 for(u32 reason=1;reason<=4;reason++){
  blocked=reason<=2?reason:0;prefix_block=reason==3;prefix_other=reason==4;
  custom_key_dispatch(0x29);assert(!custom_logical_page(&c)&&!saves);
 }
 reset();state.cfg_busy=1;custom_key_dispatch(0x29);assert(!saves);state.cfg_busy=0;
 /* Output switching selects independent remembered identity/preferences. */
 c.custom_control=CUSTOM_SET_LOGICAL(c.custom_control,0,2);
 c.custom_control=CUSTOM_SET_LOGICAL(c.custom_control,1,3);
 custom_route_sync(&c);assert(CUSTOM_ACTIVE_FLAGS(c.custom_flags,c.custom_control,0)==5);
 output=1;native_page=0;assert(CUSTOM_ACTIVE_FLAGS(c.custom_flags,c.custom_control,1)==17);
 custom_key_dispatch(0x29);assert(custom_logical_page(&c)==0&&CUSTOM_LOGICAL(c.custom_control,0)==2);
 /* Disable transfers identities back, restores only our callback/value. */
 c.custom_control&=~1u;custom_route_sync(&c);
 assert(key_slot==CUSTOM_NATIVE_KEY&&props[0]==2&&props[1]==0&&!(c.custom_control&2));
 assert(!CUSTOM_ACTIVE_FLAGS(c.custom_flags,c.custom_control,0));
 c.custom_control|=1;custom_route_sync(&c);assert(props[0]==0&&props[1]==0);
 props[0]=1;c.custom_control&=~1u;custom_route_sync(&c);assert(props[0]==1);
 /* Playback/video restore photo properties without touching video settings. */
 reset();c.custom_control=CUSTOM_SET_LOGICAL(c.custom_control,0,3);mode=1;
 custom_route_sync(&c);assert(props[0]==3&&props[1]==0&&key_slot==CUSTOM_NATIVE_KEY);
 mode=0;custom_route_sync(&c);assert(props[0]==0&&key_slot==state.key_entry);
 /* Unread config, failed module or callback conflict never takes ownership. */
 reset();c.cfg_state=1;custom_route_sync(&c);assert(!(c.custom_control&2)&&!state.route_owned);
 c.cfg_state=0;c.custom_disp_rc=-5;custom_route_sync(&c);assert(!(c.custom_control&2));
 c.custom_disp_rc=0;key_slot=0xc6000001;custom_route_sync(&c);assert(!(c.custom_control&2)&&key_slot==0xc6000001);
 /* Cold boot applies both saved identities; only LCD can remember off. */
 reset();c.custom_control=1|(4u<<4)|(3u<<8);custom_route_sync(&c);
 assert(props[0]==4&&props[1]==0&&native_page==4&&off==1);
 before=off;custom_route_sync(&c);assert(off==before);
 /* A native wake action may change page without going through DISP. */
 set_page(0);custom_route_sync(&c);assert(!custom_logical_page(&c)&&native_page==0&&state.route_save);
 c.custom_control=CUSTOM_SET_LOGICAL(c.custom_control,0,4);custom_route_sync(&c);
 custom_key_dispatch(0x29);assert(!custom_logical_page(&c)&&native_page==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-disp-routing-') as temp:
    p = Path(temp)
    (p / 'check.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined,bounds',
                    str(p / 'check.c'), '-o', str(p / 'check')], check=True)
    subprocess.run([str(p / 'check')], check=True)
print('PASS: actual routing C; native input guards, LCD/EVF cycles, base mapped before GUI, no per-frame property writes, page-memory saves, master-off restoration, video/config/conflict guards and cold display-off')
