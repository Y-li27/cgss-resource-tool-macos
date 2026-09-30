#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GBKswapUTF8.h"

void utf8_to_gbk(const char *in, char *out, int out_size) {
    if (!in || !out || out_size <= 0) return;
    strncpy(out, in, out_size - 1);
    out[out_size - 1] = 0;
}

void gbk_to_utf8(const char *in, char *out, int out_size) {
    if (!in || !out || out_size <= 0) return;
    strncpy(out, in, out_size - 1);
    out[out_size - 1] = 0;
}
