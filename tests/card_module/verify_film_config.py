#!/usr/bin/env python3
"""Host compilation of the real MENU.DAT save/load with FILM (A52) on, plus an A39 build reading the result.

FILM keeps style, strength and NEF write in word 1 bits 1-7 (bit 7 = present) so the 20-byte A39 file format is
unchanged. Checks: every FILM combination round-trips with the shutter-sound switch; files written by A44-A51
(tag 0xD215, FILM in word 2 bits 8-13) migrate; A39-format files without FILM give the defaults; burst speed-up
is off after every load (BURST_BOOT_OFF); a module built without FILM still accepts the FILM-written file.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
base = (ROOT / 'tests/card_module/verify_config.py').read_text(encoding='utf-8')
harness = base[base.index("code=("):base.index("int main(void){")]
harness = harness[len("code="):] + "'''"   # close the r'''...''' tail that held main()
source = (ROOT / 'src/card_module/main.c').read_text(encoding='utf-8')
strength = (ROOT / 'src/card_module/a44_strength.inc').read_text(encoding='utf-8')


def function(name):
    start = re.search(r'IMG_FN static (?:void|u32) ' + name + r'\([^;{]+\)\n\{', source).start()
    end = source.index('{', start)
    depth, cursor = 1, end + 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


def program(film):
    # Evaluate the harness expression from verify_config.py with this module's helpers.
    text = eval(harness, {'ROOT': ROOT, 'function': function, 'Path': Path})
    cm = ('typedef struct {u32 edited,save_err,stage,saves,on,type,vol,grid,zebra,vui,focal_on,bat_on,touch_on,'
          'custom_flags,custom_control,cfg_state,burst_mech,burst_sil;')
    assert cm in text
    if film:
        text = text.replace(cm, cm + ' uintptr_t dg_buf;')
        defs = ('#define JPEG_A44\n#define LIGHTROOM_GRAIN\n#define BURST_BOOT_OFF\n' + strength +
                '\n#define A44_LOG(c) ((volatile u32 *)((c)->dg_buf + 0x33000u))\n#define A44_MODE 9u\n'
                '#define A44_LEVEL 11u\n#define A47_LIGHTROOM 14u\n')
        text = text.replace('#define BULB_MENU\n', '#define BULB_MENU\n' + defs)
        assert '#define JPEG_A44' in text
    return text


FILM_MAIN = r'''
static u32 diag[0x40000 / 4];
#define LOG(i) (diag[0x33000 / 4 + (i)])
static void reset(cm_t *c){memset(c,0,sizeof *c);c->dg_buf=(uintptr_t)diag;memset(diag,0,sizeof diag);}
int main(void){
 cm_t c;reset(&c);
 /* A51's own MENU.DAT (the author's card): FUJI400, 6, NEF write on, A19 timer word, burst bits clear. */
 u32 a51[]={CFG_MAGIC,0x0D050101u,0xD215393Fu,0xB701000Au};
 memcpy(file,a51,sizeof a51);length=sizeof a51;
 assert(!cfg_load(&c));
 assert(LOG(9)==1&&LOG(11)==6&&LOG(14)==1&&c.on==1&&c.type==1&&c.vol==5);
 assert(!c.burst_mech&&!c.burst_sil);
 /* Saved as the A39 20-byte format, FILM in word 1. */
 save_cfg(&c);assert(length==20);
 u32 *w=(u32 *)file;
 assert((w[2]&0xFF000000u)==CUSTOM_CFG_TAG&&(w[1]&0xFFu)==(0x80u|1u|(1u<<1)|(6u<<3)|0x40u));
 /* Every combination with the sound switch round-trips; burst is always off after a load. */
 for(u32 on=0;on<2;on++)for(u32 mode=0;mode<3;mode++)for(u32 level=1;level<=6;level++)for(u32 nef=0;nef<2;nef++){
  reset(&c);c.on=on;c.type=3;c.vol=2;c.burst_mech=1;c.burst_sil=1;LOG(9)=mode;LOG(11)=level;LOG(14)=nef;
  save_cfg(&c);assert(length==20);
  cm_t next;reset(&next);LOG(9)=7;LOG(11)=0;LOG(14)=9;
  assert(!cfg_load(&next));
  assert(next.on==on&&next.type==3&&next.vol==2&&LOG(9)==mode&&LOG(11)==level&&LOG(14)==nef);
  assert(!next.burst_mech&&!next.burst_sil);
 }
 /* A39 file without FILM: defaults (off, strength 3, NEF write off); sound switch kept. */
 w[1]&=~0xFEu;
 reset(&c);assert(!cfg_load(&c));assert(LOG(9)==0&&LOG(11)==3&&LOG(14)==0);
 /* Older A44-A51 bits never override the word-1 copy. */
 u32 both[]={CFG_MAGIC,0x0D050101u|0x80u|(2u<<1)|(2u<<3),0xD215393Fu,0xB701000Au};
 memcpy(file,both,sizeof both);length=sizeof both;reset(&c);
 assert(!cfg_load(&c)&&LOG(9)==2&&LOG(11)==2&&LOG(14)==0&&c.on==1);
 /* No diagnostic buffer: FILM untouched, the rest still loads. */
 memcpy(file,a51,sizeof a51);length=sizeof a51;reset(&c);c.dg_buf=0;
 assert(!cfg_load(&c)&&c.on==1);
 /* Write the file the camera would keep, for the A39 reader below. */
 reset(&c);c.on=0;LOG(9)=2;LOG(11)=4;LOG(14)=1;save_cfg(&c);
 FILE *out=fopen("film.dat","wb");fwrite(file,1,length,out);fclose(out);
 return 0;
}
'''

A39_MAIN = r'''
#include <stdio.h>
int main(void){
 FILE *in=fopen("film.dat","rb");length=(u32)fread(file,1,sizeof file,in);fclose(in);
 cm_t c={0};assert(length==20&&!cfg_load(&c));
 /* The A39 module accepts the file; its sound switch falls back to on (bit 0 is 0 but the byte is not 0/1). */
 assert(c.on==1&&c.custom_flags==CUSTOM_NORMALIZE(CUSTOM_ENABLE_MASK));
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='z5-film-config-') as t:
    p = Path(t)
    film = program(True).replace('#include <assert.h>', '#include <assert.h>\n#include <stdio.h>')
    (p / 'film.c').write_text(film + FILM_MAIN)
    (p / 'a39.c').write_text(program(False).replace('#include <assert.h>', '#include <assert.h>\n#include <stdio.h>')
                             + A39_MAIN)
    for name in ('film', 'a39'):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(p / f'{name}.c'), '-o', str(p / name)],
                       check=True)
    subprocess.run([str(p / 'film')], check=True, cwd=p)
    subprocess.run([str(p / 'a39')], check=True, cwd=p)
print('PASS: FILM settings in MENU.DAT word 1: A51 file migration, 72 combinations with the sound switch, '
      'A39-format files without FILM, word-1 precedence, no diagnostic buffer, burst off after every load; '
      'an A39 build still accepts the FILM-written 20-byte file')
