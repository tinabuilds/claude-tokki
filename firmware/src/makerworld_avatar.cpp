// Avatar decode shared by makerworld.cpp and the sim stub: PNG → LVGL's
// ARGB8888 byte order (B, G, R, A), double-buffered so the UI can keep
// drawing the previous image while a new one decodes.
#include "makerworld.h"
#include <lvgl.h>
#define LODEPNG_NO_COMPILE_CPP   // C API only; its C++ wrappers clash with the extern "C" block
#include "src/libs/lodepng/lodepng.h"
#include <stdlib.h>
#include <string.h>

static uint8_t* bufs[2] = {nullptr, nullptr};
static int cur = -1, aw = 0, ah = 0;

bool makerworld_set_avatar_png(const uint8_t* png, size_t len) {
    // LVGL's bundled lodepng returns an lv_draw_buf_t (RGBA8888 rows, maybe padded), not raw bytes
    lv_draw_buf_t* img = nullptr;
    unsigned w = 0, h = 0;
    if (lodepng_decode32((unsigned char**)&img, &w, &h, png, len) || !img || w > 128 || h > 128) {
        if (img) lv_draw_buf_destroy(img);
        return false;
    }
    int next = cur == 0 ? 1 : 0;
    free(bufs[next]);
    bufs[next] = (uint8_t*)malloc(w * h * 4);
    if (!bufs[next]) { lv_draw_buf_destroy(img); return false; }
    for (unsigned y = 0; y < h; y++) {   // RGBA → BGRA, dropping any row padding
        const uint8_t* src = img->data + y * img->header.stride;
        uint8_t* dst = bufs[next] + y * w * 4;
        for (unsigned x = 0; x < w; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    lv_draw_buf_destroy(img);
    aw = w; ah = h; cur = next;
    return true;
}

const uint8_t* makerworld_avatar(int* w, int* h) {
    *w = aw; *h = ah;
    return cur < 0 ? nullptr : bufs[cur];
}
