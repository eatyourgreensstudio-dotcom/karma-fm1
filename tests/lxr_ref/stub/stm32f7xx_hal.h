/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/lxr_ref: what the LXR firmware's DSP files take from the STM32 HAL and CMSIS, on the Mac (arm64: its float to
 * integer conversions saturate as the Cortex-M7's do) */
#ifndef STM32F7XX_HAL_H
#define STM32F7XX_HAL_H
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef struct { int unused; } RNG_HandleTypeDef;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h);
static inline int32_t lxref_sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static inline uint32_t __QADD16(uint32_t a, uint32_t b)
{
    int32_t lo = lxref_sat16((int16_t)a + (int16_t)b), hi = lxref_sat16((int16_t)(a >> 16) + (int16_t)(b >> 16));
    return (uint32_t)(uint16_t)lo | (uint32_t)(uint16_t)hi << 16;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b)
{
    int32_t lo = lxref_sat16((int16_t)a - (int16_t)b), hi = lxref_sat16((int16_t)(a >> 16) - (int16_t)(b >> 16));
    return (uint32_t)(uint16_t)lo | (uint32_t)(uint16_t)hi << 16;
}
#define __SSAT(x, n) lxref_sat16((int32_t)(x))
static inline uint32_t __CLZ(uint32_t x) { return x ? (uint32_t)__builtin_clz(x) : 32u; }
typedef union { struct { uint32_t r : 28, V : 1, C : 1, Z : 1, N : 1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void) { return 0; }
#define I2S_AUDIOFREQ_44K 44100u
#define PROGMEM
#endif
