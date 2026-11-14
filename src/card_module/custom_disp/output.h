/* Shared timing and supported native output adapters. */
#define CUSTOM_DISP_HISTOGRAM_MS 120u
#define CUSTOM_DISP_MAX_HEIGHT 960u

Z5_IMAGE(custom_disp_image) static u32 disp_size_supported(u32 output, u32 width, u32 height)
{
    if (!output) return width == 720u && height == 480u;
    return (width == 720u && height == 480u) ||
           (width == 1024u && (height == 682u || height == 768u)) ||
           (width == 1280u && height == 960u);
}

Z5_IMAGE(custom_disp_image) static u32 disp_histogram_due(u32 now, u32 last, u32 pending)
{
    return !pending && now - last >= CUSTOM_DISP_HISTOGRAM_MS;
}
