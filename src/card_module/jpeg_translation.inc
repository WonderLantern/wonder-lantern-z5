IMG_FN static u32 jpeg_translate(u32 va, u32 write)
{
    u32 saved, cpsr, par;
    /* PAR is per core. Restore it before another task can run. */
    __asm__ volatile ("mrs %0, cpsr\ncpsid if" : "=r"(cpsr) :: "memory", "cc");
    __asm__ volatile ("mrc p15, 0, %0, c7, c4, 0" : "=r"(saved));
    if (write) __asm__ volatile ("mcr p15, 0, %0, c7, c8, 1" :: "r"(va) : "memory");
    else __asm__ volatile ("mcr p15, 0, %0, c7, c8, 0" :: "r"(va) : "memory");
    __asm__ volatile ("isb\nmrc p15, 0, %0, c7, c4, 0" : "=r"(par) :: "memory");
    __asm__ volatile ("mcr p15, 0, %0, c7, c4, 0\nisb\nmsr cpsr_c, %1"
                      :: "r"(saved), "r"(cpsr) : "memory", "cc");
    return par;
}
