#ifndef Z5_A44_STRENGTH_INC
#define Z5_A44_STRENGTH_INC

#define A44_STRENGTHS 6u
#define A44_DEFAULT_STRENGTH 3u

static inline __attribute__((always_inline)) u32 a44_strength(u32 value)
{
    return value && value <= A44_STRENGTHS ? value : A44_DEFAULT_STRENGTH;
}

#endif
