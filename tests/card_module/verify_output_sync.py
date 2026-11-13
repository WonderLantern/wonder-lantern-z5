#!/usr/bin/env python3
"""Host checks of shared output sizes, sparse cache ranges and refresh clocks."""
from pathlib import Path
import subprocess
import tempfile

HEADER = Path(__file__).resolve().parents[2] / 'src/card_module/custom_disp'
# Compile the actual scheduling and row tracking helpers, with a simulated clock
# and cache service. No firmware callbacks, ARM code or camera timing are run.
CODE = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8; typedef int32_t s32;
#define Z5_IMAGE(name)
#include "output.h"
static u32 base, bytes, calls, covered[1280*960/16], cache_bytes;
static void cache(u32 address, u32 count) {
 assert(!(address&63u)&&!(count&63u)&&count&&count<=65536u);
 assert(address>=base&&address-base+count<=bytes+63u);
 calls++;cache_bytes+=count;
 for(u32 n=(address-base)/64u;n<(address-base+count)/64u;n++)covered[n]=1;
}
#define Z5_LEVEL_CACHE_RANGE cache
#include "sparse.h"
typedef struct {
 u32 active, level_armed, level_next, level_fraction, level_start, level_last;
 u32 level_frames, level_gap, level_hz, level_gap_max;
 u32 walk_tracking, walk_tick, walk_own_ms, source_reason, diag_custom_ms;
 u32 entries, installs, restores, reason, ready, drawing_image;
} overlay_state;
static overlay_state states[2];
static u32 output, clock_ms, reason, metadata[31];
static overlay_state *overlay_data(void) {return states+output;}
static u32 overlay_reason(void) {return reason;}
static volatile u32 *disp_metadata(void) {return metadata;}
static u32 level_div(u32 n,u32 d) {return n/d;}
#define Z5_FULL_TICK clock_ms
#define __attribute__(x)
#include "refresh.h"
#undef __attribute__

static void verify_walks(void);
static void verify_adapters(void);
int main(void) {
 assert(CUSTOM_DISP_HISTOGRAM_MS==120u);
 for(u32 wrap=0;wrap<2;wrap++) {
  u32 last=wrap?0xfffffff0u:100u;
  assert(!disp_histogram_due(last+119u,last,0u));
  assert(disp_histogram_due(last+120u,last,0u));
  assert(!disp_histogram_due(last+500u,last,1u));
 }
 const u32 sizes[][3]={{0,720,480},{1,720,480},{1,1024,682},{1,1024,768},{1,1280,960}};
 for(u32 mode=0;mode<5u;mode++) {
  output=sizes[mode][0];u32 w=sizes[mode][1],h=sizes[mode][2];
  assert(disp_size_supported(output,w,h));
  assert(!disp_size_supported(0,1280,960)&&!disp_size_supported(1,1280,961));
  sparse_dirty s={0}; base=0x20000000u;bytes=w*h*4u;calls=cache_bytes=0;memset(covered,0,sizeof covered);
  u32 marks[]={0,719,w-1,w+1,w*(h-1),w*h-1};
  for(u32 i=0;i<6u;i++)sparse_mark(&s,base,marks[i],w,h);
  assert(sparse_publish(&s,base));assert(calls&&cache_bytes<bytes/4u);
  for(u32 i=0;i<6u;i++)assert(covered[marks[i]/16u]);
  for(u32 y=0;y<h;y++)assert(s.rows[y][0]==0xffffu&&s.rows[y][1]==0u);
  u32 old_calls=calls;assert(sparse_publish(&s,base));assert(calls==old_calls);
  sparse_mark(&s,base,w*h,w,h);assert(sparse_publish(&s,base));assert(calls==old_calls);
  sparse_mark(&s,base,w*h-1,w,h);assert(sparse_publish(&s,base+0x800000u));assert(calls==old_calls);
  sparse_mark(&s,base+0x800000u,0,w,h);assert(s.surface==base+0x800000u);
  assert(s.rows[h-1][0]==0xffffu);assert(!sparse_publish(&s,0));assert(!sparse_publish(&s,base+1u));
  /* Dense native dirt still splits into bounded cache calls on a large EVF. */
  base=0x30000000u;sparse_reset(&s,base,w,h);calls=cache_bytes=0;
  for(u32 y=0;y<h;y++){sparse_mark(&s,base,y*w,w,h);sparse_mark(&s,base,y*w+w-1,w,h);}
  assert(sparse_publish(&s,base));assert(cache_bytes>=bytes);
 }
 /* Identical 60 Hz clocks and 16/17 ms steps on LCD and EVF, including wrap. */
 metadata[4]=1;
 for(output=0;output<2u;output++)for(u32 wrap=0;wrap<2u;wrap++) {
  memset(states+output,0,sizeof states[output]);states[output].active=1;reason=0;
  u32 count=0,last=0,start=wrap?0xffffff00u:100u;
  for(u32 t=0;t<1000u;t++) {
   clock_ms=start+t;
   if(overlay_refresh_due()){if(count)assert(t-last==16u||t-last==17u);last=t;count++;}
   assert(overlay_refresh_sleep()>=1u&&overlay_refresh_sleep()<=17u);
  }
  assert(count==60u);
  clock_ms+=500u;assert(overlay_refresh_due());assert(!overlay_refresh_due());
  reason=8;assert(!overlay_refresh_due());assert(!states[output].level_armed);assert(overlay_refresh_sleep()==33u);
  reason=0;assert(overlay_refresh_due());
  clock_ms=100;states[output].level_start=states[output].level_last=0;
  overlay_refresh_completed();for(u32 n=1;n<=60;n++){clock_ms=100+n*1000/60;overlay_refresh_completed();}
  assert(states[output].level_hz==60u);
 }
 /* Worker failure pauses the shared clock; its last timeout is not failure. */
 for(output=0;output<2u;output++) {
  metadata[20]=1;assert(overlay_refresh_sleep()==33u);metadata[20]=0;
  metadata[21]=0xffce0000u;assert(overlay_refresh_sleep()>=1u&&overlay_refresh_sleep()<=17u);metadata[21]=0;
 }
 assert(states[0].active&&states[1].active);
 verify_walks();verify_adapters();
 return 0;
}
'''
render = (HEADER / 'overlay_render.h').read_text()
later = render[render.index('static u32 overlay_native_later_walk'):render.index('/* Native GUI synchronization')]
wrapper = render[render.index('static u32 overlay_source_paint(u32 object, u32 walk_site)'):]
wrapper = wrapper.replace('*(volatile const u32 *)', '*word').replace('*(volatile u32 *)', '*word')
CODE += r'''
#define Z5_WALK_FIRST_RETURN 0x1030D8B5u
#define Z5_WALK_SECOND_RETURN 0x1030D8DFu
#define Z5_WALK_THIRD_RETURN 0x1030D8F9u
#define Z5_WALK_FOURTH_RETURN 0x1030D915u
static u32 words[2][0x100], paints[2], diagnostics[2];
static u32 disp_output(void) {return output;}
static u32 disp_widget(u32 o) {return 0x1000u+o*0x1000u;}
static u32 disp_pre_widget(u32 o) {return disp_widget(o)+0x100u;}
static u32 *word(u32 address) {
 u32 o=address>=0x2000u;u32 offset=address-disp_widget(o);
 assert(o<2&&offset<sizeof words[o]&&!(offset&3u));return words[o]+offset/4;
}
static void overlay_diag_count(u32 index) {assert(index==2u);diagnostics[output]++;}
static void overlay_diag_cost(u32 index,u32 cost) {(void)cost;assert(index==3u);}
static void overlay_diag_publish(void) {}
static u32 overlay_source_paint_impl(u32 object) {assert(object==disp_widget(output));paints[output]++;return 1u;}
''' + later + wrapper + r'''
static void verify_walks(void) {
 reason=0;metadata[8]=0x123u;metadata[26]=0x456u;
 for(output=0;output<2;output++) {
  *word(disp_widget(output)+0x4cu)=metadata[8];
  *word(disp_pre_widget(output)+0x4cu)=metadata[26];
  assert(overlay_source_paint(disp_widget(output),Z5_WALK_FIRST_RETURN)==1u);
  assert(paints[output]==0u&&*word(disp_pre_widget(output)+0x44u)==1u);
  overlay_source_paint(disp_widget(output),Z5_WALK_SECOND_RETURN);
  overlay_source_paint(disp_widget(output),Z5_WALK_THIRD_RETURN);
  overlay_source_paint(disp_widget(output),Z5_WALK_FOURTH_RETURN);
  assert(paints[output]==3u);
  overlay_source_paint(disp_widget(output),0xdeadbeefu);
  assert(paints[output]==3u&&states[output].source_reason==128u);
  assert(diagnostics[output]==5u);
 }
}
'''
# Exercise the actual page guards and LCD descriptor adapter. This catches the
# L19 regression: EVF activation must not depend on an LCD page object, UI-mode
# byte or LCD presentation descriptor. The common shooting/busy guards remain.
state = (HEADER / 'overlay_state.h').read_text()
state = state[state.index('static u32 overlay_mask_reason'):]
state = state.replace('overlay_reason(void)', 'adapter_reason(void)')
shooting = (HEADER / 'display.c').read_text()
shooting = shooting[shooting.index('static int disp_shooting'):shooting.index('Z5_IMAGE(custom_disp_image) static int disp_view')]
level = (HEADER / 'level.h').read_text()
prepare = level[level.index('static void level_prepare'):level.index('/* Publish only ranges')]
adapter = render[render.index('static u32 overlay_source_descriptor_matches'):render.index('Z5_IMAGE(custom_disp_image) static u32 overlay_source_refuse')]
CODE += r'''
static u32 snapshot_words[256], primary, selector, page_object, ui_mode, native_busy, flags=15;
static u8 *snapshot=(u8 *)snapshot_words;
#define Z5_OVERLAY_SNAPSHOT snapshot
#define Z5_CUSTOM_DISP_PRIMARY_DISPLAY primary
#define Z5_CUSTOM_DISP_SHOOTING_PAGE() page_object
#define Z5_PROBE_SELECTOR selector
#define Z5_CUSTOM_DISP_TARGET_DISP 0u
#define Z5_FULL_BUSY_WORD native_busy
#define Z5_OVERLAY_CONDITION(n) native_busy
#define Z5_OVERLAY_BUSY() native_busy
#define Z5_GUI_UI_MODE() ui_mode
static u32 disp_flags(void) {return flags;}
typedef struct {u32 surface;} level_state;
static level_state levels[2];
static u32 pass_matches=1;
static level_state *level_data(void) {return levels+output;}
static u32 disp_pass_matches(u32 o) {return o==output&&pass_matches;}
''' + state + shooting + adapter + prepare + r'''
static void verify_adapters(void) {
 selector=1;page_object=1;ui_mode=native_busy=0;
 for(output=0;output<2;output++) {
  primary=output?4:1;
  levels[output].surface=0x9999;states[output].drawing_image=0x12340000;
  states[output].ready=0;level_prepare();assert(!levels[output].surface);
  states[output].ready=1;level_prepare();assert(levels[output].surface==0x12340000);
  pass_matches=0;level_prepare();assert(!levels[output].surface);pass_matches=1;
  assert(!overlay_mask_reason()&&!adapter_reason()&&disp_shooting());
  page_object=0;ui_mode=3;
  assert((!overlay_mask_reason())==output);
  assert((!adapter_reason())==output);
  assert(!!disp_shooting()==output);
  overlay_update(9);assert(states[output].active==output);
  page_object=1;ui_mode=0;overlay_update(9);assert(states[output].active);
  native_busy=1;assert(overlay_mask_reason()==4&&!disp_shooting());
  overlay_update(9);assert(!states[output].active);native_busy=0;
  snapshot[0x18e]=1;assert(overlay_mask_reason()==2&&!disp_shooting());snapshot[0x18e]=0;
  selector=0;assert(overlay_mask_reason()==2&&!disp_shooting());selector=1;
  snapshot[0x40]=1;assert(overlay_mask_reason()==2&&!disp_shooting());snapshot[0x40]=0;
  flags=0;assert(!disp_shooting());flags=15;
 }
 u32 d[20]={0};d[0]=d[1]=d[0x48/4]=1;d[2]=(480u<<16)|720u;d[0x28/4]=0x12340000;
 assert(overlay_source_descriptor_matches(0,d,0x12340000,720,480));
 assert(!overlay_source_descriptor_matches(0,0,0x12340000,720,480));
 assert(!overlay_source_descriptor_matches(0,d,0x12350000,720,480));
 assert(!overlay_source_descriptor_matches(0,d,0x12340000,1024,768));
 const u32 fields[]={0,1,2,0x48/4,0x4c/4,0x38/4,0x28/4};
 for(u32 i=0;i<sizeof fields/sizeof fields[0];i++) {
  d[fields[i]]^=2;assert(!overlay_source_descriptor_matches(0,d,0x12340000,720,480));d[fields[i]]^=2;
 }
 assert(overlay_source_descriptor_matches(1,0,0x12340000,1280,960));
}
'''
with tempfile.TemporaryDirectory(prefix='z5-output-sync-') as scratch:
    root = Path(scratch)
    (root / 'check.c').write_text(CODE)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined,bounds', '-I', str(HEADER),
                    str(root / 'check.c'), '-o', str(root / 'check')], check=True)
    subprocess.run([str(root / 'check')], check=True, timeout=20)
print('PASS: checked-tail surface ownership; native EVF page/descriptor independence with common shooting guards; LCD/EVF supported sizes, sparse last-row/pixel/cache bounds, bank changes, identical 60Hz cadence, wrap, idle/reentry and 120ms constant')
