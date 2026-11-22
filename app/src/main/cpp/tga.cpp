// tga.cpp — ver tga.h
#include "tga.h"
#include <cstring>

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool loadTga(const uint8_t* data, size_t size, TgaImage& out) {
    if (!data || size < 18) return false;
    const uint8_t idLen = data[0];
    const uint8_t colorMapType = data[1];
    const uint8_t imageType = data[2];
    if (colorMapType != 0) return false;  // sin paleta (las 5 TGA del demo)
    if (imageType != 2 && imageType != 10) return false;
    const int w = rd16(data + 12);
    const int h = rd16(data + 14);
    const int bpp = data[16];
    const uint8_t desc = data[17];
    if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32)) return false;
    const bool topOrigin = (desc & 0x20) != 0;
    const size_t header = 18 + idLen;
    if (size < header) return false;
    const uint8_t* src = data + header;
    const size_t pxCount = (size_t)w * (size_t)h;
    const int bytesPerPx = bpp / 8;
    std::vector<uint8_t> bgr(pxCount * bytesPerPx);
    if (imageType == 2) {
        if (size - header < pxCount * bytesPerPx) return false;
        std::memcpy(bgr.data(), src, pxCount * bytesPerPx);
    } else {  // RLE type 10
        size_t in = 0, outPx = 0;
        const size_t avail = size - header;
        while (outPx < pxCount) {
            if (in >= avail) return false;
            const uint8_t hdr = src[in++];
            const int count = (hdr & 0x7F) + 1;
            if (hdr & 0x80) {  // run
                if (in + bytesPerPx > avail) return false;
                for (int i = 0; i < count && outPx < pxCount; ++i, ++outPx)
                    std::memcpy(&bgr[outPx * bytesPerPx], src + in, bytesPerPx);
                in += bytesPerPx;
            } else {
                if (in + (size_t)count * bytesPerPx > avail) return false;
                for (int i = 0; i < count && outPx < pxCount; ++i, ++outPx)
                    std::memcpy(&bgr[outPx * bytesPerPx], src + in + (size_t)i * bytesPerPx, bytesPerPx);
                in += (size_t)count * bytesPerPx;
            }
        }
    }
    out.width = w;
    out.height = h;
    out.bpp = bpp;
    out.hasAlpha = (bpp == 32);
    out.rgba.resize(pxCount * 4);
    for (int y = 0; y < h; ++y) {
        const int srcY = topOrigin ? y : (h - 1 - y);
        for (int x = 0; x < w; ++x) {
            const size_t s = ((size_t)srcY * w + x) * bytesPerPx;
            const size_t d = ((size_t)y * w + x) * 4;
            out.rgba[d + 0] = bgr[s + 2];  // BGR(A) -> RGBA
            out.rgba[d + 1] = bgr[s + 1];
            out.rgba[d + 2] = bgr[s + 0];
            out.rgba[d + 3] = (bpp == 32) ? bgr[s + 3] : 255;
        }
    }
    return true;
}
