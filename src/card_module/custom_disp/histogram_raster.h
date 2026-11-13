/* Shared LCD/EVF CPU raster. Caller supplies cached bins, scale and pixel adapters. */
#define HIST_WIDTH 192u
#define HIST_HEIGHT 96u
    u32 background = HIST_COLOR(756u), grid = HIST_COLOR(730u);
    u32 border = HIST_COLOR(729u), white = HIST_COLOR(1u);
    HIST_FILL(0, 0, HIST_WIDTH, HIST_HEIGHT, background);
    for (u32 i = 1u; i < 4u; i++)
        HIST_FILL(i * (HIST_WIDTH / 4), 0, 2, HIST_HEIGHT, grid);
    u32 at = 0u, ninth = 0u;
    for (u32 column = 0u; column < 100u; column++) {
        u32 a = HIST_BIN(at++), b = HIST_BIN(at++);
        if (b > a) a = b;
        if (!(column & 1u) || !ninth) { b = HIST_BIN(at++); if (b > a) a = b; }
        if (++ninth == 9u) ninth = 0u;
        u32 height = HIST_DIV(HIST_HEIGHT * a, scale);
        if (height > HIST_HEIGHT) height = HIST_HEIGHT;
        u32 column_left = (column * HIST_WIDTH) / 100u, column_right = ((column + 1u) * HIST_WIDTH) / 100u;
        if (height) HIST_FILL(column_left, HIST_HEIGHT - height,
                                     column_right - column_left, height, white);
    }
    HIST_FILL(0, 0, HIST_WIDTH, 1, border);
    HIST_FILL(0, HIST_HEIGHT - 1, HIST_WIDTH, 1, border);
    HIST_FILL(0, 0, 1, HIST_HEIGHT, border);
    HIST_FILL(HIST_WIDTH - 1, 0, 1, HIST_HEIGHT, border);
#undef HIST_WIDTH
#undef HIST_HEIGHT
