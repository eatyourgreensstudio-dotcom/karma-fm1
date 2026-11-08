/* host: the songs' LZ4 blocks (firmware/src/lz.h) round-trip; cc -O2 -Ifirmware/src -o build/host/lz_test tests/lz_test.c */
#include <stdio.h>
#include <stdlib.h>
#define LZ_HOST
#include "lz.h"
static uint8_t src[3840], pk[8192], out[3840];
static uint16_t ht[LZ_HT];
static int trip(const char *name, uint32_t n, uint32_t cap)
{
    uint32_t p = lz_pack(src, n, pk, cap, ht);
    int u;
    if (!p) { printf("%-10s n=%u: did not fit %u\n", name, n, cap); return cap >= n + n / 255u + 16u; }
    u = lz_unpack(pk, p, out, sizeof out);
    if (u != (int)n || memcmp(src, out, n)) { printf("FAIL %s n=%u p=%u u=%d\n", name, n, p, u); return 1; }
    printf("%-10s n=%u packed %u\n", name, n, p);
    return 0;
}
int main(int argc, char **argv)
{
    int bad = 0;
    uint32_t i, n;
    srand(1);
    memset(src, 0, sizeof src); bad |= trip("zeros", 3840, sizeof pk);
    for (i = 0; i < 3840; i++) src[i] = (uint8_t)rand(); bad |= trip("random", 3840, sizeof pk);
    bad |= trip("random-cap", 3840, 3000);              /* must refuse, never overrun */
    for (n = 0; n < 40; n++) { for (i = 0; i < n; i++) src[i] = (uint8_t)(i * 7 % 3); bad |= trip("tiny", n, sizeof pk); }
    for (i = 0; i < 3840; i++) src[i] = (uint8_t)(i % 10 == 4 ? rand() & 3 : i % 10 < 2 ? 60 + i % 13 : 0); bad |= trip("steps", 3840, sizeof pk);
    for (i = 1; i < (uint32_t)argc; i++) {              /* real parts: raw 3840-byte files */
        FILE *f = fopen(argv[i], "rb");
        if (f && fread(src, 1, 3840, f) == 3840) bad |= trip(argv[i], 3840, sizeof pk);
        if (f) fclose(f);
    }
    /* a corrupt block must not crash or overrun */
    for (i = 0; i < 3840; i++) src[i] = (uint8_t)(i / 7);
    n = lz_pack(src, 3840, pk, sizeof pk, ht);
    for (i = 0; i < 2000; i++) { uint8_t c[8192]; memcpy(c, pk, n); c[rand() % n] ^= (uint8_t)(1 + rand() % 255); (void)lz_unpack(c, n, out, sizeof out); }
    printf(bad ? "lz_test: FAIL\n" : "lz_test: ok\n");
    return bad;
}
