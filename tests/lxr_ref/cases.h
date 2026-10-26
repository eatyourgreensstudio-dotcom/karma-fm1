/* SPDX-License-Identifier: GPL-3.0-only */
/* The sounds both renders play (tests/lxr_ref/ref_lxr.c: the LXR firmware's own voices; tests/lxr_test.c: KARMA's
 * port, firmware/src/lxr_dsp.c): the LXR's parameter values (its CC values, 0..127), in lx_par_t's order. */
#define LXC_SECONDS 1                    /* each render: a second, from the hit (note 60, its velocity) */
typedef struct {
    const char *name;
    uint8_t vel;
    uint8_t kind, wave, coarse, fine, mwave, mcoarse, mamt, m2wave, m2coarse, m2amt, mix, noisef, atk, dec, slope,
        repeat, pamt, pdec, pslope, twave, tvol, tfreq, ftype, fcut, fres, fdrive, dist, decim, vol, pwm;
} lxc_t;
/* kind: 0 DRUM, 1 SNARE, 2 CYM, 3 HAT. wave: 0 SIN 1 TRI 2 SAW 3 REC 4 NOISE 5 PWM. twave: 0 snap, 1 offset,
 * 2.. the samples (2 Clk, 3 Ck2, 4 Tik, 5 Kik, 6 Rim, 7 Drp, 8 Hat, 9 Clp, 10 Kk2, 11 Snr, 12 Tom, 13 Sp2).
 * ftype: 1 LP 2 HP 3 BP 4 UBP 5 NOTCH 6 PEAK 7 LP2 8 OFF */
static const lxc_t LXC[] = {
    /* name             vel  k  w  crs fin mw mcr mam 2w 2cr 2am mix nf atk dec slp rep pam pde psl tw tvl tfr ft cut res drv dst dec vol pwm */
    {"drum sine kick",  110, 0, 0, 31, 64, 0, 43, 0, 0, 0, 0, 0, 0, 0, 34, 40, 0, 40, 32, 34, 5, 70, 25, 1, 110, 10, 40, 10, 127, 120, 64},
    {"drum sine fm",    127, 0, 0, 40, 64, 0, 52, 60, 0, 0, 0, 0, 0, 0, 30, 30, 0, 30, 28, 44, 2, 80, 30, 1, 112, 20, 50, 0, 127, 110, 64},
    {"drum snap lp2",   100, 0, 0, 33, 64, 0, 45, 20, 0, 0, 0, 0, 0, 0, 22, 30, 0, 64, 18, 14, 0, 60, 40, 7, 100, 40, 60, 50, 90, 124, 64},
    {"drum tri fm sqr", 120, 0, 1, 45, 70, 3, 64, 40, 0, 0, 0, 0, 0, 0, 20, 50, 0, 26, 24, 40, 1, 40, 25, 2, 60, 30, 50, 20, 127, 116, 64},
    {"drum saw mix",     90, 0, 2, 36, 64, 3, 43, 50, 0, 0, 0, 1, 0, 30, 40, 70, 0, 20, 30, 64, 4, 60, 30, 3, 90, 50, 40, 0, 127, 110, 64},
    {"drum rec offset", 127, 0, 3, 50, 50, 0, 62, 30, 0, 0, 0, 0, 0, 0, 16, 30, 0, 10, 20, 64, 1, 90, 25, 5, 100, 30, 40, 30, 127, 104, 64},
    {"drum pwm peak",   110, 0, 5, 48, 64, 0, 55, 10, 0, 0, 0, 0, 0, 0, 24, 40, 0, 16, 20, 64, 12, 50, 30, 6, 96, 60, 50, 0, 127, 110, 80},
    {"drum noise ubp",  110, 0, 4, 100, 64, 0, 60, 0, 0, 0, 0, 0, 0, 0, 12, 40, 0, 0, 0, 64, 6, 80, 30, 4, 104, 40, 40, 0, 127, 116, 64},
    {"snare tri",       110, 1, 1, 52, 64, 0, 0, 0, 0, 0, 0, 85, 120, 0, 18, 30, 0, 22, 22, 44, 11, 60, 25, 2, 100, 20, 40, 10, 127, 110, 64},
    {"snare clap",      120, 1, 0, 60, 64, 0, 0, 0, 0, 0, 0, 127, 115, 8, 16, 30, 3, 0, 0, 64, 9, 50, 25, 3, 84, 40, 50, 10, 127, 120, 64},
    {"snare snap lp2",  127, 1, 0, 55, 64, 0, 0, 0, 0, 0, 0, 100, 127, 0, 14, 30, 0, 20, 18, 44, 0, 70, 30, 7, 108, 20, 40, 40, 100, 110, 64},
    {"cym rec noise",   110, 2, 3, 70, 64, 3, 83, 80, 4, 118, 60, 0, 0, 0, 55, 50, 0, 0, 0, 64, 8, 40, 20, 2, 106, 10, 40, 10, 127, 96, 64},
    {"cym sine fm",     100, 2, 0, 78, 70, 0, 91, 90, 0, 97, 40, 0, 0, 0, 50, 50, 0, 0, 0, 64, 1, 30, 25, 3, 110, 50, 60, 40, 127, 98, 64},
    {"hat closed",      120, 3, 3, 82, 64, 3, 91, 60, 4, 120, 70, 0, 0, 0, 4, 30, 0, 0, 0, 64, 8, 40, 30, 2, 112, 10, 40, 10, 127, 120, 64},
    {"hat tri peak",    110, 3, 1, 86, 64, 0, 95, 70, 4, 124, 80, 0, 0, 2, 16, 40, 0, 0, 0, 64, 0, 40, 40, 6, 116, 20, 50, 20, 80, 116, 64},
};
#define LXC_N (sizeof LXC / sizeof LXC[0])
