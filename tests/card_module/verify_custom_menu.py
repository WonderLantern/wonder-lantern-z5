#!/usr/bin/env python3
"""Compile actual nested menu installation/navigation with native UI mocked."""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/card_module/custom_disp_runtime.inc').read_text()
state=source[source.index('typedef struct'):source.index('} custom_disp_state;')+len('} custom_disp_state;')]
menus=source[source.index('IMG_FN static void custom_label'):]
install=(ROOT/'src/card_module/custom_disp_install.inc').read_text()
main=(ROOT/'src/card_module/main.c').read_text()
bulb = (ROOT/'src/card_module/burst_speed.inc').read_text()
def slot(source, name):
 return int(re.search(r'^#define '+name+r'\s+(\d+)u',source,re.M)[1])
bulb_slots = [slot(bulb,'BT_SLOT_ENTRY'), *range(slot(bulb,'BT_SLOT_ROW'), slot(bulb,'BT_SLOT_ROW') + 4)]
# Use production slot numbers: synthetic slots previously hid the fifth-row collision.
slots='\n'.join(re.search(r'^#define '+name+r'\s+.*$',main,re.M)[0] for name in ('ROW_FIRST_SLOT','ENTRY_SLOT','CUSTOM_MENU_FIRST_SLOT'))
code=(ROOT/'src/card_module/custom_disp/settings.h').read_text()+'\n'+slots+'\n#define BULB_SLOTS '+','.join(map(str,bulb_slots))+r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
/* Pointer-sized words let the actual native menu tables run on the host.
 * Production ARM sizes/capacity are checked separately by the cross-build. */
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define IMG_FN
#define MAP_MAX 4u
#define EV_END 0xffu
#define BACK 2u
#define STAY 3u
#define ROW_WORDS 7u
#define ROW_SIZE 28u
#define STOCK_ROWS 1u
#define ENTRY_FLAGS 1u
#define F_UTF16 2u
#define F_ASCII 4u
#define OWN_INDEX 0u
#define SLOT_TITLE_V 1u
#define HANS_STYLE 114u
#define HANS_FONT 2u
static u32 template[7],layouts[32],stock_rows[7],page0[11],style;
#define TEMPLATE_HDR ((u32)template)
#define SETUP_ROWS ((u32)stock_rows)
#define PAGE0 ((u32)page0)
#define RUNTIME_STYLE ((u32)&style)
static u16 labels[300][24],title[24];static char ascii_labels[300][40];static u32 saves,applies;
typedef struct {u32 custom_flags,custom_control,custom_hdr,custom_state,hans,title_slot;u16 hz_on[2],hz_off[2];char en_on[4],en_off[4];}cm_t;
static cm_t c;
static cm_t *cm_get(void){return &c;}
static void *z5_alloc(u32 n,u32 mode){(void)mode;return calloc(1,n*2);}
static void zero_words(u32 *p,u32 n){memset(p,0,n*sizeof(*p));}
static void copy_words(u32 *d,const u32 *s,u32 n){memcpy(d,s,n*sizeof(*d));}
static void copy_u16(u16 *d,u32 n,const u16 *s){for(u32 i=0;i<n;i++){d[i]=s[i];if(!s[i])break;}}
static void copy_str(char *d,u32 n,const char *s){strncpy(d,s,n);}
static u32 img_addr(cm_t *owner,const void *fn){(void)owner;return (u32)fn;}
static u32 build_map(u32 first,u32 fn,u32 how){(void)first;(void)how;u32 *p=calloc(MAP_MAX*4,sizeof(u32));p[0]=1;p[1]=fn;p[4]=2;p[7]=BACK;p[8]=EV_END;return (u32)p;}
static void sub_title(cm_t *owner,const u16 *hz,const char *en){(void)owner;(void)en;copy_u16(title,24,hz);}
static void utf(const u16 *text,u32 slot,u32 unused){(void)unused;assert(slot<300);copy_u16(labels[slot],24,text);}
static void ascii(const char *text,u32 slot,u32 unused){(void)unused;assert(slot<300);copy_str(ascii_labels[slot],40,text);}
static void open_page(u32 kind,u32 index){assert(kind==2&&index==0);}
#define FW(type,name) name
#define UTF16_SET utf
#define ASCII_SET ascii
#define OPEN_PAGE open_page
static void custom_apply(cm_t *owner){(void)owner;applies++;}
static void save_cfg(cm_t *owner){(void)owner;saves++;}
static u32 config_ready=1;
static u32 cfg_ready(cm_t *owner){(void)owner;return config_ready;}
static u32 custom_key_dispatch(u32 action){return action;}
'''+state+menus+install+r'''
static u32 *active(void){return (u32 *)page0[10];}
static void run(u32 row,u32 back){
 u32 *h=active(),*maps=(u32 *)h[4];u32 *map=(u32 *)maps[row];
 u32 (*fn)(u32,u32,u32,u32)=(void *)map[back?5:1];assert(fn);fn(0,0,0,0);
}
int main(void){
 template[1]=(u32)layouts;layouts[0]=5;layouts[4]=3;
 c.hans=1;c.hz_on[0]=1;c.hz_off[0]=2;c.custom_flags=15;c.custom_control=1;
 assert(install_custom_page(&c,template,0)==1);
 custom_disp_state *s=(void *)c.custom_state;
 utf((const u16 *)u"扩展功能",ENTRY_SLOT,0);
 custom_entry_ok(0,0,0,0);assert((u32)active()==c.custom_hdr);
 /* Half-press bypasses Back: the setup row must already be intact. */
 assert(!memcmp(labels[ENTRY_SLOT],u"扩展功能",sizeof(u"扩展功能")));
 for(u32 p=0;p<4;p++){
  assert(!memcmp(labels[CUSTOM_MENU_FIRST_SLOT+p+1],s->hz[8+p],16));
 }
 run(0,0);assert(!c.custom_control);run(0,0);assert(c.custom_control==1);
 for(u32 p=0;p<4;p++){
  custom_entry_ok(0,0,0,0);run(p+1,0);assert(s->editing==p&&(u32)active()==s->page_hdr);
  assert(!memcmp(title,s->hz[8+p],16));
  run(0,0);assert((u32)active()==s->level_hdr);run(1,1);assert((u32)active()==s->page_hdr);
  for(u32 mode=0;mode<3;mode++){
   run(0,0);run(mode,0);assert((u32)active()==s->page_hdr);
   u32 expected=mode==0?0:mode==1?16:2;
   assert((CUSTOM_PAGE_FLAGS(c.custom_flags,p)&18u)==expected);
  }
  run(1,0);run(2,0);run(0,1);assert((u32)active()==c.custom_hdr);
 }
 assert(saves==22&&applies==22);
 /* Failed settings recovery blocks mutations before RAM or disk changes. */
 config_ready=0;u32 previous=c.custom_flags;
 custom_entry_ok(0,0,0,0);run(0,0);run(4,0);
 run(1,0);run(2,0);run(0,0);run(1,0);
 assert(c.custom_flags==previous&&saves==22&&applies==22);
 /* Root and nested menus must preserve persistent setup/hub/burst/Bulb labels,
  * even when native half-press exits without running any Back action. */
 for(u32 hans=0;hans<2;hans++){
  c.hans=hans;
  const u32 protected_slots[]={ENTRY_SLOT,251,252,253,254,255,256,BULB_SLOTS};
  for(u32 k=0;k<sizeof(protected_slots)/sizeof(*protected_slots);k++){
   utf((const u16 *)u"Persistent",protected_slots[k],0);
   ascii("Persistent",protected_slots[k],0);
  }
  for(u32 p=0;p<4;p++){
   custom_entry_ok(0,0,0,0);
   assert(!strcmp(ascii_labels[ENTRY_SLOT],"Persistent"));
   run(p+1,0);run(0,0);
   /* Simulate abandoning the page rather than executing its Back map. */
   page0[10]=0;custom_entry_ok(0,0,0,0);
   u32 *root_rows=(u32 *)active()[3];
   for(u32 row=0;row<5;row++)assert(root_rows[row*ROW_WORDS]==CUSTOM_MENU_FIRST_SLOT+row);
   for(u32 k=0;k<sizeof(protected_slots)/sizeof(*protected_slots);k++){
    assert(!memcmp(labels[protected_slots[k]],u"Persistent",sizeof(u"Persistent")));
    assert(!strcmp(ascii_labels[protected_slots[k]],"Persistent"));
   }
   if(hans)assert(!memcmp(labels[root_rows[4*ROW_WORDS]],s->hz[11],sizeof(u"DISP3 水平线")));
   else assert(!strcmp(ascii_labels[root_rows[4*ROW_WORDS]],"DISP3 Level"));
  }
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='z5-custom-menu-') as t:
 p=Path(t);(p/'check.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined,bounds',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: actual five-row root with one master, four three-row page menus, official DISP labels, three level choices, nested Back, production text slots, Chinese/English half-press exits without Back, preserved setup/hub/burst/Bulb labels and unread-settings mutation guards')
