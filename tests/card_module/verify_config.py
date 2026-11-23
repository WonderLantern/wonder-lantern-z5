#!/usr/bin/env python3
"""Host compilation of the actual save/load functions with an in-memory file."""
from pathlib import Path
import tempfile,subprocess,re
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/card_module/main.c').read_text(encoding='utf-8')
def function(name):
 start=re.search(r'IMG_FN static (?:void|u32) '+name+r'\([^;{]+\)\n\{',source).start()
 end=source.index('{',start);depth=1;cursor=end+1
 while depth:
  depth+=(source[cursor]=='{')-(source[cursor]=='}');cursor+=1
 return source[start:cursor]
code=(ROOT/'src/card_module/custom_disp/settings.h').read_text(encoding='utf-8')+r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32;
#define IMG_FN
#define BULB_MENU
#define ASPECT43_FX
#define A43_PROFILES 1u
typedef struct {u32 h,m,s,cnt_i,gap_i,a43_sel;} bt_t;
#define BTP(c) ((c)->timer)
#define CFG_MAGIC 0x4D53355Au
#define FS_OPEN_OK 0x043A0080u
#define FS_CREATE 0x30Au
#define STYLE_STOCK 0u
#define STYLE_CROSS 2u
#define FW(type,name) name
#define BURST_CFG_MECH_OFF 0x10u
#define BURST_CFG_SIL_OFF 0x20u
typedef struct {u32 cfg_ms,cfg_busy;}custom_disp_state;
#define PH_DONE 12u
#define CM_MS_CLOCK 0u
#define FS_WRITE_EXISTING 0xAu
static u32 cfg_clock;
typedef struct {u32 edited,save_err,stage,saves,on,type,vol,grid,zebra,vui,focal_on,bat_on,touch_on,custom_flags,custom_control,cfg_state,burst_mech,burst_sil;
 bt_t *timer; uintptr_t custom_state;u32 phase,err,reload,fx,snd_mech;char cfg_path[24];}cm_t;
static _Alignas(u32) unsigned char file[24];static u32 length=8,exists=1,position;
static u32 fail_open,fail_seek,fail_read,fail_write,readback_fail,writes,opens,recoveries;
static void zero_words(u32 *p,u32 n){memset(p,0,n*4);}
static void FW_BIND(u32 *f,char *path){(void)f;(void)path;}
static u32 FW_OPEN(u32 *f,u32 mode,u32 unused){
 (void)unused;opens++;if(fail_open||!exists)return 0;f[2]=1;
 assert(mode!=FS_CREATE);assert(mode==0||mode==FS_WRITE_EXISTING);
 position=mode==FS_WRITE_EXISTING?length:0;return FS_OPEN_OK;
}
static void FW_CLOSE(u32 *f){(void)f;}
static int FW_SEEK(u32 handle,u32 offset,u32 origin,u32 *out){
 assert(handle==1);if(fail_seek)return 0;
 position=(origin==2?length:0)+offset;*out=position;return 1;
}
/* Match the disassembled readat: on error/short transfer dst is untouched. */
static int FW_READAT(u32 *f,void *data,u32 offset,u32 count){
 (void)f;if(fail_read||(readback_fail&&writes)||offset>length||count>length-offset)return 0;
 memcpy(data,file+offset,count);return 1;
}
static void FW_WRITE(u32 handle,void *data,u32 count,u32 *out){
 assert(handle==1);writes++;if(fail_write){*out=0;return;}
 assert(position+count<=sizeof file);memcpy(file+position,data,count);
 position+=count;if(position>length)length=position;*out=count;
}
static u32 load_clip(cm_t *c){(void)c;recoveries++;return 0;}
static void fx_apply(cm_t *c){(void)c;}
static void custom_apply(cm_t *c){(void)c;}
static void refresh_labels(cm_t *c){(void)c;}
static void hub_labels(cm_t *c){(void)c;}
'''+function('cfg_defaults')+'\n'+function('cfg_timer_valid')+'\n'+function('cfg_load')+'\n'+function('cfg_ready').replace('*(volatile u32 *)CM_MS_CLOCK','cfg_clock')+'\n'+function('cfg_retry').replace('*(volatile u32 *)CM_MS_CLOCK','cfg_clock')+'\n'+function('save_cfg')+r'''
int main(void){
 cm_t c={0};u32 legacy[2]={CFG_MAGIC,1u|(2u<<8)|(4u<<16)|(1u<<27)};
 memcpy(file,legacy,8);assert(!cfg_load(&c));assert(c.custom_flags==CUSTOM_DEFAULT_FLAGS&&c.vui==1&&c.type==2&&c.vol==4);
 /* Exhaust all 20-bit page packs, with burst switches independent. */
 for(u32 packed=0;packed<=CUSTOM_PAGES_MASK;packed++){
  c.burst_mech=packed&1;c.burst_sil=(packed>>1)&1;c.custom_flags=packed;
  save_cfg(&c);assert(length==20&&!memcmp(file,legacy,8));
  assert((((u32 *)file)[2]&0xff000000u)==CUSTOM_CFG_TAG);
  cm_t fresh={0};assert(!cfg_load(&fresh));
  u32 normalized=packed | CUSTOM_ENABLE_MASK;
  for(u32 page=0;page<4;page++)if((packed>>(page*5))&16u)normalized&=~(2u<<(page*5));
  assert(fresh.custom_flags==normalized&&fresh.burst_mech==c.burst_mech&&fresh.burst_sil==c.burst_sil);
 }
 length=12;
 for(u32 tag=0;tag<2;tag++)for(u32 bits=0;bits<128;bits++) {
  ((u32 *)file)[2]=(tag?0xD2160000u:0xD2150000u)|bits;assert(!cfg_load(&c));
  u32 flags=(bits&15u)|(tag?((bits>>2)&16u):0u);
  if(flags&16u)flags&=~2u;
  assert(c.custom_flags==(flags | CUSTOM_ENABLE_MASK)&&c.burst_mech==!(bits&16)&&c.burst_sil==!(bits&32));
 }
 c.burst_mech=1;c.burst_sil=1;
 ((u32 *)file)[2]=0x11110000;c.custom_flags=123;
 assert(cfg_load(&c)==2&&c.custom_flags==123);
 length=8;assert(!cfg_load(&c));assert(c.custom_flags==CUSTOM_DEFAULT_FLAGS);

 exists=0;c.custom_flags=123;assert(cfg_load(&c)==1&&c.custom_flags==123);
 /* Failed startup read cannot enable a save of defaults. */
 unsigned char backup[24];memcpy(backup,file,sizeof file);u32 n=writes;
 save_cfg(&c);assert(writes==n&&!memcmp(file,backup,sizeof file));
 exists=1;length=12;u32 saved[3]={CFG_MAGIC,legacy[1],0xD7218421u};memcpy(file,saved,12);
 for(u32 failure=0;failure<3;failure++){
  c.custom_flags=123;c.vui=0;
  fail_open=failure==0;fail_seek=failure==1;fail_read=failure==2;
  assert(cfg_load(&c)==(failure==0?1u:3u));assert(c.custom_flags==123&&!c.vui);
  n=writes;save_cfg(&c);assert(writes==n&&!memcmp(file,saved,12));
 }
 fail_open=fail_seek=fail_read=0;
 for(u32 bad=0;bad<3;bad++){
  length=bad==0?0:bad==1?10:12;
  if(bad==2)((u32 *)file)[0]=0;
  assert(cfg_load(&c)==2&&c.custom_flags==123);
 }
 memcpy(file,saved,12);length=12;assert(!cfg_load(&c));
 assert(c.custom_flags==CUSTOM_NORMALIZE(saved[2] | CUSTOM_ENABLE_MASK)&&c.burst_mech==1&&c.burst_sil==0);
 /* No truncate: seek failure and zero-byte write preserve the previous file. */
 for(u32 failure=0;failure<2;failure++){
  n=c.saves;memcpy(backup,file,sizeof file);
  fail_seek=failure==0;fail_write=failure==1;save_cfg(&c);
  assert(c.saves==n&&length==12&&!memcmp(file,backup,sizeof file));
 }
 fail_seek=fail_write=0;readback_fail=1;n=c.saves;
 save_cfg(&c);assert(c.saves==n);readback_fail=0;
 /* Matching first words are insufficient if overwrite left an appended tail. */
 length=24;n=c.saves;save_cfg(&c);assert(c.saves==n);length=12;
 save_cfg(&c);assert(c.saves==n+1&&c.edited);
 /* Delayed GUI retry publishes the entire saved configuration; no user edit
  * and no boot/retry path ever creates/truncates/writes the file. */
 custom_disp_state runtime={0};c.custom_state=(uintptr_t)&runtime;c.phase=PH_DONE;
 memcpy(file,saved,12);length=12;cfg_defaults(&c);fail_read=1;assert(cfg_load(&c)==3);
 cfg_clock=499;n=opens;cfg_retry(&c);assert(opens==n);
 cfg_clock=500;n=writes;cfg_retry(&c);assert(c.cfg_state==3&&writes==n&&runtime.cfg_ms==500);
 fail_read=0;cfg_clock=999;u32 recovered=recoveries;cfg_retry(&c);assert(recoveries==recovered);
 cfg_clock=1000;cfg_retry(&c);assert(!c.cfg_state&&recoveries==recovered+1&&writes==n);
 assert(c.vui==1&&c.custom_flags==CUSTOM_NORMALIZE(saved[2] | CUSTOM_ENABLE_MASK));
 n=opens;cfg_clock=1500;cfg_retry(&c);assert(opens==n);
 /* A mutation first recovers the saved baseline; it cannot reset other pages. */
 cfg_defaults(&c);fail_read=1;assert(cfg_load(&c)==3);assert(!cfg_ready(&c));
 fail_read=0;assert(cfg_ready(&c));u32 flags=c.custom_flags;c.custom_flags^=4u<<15;save_cfg(&c);
 cm_t fresh={0};assert(!cfg_load(&fresh)&&fresh.custom_flags==(flags^(4u<<15)));
 runtime.cfg_busy=1;assert(!cfg_ready(&c));runtime.cfg_busy=0;
 /* Unsigned elapsed time also works across clock wrap. */
 cfg_defaults(&c);fail_read=1;assert(cfg_load(&c)==3);runtime.cfg_ms=0xffffff00u;
 cfg_clock=0xf3;n=opens;cfg_retry(&c);assert(opens==n);
 cfg_clock=0xf4;fail_read=0;cfg_retry(&c);assert(!c.cfg_state);

 /* Both output memories and global switch survive a fresh read. ACTIVE
  * is transient; the GUI must establish the native base after startup. */
 for(u32 on=0;on<2;on++)for(u32 lcd=0;lcd<5;lcd++)for(u32 evf=0;evf<5;evf++){
  c.custom_control=on|CUSTOM_CONTROL_ACTIVE|(lcd<<4)|(evf<<8);save_cfg(&c);
  cm_t next={0};assert(!cfg_load(&next));
  assert(next.custom_control==(on|(lcd<<4)|(evf<<8)));
 }
 /* Malformed extended files never partially replace live state. */
 u32 good[5];memcpy(good,file,20);u32 keep=c.custom_flags;
 for(u32 error=0;error<6;error++){
  memcpy(file,good,20);length=20;
  if(error==0)((u32 *)file)[4]^=1u<<31;
  if(error==1)((u32 *)file)[4]|=2u;
  if(error==2)((u32 *)file)[4]=CUSTOM_CONTROL_TAG|0x50u;
  if(error==3)((u32 *)file)[4]=CUSTOM_CONTROL_TAG|0x500u;
  if(error==4)((u32 *)file)[2]|=1u<<23;
  if(error==5)((u32 *)file)[3]=0xB7180000u;
  assert(cfg_load(&c)==2&&c.custom_flags==keep);
 }
 memcpy(file,good,20);assert(!cfg_load(&c));
 /* A19 migrates without enabling custom DISP or losing the timer. */
 bt_t timer={0};c.timer=&timer;
 u32 a19[]={CFG_MAGIC,legacy[1],0xD215002Fu,0xB7173B3Bu};
 memcpy(file,a19,sizeof a19);length=sizeof a19;
 assert(!cfg_load(&c)&&timer.h==23&&timer.m==59&&timer.s==59);
 assert(!c.custom_control&&c.burst_mech&&!c.burst_sil);
 /* The DISP write keeps the original B7 word; a timer write keeps DISP. */
 c.custom_control=CUSTOM_CONTROL_ON|(2u<<4)|(3u<<8);save_cfg(&c);
 assert(length==20&&((u32 *)file)[3]==a19[3]);
 u32 disp=((u32 *)file)[4];timer.s=42;save_cfg(&c);
 assert(((u32 *)file)[4]==disp&&((u32 *)file)[3]==0xB7173B2Au);
 bt_t reboot={0};cm_t merged={.timer=&reboot};
 assert(!cfg_load(&merged)&&merged.custom_control==c.custom_control);
 assert(reboot.h==23&&reboot.m==59&&reboot.s==42);
 assert(merged.burst_mech&&!merged.burst_sil);
 /* Fork's 16-byte routing remains intact and gets the default Bulb time. */
 u32 fork[]={CFG_MAGIC,legacy[1],((u32 *)file)[2],disp};
 memcpy(file,fork,sizeof fork);length=sizeof fork;
 assert(!cfg_load(&c)&&c.custom_control==merged.custom_control);
 assert(!timer.h&&timer.m==1&&!timer.s);
 save_cfg(&c);assert(length==20&&((u32 *)file)[3]==0xB7000100u&&((u32 *)file)[4]==disp);
 /* A failed 20-byte read must leave both active settings untouched. */
 fail_read=1;timer.m=17;c.custom_control=0;assert(cfg_load(&c)==3);
 assert(timer.m==17&&!c.custom_control);
 fail_read=0;assert(!cfg_load(&c)&&timer.m==1&&c.custom_control==merged.custom_control);
 /* A55: mechanical-shutter sound (word 1 bit 31) and the Bulb series steps (bits 11-15 / 19-23) round-trip with
  * every type and volume; all zero keeps the older word 1 byte for byte; older readers (whole type / volume
  * bytes) see out-of-range values only when a series is set and then keep their own defaults. */
 for(u32 mech=0;mech<2;mech++)for(u32 ci=0;ci<32;ci++)for(u32 gi=0;gi<32;gi++)for(u32 ty=1;ty<=5;ty++){
  c.snd_mech=mech;timer.cnt_i=ci;timer.gap_i=gi;c.type=ty;c.vol=6-ty;c.on=ty&1;save_cfg(&c);
  u32 w1=((u32 *)file)[1];
  if(!mech&&!ci&&!gi)assert(w1==(c.on|(ty<<8)|((6-ty)<<16)|(c.grid<<24)|(c.zebra<<26)|(c.vui<<27)|
   ((c.focal_on?0u:1u)<<28)|((c.bat_on?0u:1u)<<29)|((c.touch_on?0u:1u)<<30)));
  if(!ci)assert(((w1>>8)&0xFFu)==ty);
  if(!gi)assert(((w1>>16)&0xFFu)==6-ty);
  bt_t t2={0};cm_t n2={.timer=&t2};assert(!cfg_load(&n2));
  assert(n2.snd_mech==mech&&t2.cnt_i==ci&&t2.gap_i==gi&&n2.type==ty&&n2.vol==6-ty&&n2.on==(ty&1));
  assert(t2.h==timer.h&&t2.m==timer.m&&t2.s==timer.s);
 }
 /* Files without the A55 bits load with mechanical sound off and one shot, no interval. */
 c.snd_mech=0;timer.cnt_i=0;timer.gap_i=0;save_cfg(&c);
 {bt_t t3={.cnt_i=9,.gap_i=9};cm_t n3={.timer=&t3,.snd_mech=1};assert(!cfg_load(&n3)&&!n3.snd_mech&&!t3.cnt_i&&!t3.gap_i);}
 /* A69: photo aspect in word 4 bits 12-15 round-trips; 0 keeps the older word 4; out of range is refused */
 {u32 w4=((u32 *)file)[4];assert(!(w4&0xF000u));
  timer.a43_sel=1;save_cfg(&c);assert(((u32 *)file)[4]==(w4|0x1000u));
  bt_t t4={0};cm_t n4={.timer=&t4};assert(!cfg_load(&n4)&&t4.a43_sel==1&&n4.custom_control==c.custom_control);
  timer.a43_sel=0;save_cfg(&c);assert(((u32 *)file)[4]==w4);
  ((u32 *)file)[4]=w4|0x2000u;bt_t t5={.a43_sel=7};cm_t n5={.timer=&t5};assert(cfg_load(&n5)==2&&t5.a43_sel==7);
  ((u32 *)file)[4]=w4;assert(!cfg_load(&n5)&&!t5.a43_sel);}
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-custom-config-') as t:
 p=Path(t);(p/'check.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: exhaustive 20-bit preferences, legacy migration, global switch and both page memories; 8/12/16/20-byte reads and A19/L39 migrations; open/seek/read failures preserve RAM and disk; zero-byte writes do not truncate; readback verification; 500ms GUI retry, no boot writes, recovered baseline before edits, busy guard and clock wrap; A55 mechanical-sound bit and Bulb series steps round-trip, zero keeps the old word 1; A69 photo aspect in word 4 round-trips, 0 = older file, out of range refused')
