static u32 install_custom_page(cm_t *c, u32 *title, u32 first_map)
{
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    custom_disp_state *s = z5_alloc(sizeof(*s), 0);
    u32 *rows = z5_alloc(4u * ROW_SIZE, 0), *maps = z5_alloc(16u, 0), *hdr = z5_alloc(28u, 0);
    if (!s || !rows || !maps || !hdr) return 90u;
    zero_words((u32 *)s, sizeof(*s) / 4u);
    c->custom_state = (u32)s;
    copy_u16(s->hz[0],16,(const u16 *)u"DISP2自定义"); copy_str(s->en[0],24,"DISP2 Custom");
    copy_u16(s->hz[1],16,(const u16 *)u"自定义显示"); copy_str(s->en[1],24,"Custom display");
    copy_u16(s->hz[2],16,(const u16 *)u"水平仪"); copy_str(s->en[2],24,"Level");
    copy_u16(s->hz[3],16,(const u16 *)u"直方图"); copy_str(s->en[3],24,"Histogram");
    copy_u16(s->hz[4],16,(const u16 *)u"详细信息"); copy_str(s->en[4],24,"Detailed info");
    u32 fns[4];
    fns[0]=img_addr(c,custom_enable_ok); fns[1]=img_addr(c,custom_level_ok);
    fns[2]=img_addr(c,custom_hist_ok); fns[3]=img_addr(c,custom_info_ok);
    const u32 *layout = 0;
    for (u32 k=0;k<8u;k++) if (((const u32 *)(th[1]+k*16u))[0]==4u) {layout=(const u32 *)(th[1]+k*16u);break;}
    if (!layout) return 91u;
    for (u32 k=0;k<4u;k++) {
        u32 *r=rows+k*ROW_WORDS;
        copy_words(r,(const u32 *)SETUP_ROWS+(STOCK_ROWS-1u)*ROW_WORDS,ROW_WORDS);
        r[0]=ROW_FIRST_SLOT+k; r[4]=ENTRY_FLAGS|(c->hans?F_UTF16:F_ASCII);r[6]=0x2FD1u+k;
        maps[k]=build_map(first_map,fns[k],STAY);if(!maps[k])return 92u;
    }
    hdr[0]=th[0];hdr[1]=(u32)layout;hdr[2]=(u32)title;hdr[3]=(u32)rows;
    hdr[4]=(u32)maps;hdr[5]=(4u<<16)|4u;hdr[6]=th[6];c->custom_hdr=(u32)hdr;
    custom_label(c);return 1u;
}
