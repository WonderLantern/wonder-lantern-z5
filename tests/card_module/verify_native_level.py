#!/usr/bin/env python3
"""Compile the real native-level ownership logic with checked firmware services mocked.

This tests GUI decisions and restoration, not the camera's renderer or timing.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/card_module/custom_disp/native_level.h').read_text()
source = source.replace('Z5_IMAGE(custom_disp_image) static native_level_state *native_level_data(void)',
                        'static native_level_state state;\nZ5_IMAGE(custom_disp_image) static native_level_state *native_level_data(void)')
source = source.replace('return (native_level_state *)disp_metadata()[31];', 'return &state;')
# Simulated ARM addresses address actual 32-bit words, without truncating host pointers.
source = re.sub(r'\(volatile u32 \*\)\((w(?:\[i\]|\[0\])?|controller) \+ (0x[0-9a-f]+u)\)',
                r'native_word(\1, \2)', source)
preamble = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32; typedef uint8_t u8;
#define CUSTOM_PAGE_COUNT 4u
#define Z5_IMAGE(name)
#define Z5_OVERLAY_RAM_LOW 0xC1800000u
#define Z5_OVERLAY_RAM_HIGH 0xC8700000u
static u32 flags,ready,shooting,ui,output,pass,page,native_mode,queries,poses,shows,meta[32];
static u32 bank,clears,invalidations,sync_change,stale[2];
static u32 addresses[4],words[4][64],original[4][64];
static u32 disp_flags(void){return flags;}
static u32 disp_output(void){return output;}
static u32 disp_pass_matches(u32 o){return pass==(o?2u:0u);}
static u32 disp_gui_ready(void){return ready;}
static u32 *disp_metadata(void){return meta;}
static u32 *native_word(u32 a,u32 offset){
 assert(offset%4==0);u32 n=(a-Z5_OVERLAY_RAM_LOW)/256u;
 assert(n<4);return &words[n][offset/4];
}
static u32 widget(u32 i){assert(i<4);return addresses[i];}
static u32 gui_mode(void){queries++;return ui;}
#define Z5_CUSTOM_DISP_SHOOTING_PAGE() shooting
#define Z5_GUI_UI_MODE gui_mode
#define Z5_NATIVE_DISP_PAGE page
#define Z5_NATIVE_LEVEL_MODE native_mode
#define Z5_NATIVE_LEVEL_WIDGET widget
static void pose(void){poses++;}
static void show(void){
 shows++;native_mode=output?3u:1u;
 *native_word(widget(0),0x28)=1;*native_word(widget(0),0x44)=1;
 for(u32 i=1;i<4;i++)*native_word(widget(i),0x48)=output?0xC20293A4u:0xC2029214u;
}
#define Z5_NATIVE_LEVEL_POSE pose
#define Z5_NATIVE_LEVEL_SHOW show
static void clear_level(void){clears++;stale[output]=0;native_mode=output?3u:1u;}
static void invalidate(void){invalidations++;}
static void sync_level(void){
 if(sync_change==1)bank+=256;if(sync_change==2)output^=1;
 if(sync_change==3)flags=0;if(sync_change==4)page=0;
 sync_change=0;
}
#define Z5_NATIVE_LEVEL_CLEAR clear_level
#define Z5_NATIVE_LEVEL_INVALIDATE invalidate
#define Z5_LEVEL_SYNC sync_level
static u32 read_bank(void){return bank;}
#define Z5_LEVEL_FAST_BUFFER read_bank()
'''
display=(ROOT/'src/card_module/custom_disp/display.c').read_text()
leading=display[display.index('Z5_IMAGE(custom_disp_image) static u32 disp_register_leading('):display.index('Z5_IMAGE(custom_disp_image) static u32 disp_register(')]
leading=leading.replace('*(volatile const u32 *)(pre + 0x4cu)', 'pre_callback[(pre-0x2000u)/256u]')
preamble+=r'''
#define WIDGET_ID 0x7ffdu
static u32 head[2],pre_callback[2],prepend_fail;
static u32 disp_canvas(u32 o){return 0x1000u+o*256u;}
static u32 disp_pre_widget(u32 o){return 0x2000u+o*256u;}
static u32 find_widget(u32 id,u32 canvas){assert(id==WIDGET_ID-1u);return head[(canvas-0x1000u)/256u];}
static u32 prepend_widget(u32 list,u32 pre){
 if(prepend_fail)return 0;head[(list-0x1000u-0x9cu)/256u]=pre;return 1;
}
#define Z5_HBASE_FIND find_widget
#define Z5_HBASE_PREPEND prepend_widget
'''
harness = r'''
static void reset(u32 out){
 memset(&state,0,sizeof state);memset(words,0,sizeof words);memset(meta,0,sizeof meta);
 flags=29;ready=shooting=1;ui=page=queries=poses=shows=0;output=out;pass=out?2:0;native_mode=0;
 bank=0xC2000000u;clears=invalidations=sync_change=0;stale[0]=stale[1]=1;
 for(u32 i=0;i<4;i++){addresses[i]=Z5_OVERLAY_RAM_LOW+i*256u;words[i][0x48/4]=100+i;}
 words[0][0x4c/4]=0x104B9FEFu;memcpy(original,words,sizeof words);
}
/* Same dispatch condition as the checked native walker: visibility is irrelevant. */
static u32 stale_callback_would_run(void){return words[0][0x44/4]!=0;}
int main(void){
 meta[26]=pre_callback[0]=pre_callback[1]=123;
 assert(disp_register_leading(0)&&disp_register_leading(1));
 assert(head[0]==disp_pre_widget(0)&&head[1]==disp_pre_widget(1));
 assert(disp_register_leading(1));head[0]=0x9999;
 assert(!disp_register_leading(0)&&head[0]==0x9999);
 head[0]=0;prepend_fail=1;assert(!disp_register_leading(0));prepend_fail=0;
 pre_callback[1]=0;assert(!disp_register_leading(1));
 for(u32 out=0;out<2;out++)for(u32 pg=0;pg<4;pg++)for(u32 f=0;f<32;f++){
  reset(out);flags=f;page=pg;native_level_prepare();assert(shows==((f&19)==17));
  if(shows){
   assert(state.active&&state.prepared&&poses==1&&native_mode==(out?3u:1u));
   assert(stale_callback_would_run());
   native_level_prepare();assert(shows==1&&poses==1);
   /* Release before the callback ran must still disarm it before mode rollback. */
   native_level_release();assert(!state.active&&!state.prepared&&!native_mode);
   assert(!stale_callback_would_run());
   for(u32 i=0;i<4;i++)assert(words[i][0x48/4]==100+i);
  }
  native_level_release();assert(!state.suppressed);
 }
 /* Every custom Native page forces pose/show once. Off/TypeA suppress all
  * four dirty-only children, including artwork queued by cold boot. */
 for(u32 f=0;f<32;f++){
  reset(1);page=3;flags=f;
  for(u32 i=0;i<4;i++)words[i][0x44/4]=words[i][0x28/4]=1;
  native_level_prepare();assert(shows==((f&19u)==17u));
  if((f&1u)&&(f&19u)!=17u){
   assert(state.suppressed==15&&!stale_callback_would_run());
   for(u32 i=0;i<4;i++)assert(!words[i][0x28/4]&&!words[i][0x44/4]);
   /* A later stock stage may queue the image children again. */
   for(u32 i=0;i<4;i++)words[i][0x44/4]=words[i][0x28/4]=1;
   native_level_prepare();native_level_release();
   for(u32 i=0;i<4;i++)assert(words[i][0x28/4]==1&&!words[i][0x44/4]);
  }else assert(stale_callback_would_run());
 }
 /* Cold-start repair waits for registration, invalidates before native
  * painters, erases once per bank/mode and leaves steady state untouched. */
 for(u32 out=0;out<2;out++)for(u32 f=1;f<=17;f+=(f==1?2:14)){
  reset(out);page=3;flags=f;
  native_level_begin();native_level_prepare();assert(!clears&&!invalidations);
  native_level_release();meta[7]=3;
  native_level_begin();assert(invalidations==1&&state.repair_pending[out]==bank);
  native_level_prepare();assert(clears==1&&!stale[out]&&stale[out^1u]);
  native_level_release();assert(native_mode==0);
  native_level_begin();native_level_prepare();native_level_release();
  assert(clears==1&&invalidations==1);
  bank+=256;native_level_begin();native_level_prepare();native_level_release();
  assert(clears==2&&invalidations==2);
  flags=f==17?3:17;native_level_begin();native_level_prepare();native_level_release();
  assert(clears==3&&invalidations==3);
  page=0;native_level_begin();native_level_prepare();native_level_release();
  assert(clears==3&&invalidations==3&&!state.repair_key[out]);
  page=3;native_level_begin();native_level_prepare();native_level_release();assert(clears==4);
 }
 /* A GPU wait can yield: reject changed bank/output/page/settings before erase. */
 for(u32 change=1;change<=4;change++){
  reset(0);page=3;flags=3;meta[7]=3;native_level_begin();sync_change=change;
  native_level_prepare();native_level_release();assert(!clears);
 }
 reset(0);page=3;flags=3;meta[7]=3;
 for(u32 i=0;i<4;i++){
  bank=0xC2000000u+i*256u;native_level_begin();native_level_prepare();native_level_release();
 }
 assert(clears==4&&invalidations==4);
 for(u32 i=0;i<8;i++){
  bank=0xC2000000u+(i%4)*256u;native_level_begin();native_level_prepare();native_level_release();
 }
 assert(clears==4&&invalidations==4);
 /* Native handlers can switch pages after begin(), or remove the private
  * widgets for temporary UI. Both cases must rearm the next takeover. */
 page=0;native_level_release();assert(!state.repair_key[0]&&!state.repair_key[1]);
 page=3;native_level_begin();native_level_prepare();native_level_release();assert(clears==5);
 native_level_forget();native_level_begin();native_level_prepare();native_level_release();assert(clears==6);
 for(u32 failure=0;failure<2;failure++){
  reset(0);page=3;flags=3;meta[7]=3;meta[failure?20:3]=1;
  native_level_begin();native_level_prepare();native_level_release();assert(!clears&&!invalidations);
 }
 /* Visibility changed by firmware after suppression takes precedence. */
 reset(0);flags=3;native_level_prepare();words[2][0x28/4]=2;
 native_level_release();assert(words[2][0x28/4]==2);
 reset(0);native_level_prepare();output=1;pass=2;native_level_prepare();
 assert(shows==2&&poses==2&&state.projected_mode==3&&native_mode==3);
 for(u32 i=1;i<4;i++)assert(words[i][0x48/4]==0xC20293A4u);
 native_level_release();assert(!stale_callback_would_run()&&!native_mode);
 /* Successive APPLV callbacks may configure again; no cross-call state survives. */
 native_level_prepare();assert(shows==3);words[0][0x44/4]=0;
 native_level_release();assert(!stale_callback_would_run());
 reset(1);ui=shooting=0;native_level_prepare();assert(state.active&&!queries);
 reset(0);ui=1;native_level_prepare();assert(!state.active&&!shows);
 reset(0);shooting=0;native_level_prepare();assert(!state.active&&!shows);
 reset(0);native_level_prepare();words[1][0x28/4]=2;words[2][0x48/4]=999;native_mode=9;
 native_level_release();assert(words[1][0x28/4]==2&&words[2][0x48/4]==999&&native_mode==9);
 reset(0);native_level_prepare();page=3;flags=0;native_level_release();
 assert(!state.active&&words[0][0x28/4]==1&&native_mode==1&&stale_callback_would_run());
 for(u32 gate=0;gate<8;gate++){
  reset(1);
  if(gate==0)ready=0;if(gate==1)page=4;if(gate==2)pass=1;
  if(gate==3)meta[3]=1;if(gate==4)meta[20]=1;if(gate==5)addresses[0]=0;
  if(gate==6)words[0][0x4c/4]=0;if(gate==7)words[2][0x28/4]=2;
  memcpy(original,words,sizeof words);native_level_prepare();
  assert(!state.active&&!shows&&!poses&&!memcmp(original,words,sizeof words));
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-native-scoped-') as temp:
    p = Path(temp)
    (p/'check.c').write_text(preamble+source+leading+harness)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'check.c'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
# Static lifecycle check: permission is not left enabled after original callback.
display=(ROOT/'src/card_module/custom_disp/display.c').read_text()
assert 'u32 result = original(event,a1,a2,a3);\n    native_level_release();' in display
assert 'if (!disp_register_leading(0u) || !disp_register_leading(1u)) return 0;' in display
assert 'if (event == 9u) native_level_begin();' in display
assert 'native_level_forget();' in display[display.index('static void disp_remove('):display.index('static void disp_dirty(')]
print('PASS: native pose/setup on LCD/EVF, 32 flags on four pages, four-child suppression, cold DISP3 bank/mode/entry repair with no steady-state invalidation, GPU-wait revalidation, per-call setup, output transition, dirty disarm before rollback, foreign changes and stock DISP3 handoff; dual-leading registration; renderer/timing not executed')
