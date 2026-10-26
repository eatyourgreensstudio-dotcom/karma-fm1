/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Ariel Muchnik */
/* Host test of menu USB AUDIO = STEMS in src/usb.c (KARMA 3.0, after JIANT FM): the device and configuration a host
 * reads (6 channels, 44.1 kHz only, EP4 552 bytes, no console, bcdDevice + 0x40), then the ring at 3 words a frame:
 * renders of HALF_FRAMES at the I2S rate against packets at 1 kHz for 10 s, every frame of every channel delivered once
 * and in order; a host asking for 48 kHz stays at 44.1; nobody reading: no overrun into the console's memory.
 * The SIE is never touched. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef HALF_FRAMES
#error "-DHALF_FRAMES=n"
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 1
#define FELUCCA_UAC 1
#define CTL 32
static int32_t stem_buf[4][CTL], stem_fx[2][CTL];   /* (fx.c's) */
#define UAC_STEMS_SRC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"

static int fails;
static void check(const char *what, int ok) { printf("%-70s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

static void test_descriptors(void)
{
    const uint8_t *c, *dv;
    uint16_t cl, dl;
    uint32_t off, it6 = 0, fmt6 = 0, ep = 0, nif = 0, iad = 0;
    usb_stems = 1;
    get_desc(0x0100, &dv, &dl);
    get_desc(0x0200, &c, &cl);
    check("STEMS: device class 0, bcdDevice + 0x40 (a device of its own)", dv[4] == 0 && (dv[12] & 0x40u) && dv[13] == 3);
    check("STEMS: wTotalLength = the bytes, 3 interfaces", le16(c + 2) == cl && c[4] == 3);
    for (off = 0; off < cl; off += c[off]) {
        const uint8_t *d = c + off;
        if (d[0] < 2)
            break;
        if (d[1] == 0x0B)
            iad++;
        if (d[1] == 4 && d[3] == 0)
            nif++;
        if (d[1] == 0x24 && d[2] == 2 && d[0] == 12)
            it6 = d[7] == 6;
        if (d[1] == 0x24 && d[2] == 2 && d[0] == 11 && d[3] == 1)
            fmt6 = d[4] == 6 && d[5] == 2 && d[6] == 16 && d[7] == 1 && (d[8] | d[9] << 8 | d[10] << 16) == 44100;
        if (d[1] == 5 && d[2] == 0x84)
            ep = d[3] == 0x05 && le16(d + 4) == 552u;
    }
    check("STEMS: input terminal 6 channels", it6);
    check("STEMS: type I format 6 ch, 16 bit, 44100 Hz only", fmt6);
    check("STEMS: EP 0x84 isochronous async, 552 bytes (46 frames x 12)", ep);
    check("STEMS: no IAD, no console (3 interfaces)", iad == 0 && nif == 3);
    uac_rate_set(48000);
    check("STEMS: a host asking 48 kHz stays at 44.1", uac.r48 == 0);
}

#define FS_DEV 44117.6
static uint32_t prod_n, cons_n, cons_bad, cons_rep, npk, frames;
static uint16_t last;
static int16_t ch(uint32_t k, uint32_t n)           /* channel k of frame n */
{
    switch (k) {
    case 0: return (int16_t)n;
    case 1: return (int16_t)~n;
    case 2: return (int16_t)(n * 3u);
    case 3: return (int16_t)(n + 1000u);
    case 4: return (int16_t)(n ^ 0x5555u);
    default: return (int16_t)(0u - n);
    }
}
static void render_block(void)
{
    uint32_t i;
    for (i = 0; i < CTL; i++, prod_n++) {
        stem_buf[0][i] = ch(0, prod_n), stem_buf[1][i] = ch(1, prod_n);
        stem_buf[2][i] = ch(2, prod_n), stem_buf[3][i] = ch(3, prod_n);
        stem_fx[0][i] = ch(4, prod_n), stem_fx[1][i] = ch(5, prod_n);
    }
    uac_tap6(CTL);
}
static void take_packet(void)
{
    uint32_t d[UA_TXW], n = uac_packet(d), i, k;
    npk++;
    frames += n;
    for (i = 0; i < n; i++) {
        uint16_t c0 = (uint16_t)d[i * 3u];
        int ok = 1;
        if (!uac.go || (d[i * 3u] | d[i * 3u + 1u] | d[i * 3u + 2u]) == 0u)
            continue;                                   /* (primed silence) */
        for (k = 0; k < 6u; k++)
            ok &= (int16_t)(uint16_t)(d[i * 3u + k / 2u] >> (16u * (k & 1u))) == ch(k, c0);
        if (!ok) {
            cons_bad++;
            continue;
        }
        if (cons_n && c0 == last)
            cons_rep++;
        else if (cons_n && c0 != (uint16_t)(last + 1u))
            cons_bad++;
        last = c0;
        cons_n++;
    }
}
static uint64_t now_ns, next_half, next_pkt, next_block, rend_len;
static uint32_t blocks_left, lcg = 777;
static void run(uint32_t ms, int consume)
{
    uint64_t end = now_ns + (uint64_t)ms * 1000000u;
    while (now_ns < end) {
        uint64_t t = end;
        if (next_half < t) t = next_half;
        if (blocks_left && next_block < t) t = next_block;
        if (next_pkt < t) t = next_pkt;
        now_ns = t;
        if (now_ns >= end)
            break;
        if (now_ns == next_half) {
            next_half += (uint64_t)((double)HALF_FRAMES / FS_DEV * 1e9);
            lcg = lcg * 1103515245u + 12345u;
            rend_len = (uint64_t)(HALF_FRAMES * 1e9 / FS_DEV * (0.07 + 0.78 * ((lcg >> 8) % 1000u) / 1000.0));
            uac_render_start();
            blocks_left = HALF_FRAMES / CTL;
            next_block = now_ns + rend_len / blocks_left;
        } else if (blocks_left && now_ns == next_block) {
            render_block();
            blocks_left--;
            next_block += rend_len / (HALF_FRAMES / CTL);
        } else {
            lcg = lcg * 1103515245u + 12345u;
            next_pkt = (now_ns / 1000000u + 1u) * 1000000u + (lcg >> 8) % 500000u;
            if (consume)
                take_packet();
        }
    }
}
static void test_ring(void)
{
    char s[120];
    uint32_t base, i, guard_ok = 1;
    static uint32_t after[UA_MEMW];
    usb.config = 1;
    memset(&uac, 0, sizeof uac);
    uac_stream(1);
    uac.flowing = 1;
    next_half = 300000;
    next_pkt = 1000000;
    run(2000, 1);
    base = uac.underruns + uac.overruns;
    npk = frames = 0;
    run(10000, 1);
    snprintf(s, sizeof s, "10 s, random render times: mean packet %.4f frames", (double)frames / npk);
    check(s, (double)frames / npk > 44.05 && (double)frames / npk < 44.2);
    check("  every frame of the 6 channels once, in order; no underrun / overrun",
          cons_bad == 0 && cons_rep == 0 && uac.underruns + uac.overruns == base && cons_n > 400000u);
    snprintf(s, sizeof s, "  fill at render start %u..%u (band %u..%u, ring %u frames)", uac.fill_lo, uac.fill_hi, UA_LO, UA_HI, UA_NFS);
    check(s, uac.fill_lo >= 46u && uac.fill_hi + HALF_FRAMES <= UA_NFS);
    memcpy(after, ua_ring, sizeof after);
    run(300, 0);                                        /* nobody reads: the ring never overfills */
    check("nobody reads: blocks dropped, the ring stays within its frames", uac.overruns > 0 && ua_w - ua_r <= UA_NFS);
    for (i = UA_NFS * UA_WS; i < UA_MEMW; i++)
        guard_ok &= ua_ring[i] == after[i];
    check("  nothing written past the stems ring", guard_ok);
}
int main(void)
{
    test_descriptors();
    test_ring();
    printf(fails ? "uac stems: FAILED\n" : "uac stems: all ok\n");
    return fails != 0;
}
