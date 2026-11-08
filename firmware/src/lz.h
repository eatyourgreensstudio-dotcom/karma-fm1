/* SPDX-License-Identifier: GPL-3.0-only */
/* LZ4 block format (no frame), for the saved songs (project.c, KARMA 3.0.5): a part is 3840 bytes, mostly empty
 * steps and free locks, ~300 bytes packed. Greedy, one hash table; tools/lz4blk.py reads the same blocks.
 * (Spec: min match 4, the last 5 bytes literals, the last match starting at least 12 bytes before the end.) */
#ifndef LZ_H
#define LZ_H
#ifdef LZ_HOST                                          /* (the firmware: libc.c) */
#include <stdint.h>
#include <string.h>
#endif

#define LZ_HBITS 10u
#define LZ_HT (1u << LZ_HBITS)                       /* the caller's hash table: uint16_t[LZ_HT] (2 KiB: the
                                                         * main loop's stack, while a song is saved) */

static uint32_t lz_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t lz_hash(uint32_t v) { return (v * 2654435761u) >> (32u - LZ_HBITS); }

static uint8_t *lz_more(uint8_t *d, const uint8_t *end, uint32_t n)   /* a length's 255 .. bytes; 0: no room */
{
    for (;;) {
        if (d >= end)
            return 0;
        if (n < 255u) {
            *d++ = (uint8_t)n;
            return d;
        }
        *d++ = 255u;
        n -= 255u;
    }
}

/* s (n bytes, n < 65535) packed into d (ht: LZ_HT entries, scratch); the packed size, 0 if it does not fit cap */
static uint32_t lz_pack(const uint8_t *s, uint32_t n, uint8_t *d0, uint32_t cap, uint16_t *lz_ht)
{
    uint8_t *d = d0;
    const uint8_t *end = d0 + cap;
    uint32_t i = 0, anchor = 0, L;
    memset(lz_ht, 0xFF, LZ_HT * sizeof *lz_ht);
    while (n >= 13u && i < n - 12u) {
        uint32_t h = lz_hash(lz_rd32(s + i)), c = lz_ht[h], m;
        lz_ht[h] = (uint16_t)i;
        if (c == 0xFFFFu || lz_rd32(s + c) != lz_rd32(s + i)) {
            i++;
            continue;
        }
        m = 4u;
        while (i + m < n - 5u && s[c + m] == s[i + m])
            m++;
        L = i - anchor;
        if (d >= end)
            return 0;
        *d++ = (uint8_t)((L < 15u ? L : 15u) << 4 | (m - 4u < 15u ? m - 4u : 15u));
        if (L >= 15u && !(d = lz_more(d, end, L - 15u)))
            return 0;
        if (L + 2u > (uint32_t)(end - d))
            return 0;
        memcpy(d, s + anchor, L);
        d += L;
        *d++ = (uint8_t)(i - c);
        *d++ = (uint8_t)((i - c) >> 8);
        if (m - 4u >= 15u && !(d = lz_more(d, end, m - 19u)))
            return 0;
        i += m;
        anchor = i;
    }
    L = n - anchor;                                     /* the last literals */
    if (d >= end)
        return 0;
    *d++ = (uint8_t)((L < 15u ? L : 15u) << 4);
    if (L >= 15u && !(d = lz_more(d, end, L - 15u)))
        return 0;
    if (L > (uint32_t)(end - d))
        return 0;
    memcpy(d, s + anchor, L);
    d += L;
    return (uint32_t)(d - d0);
}

/* a block back into d (cap bytes): the size, or -1 when it is not a valid block */
static int lz_unpack(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t cap)
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t t = s[i++], L = t >> 4, m, off, b;
        if (L == 15u)
            do {
                if (i >= n)
                    return -1;
                b = s[i++];
                L += b;
            } while (b == 255u);
        if (L > n - i || L > cap - o)
            return -1;
        memcpy(d + o, s + i, L);
        i += L;
        o += L;
        if (i >= n)
            break;                                      /* the last sequence: literals only */
        if (n - i < 2u)
            return -1;
        off = (uint32_t)s[i] | (uint32_t)s[i + 1u] << 8;
        i += 2u;
        if (!off || off > o)
            return -1;
        m = (t & 15u) + 4u;
        if ((t & 15u) == 15u)
            do {
                if (i >= n)
                    return -1;
                b = s[i++];
                m += b;
            } while (b == 255u);
        if (m > cap - o)
            return -1;
        while (m--) {
            d[o] = d[o - off];
            o++;
        }
    }
    return (int)o;
}
#endif
