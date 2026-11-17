static u32 install_custom_page(cm_t *c, u32 *title, u32 first_map)
{
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    custom_disp_state *s = z5_alloc(sizeof(*s), 0);
    u32 *rows = z5_alloc(11u * ROW_SIZE, 0), *maps = z5_alloc(44u, 0), *hdr = z5_alloc(84u, 0);
    if (!s || !rows || !maps || !hdr) return 90u;
    zero_words((u32 *)s, sizeof(*s) / 4u);
    c->custom_state = (u32)s;
    copy_u16(s->hz[0],16,(const u16 *)u"自定义DISP"); copy_str(s->en[0],24,"Custom DISP");
    copy_u16(s->hz[1],16,(const u16 *)u"总开关"); copy_str(s->en[1],24,"Enabled");
    copy_u16(s->hz[2],16,(const u16 *)u"水平线"); copy_str(s->en[2],24,"Level");
    copy_u16(s->hz[3],16,(const u16 *)u"直方图"); copy_str(s->en[3],24,"Histogram");
    copy_u16(s->hz[4],16,(const u16 *)u"详细信息"); copy_str(s->en[4],24,"Detailed info");
    copy_u16(s->hz[5],16,(const u16 *)u"关闭"); copy_str(s->en[5],24,"Off");
    copy_u16(s->hz[6],16,(const u16 *)u"原版"); copy_str(s->en[6],24,"Native");
    copy_u16(s->hz[7],16,(const u16 *)u"TypeA"); copy_str(s->en[7],24,"TypeA");
    copy_u16(s->hz[8],16,(const u16 *)u"DISP0 详细信息"); copy_str(s->en[8],24,"DISP0 Detailed info");
    copy_u16(s->hz[9],16,(const u16 *)u"DISP1 简单信息"); copy_str(s->en[9],24,"DISP1 Simple info");
    copy_u16(s->hz[10],16,(const u16 *)u"DISP2 直方图"); copy_str(s->en[10],24,"DISP2 Histogram");
    copy_u16(s->hz[11],16,(const u16 *)u"DISP3 水平线"); copy_str(s->en[11],24,"DISP3 Level");
    s->key_entry = img_addr(c, custom_key_dispatch);
    u32 fns[11];
    fns[0]=img_addr(c,custom_enable_ok);
    fns[1]=img_addr(c,custom_page0_ok); fns[2]=img_addr(c,custom_page1_ok);
    fns[3]=img_addr(c,custom_page2_ok); fns[4]=img_addr(c,custom_page3_ok);
    fns[5]=img_addr(c,custom_level_ok); fns[6]=img_addr(c,custom_hist_ok);
    fns[7]=img_addr(c,custom_info_ok); fns[8]=img_addr(c,custom_level_off);
    fns[9]=img_addr(c,custom_level_native); fns[10]=img_addr(c,custom_level_typea);
    const u32 *root_layout = 0, *three_layout = 0;
    for (u32 k=0;k<8u;k++) {
        const u32 *candidate = (const u32 *)(th[1]+k*16u);
        if (candidate[0] == 5u) root_layout = candidate;
        if (candidate[0] == 3u) three_layout = candidate;
    }
    if (!root_layout || !three_layout) return 91u;
    for (u32 k=0;k<11u;k++) {
        u32 *r=rows+k*ROW_WORDS;
        copy_words(r,(const u32 *)SETUP_ROWS+(STOCK_ROWS-1u)*ROW_WORDS,ROW_WORDS);
        r[0]=CUSTOM_MENU_FIRST_SLOT+(k<5u?k:k<8u?k-5u:k-8u);
        r[4]=ENTRY_FLAGS|(c->hans?F_UTF16:F_ASCII);r[6]=0x2FD1u+k;
        maps[k]=build_map(first_map,fns[k],STAY);if(!maps[k])return 92u;
        if (k >= 5u) for (u32 j=0u; j<MAP_MAX; j++) {
            u32 *event=(u32 *)maps[k]+j*4u;
            if (event[3]==BACK) {event[1]=img_addr(c,k<8u?custom_entry_ok:custom_options_ok);event[2]=0u;event[3]=STAY;}
            if (event[0]==EV_END) break;
        }
    }
    hdr[0]=th[0];hdr[1]=(u32)root_layout;hdr[2]=(u32)title;hdr[3]=(u32)rows;
    hdr[4]=(u32)maps;hdr[5]=(5u<<16)|5u;hdr[6]=th[6];c->custom_hdr=(u32)hdr;
    hdr += 7u;
    hdr[0]=th[0];hdr[1]=(u32)three_layout;hdr[2]=(u32)title;hdr[3]=(u32)(rows+5u*ROW_WORDS);
    hdr[4]=(u32)(maps+5u);hdr[5]=(3u<<16)|3u;hdr[6]=th[6];s->page_hdr=(u32)hdr;
    hdr += 7u;
    hdr[0]=th[0];hdr[1]=(u32)three_layout;hdr[2]=(u32)title;hdr[3]=(u32)(rows+8u*ROW_WORDS);
    hdr[4]=(u32)(maps+8u);hdr[5]=(3u<<16)|3u;hdr[6]=th[6];s->level_hdr=(u32)hdr;
    custom_label(c);return 1u;
}
