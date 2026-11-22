// tga.h — Loader TGA mínimo, igual que el del demo PSP original.
// El original carga las 5 TGA (type 2, sin compresión, 24/32 bpp) en VRAM
// con sceIoRead + una rutina de subida (func_0x89028c0 / 0x890285c).
// Aquí: type 2 (sin comprimir) y type 10 (RLE), 24/32 bpp, origen
// inferior o superior (bit 5 del descriptor). Salida: RGBA8, fila 0 = arriba.
#pragma once
#include <cstdint>
#include <vector>

struct TgaImage {
    int width = 0;
    int height = 0;
    int bpp = 0;              // bpp del archivo (24 o 32)
    bool hasAlpha = false;    // true si 32 bpp
    std::vector<uint8_t> rgba;  // width*height*4, RGBA8, fila 0 arriba
};

// Devuelve false si el formato no es soportado o los datos están truncados.
bool loadTga(const uint8_t* data, size_t size, TgaImage& out);
