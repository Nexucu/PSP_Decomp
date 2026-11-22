// flower_scene.cpp — ver flower_scene.h
#include "flower_scene.h"
#include <cmath>

namespace flower {
namespace {

// Sprite fullscreen (bg / overlay): quad 0..480 x 0..272 como 2 triángulos.
void addFullscreenQuad(std::vector<Vertex>& v) {
    const Vertex q[6] = {
        {0, 0, 0, 0}, {kScreenW, 0, 1, 0}, {kScreenW, kScreenH, 1, 1},
        {0, 0, 0, 0}, {kScreenW, kScreenH, 1, 1}, {0, kScreenH, 0, 1},
    };
    v.insert(v.end(), q, q + 6);
}

// Celda del atlas 256x128: 8 columnas x 2 filas de 32x64.
// Cada pétalo/rama usa una celda distinta (como un atlas de sprites).
void cellUV(int cell, float& u0, float& v0, float& u1, float& v1) {
    u0 = (float)(cell % 8) / 8.0f;
    v0 = (float)(cell / 8) / 2.0f;
    u1 = u0 + 1.0f / 8.0f;
    v1 = v0 + 0.5f;
}

// Un pétalo = 2 triángulos (centro, baseIzq, punta) + (centro, punta, baseDer).
// a = ángulo del pétalo, cx/cy = centro, len = largo, wid = semiancho base.
void addPetal(std::vector<Vertex>& v, float cx, float cy, float a,
              float len, float wid, int cell) {
    float u0, v0, u1, v1;
    cellUV(cell, u0, v0, u1, v1);
    const float uc = (u0 + u1) * 0.5f, vc = (v0 + v1) * 0.5f;
    const float dx = std::cos(a), dy = std::sin(a);
    const float px = -dy, py = dx;  // perpendicular
    const float tx = cx + dx * len, ty = cy + dy * len;      // punta
    const float bx1 = cx + px * wid, by1 = cy + py * wid;    // base izq
    const float bx2 = cx - px * wid, by2 = cy - py * wid;    // base der
    const Vertex tris[6] = {
        {cx, cy, uc, vc}, {bx1, by1, u0, v1}, {tx, ty, uc, v0},
        {cx, cy, uc, vc}, {tx, ty, uc, v0}, {bx2, by2, u1, v1},
    };
    v.insert(v.end(), tris, tris + 6);
}

}  // namespace

Frame buildFrame(float phase, float touchX, float touchY) {
    Frame f;
    const float twoPi = 6.283185307179586f;
    const float cx = 240.0f + touchX;
    const float cy = 136.0f + touchY;

    // 1) bg — sprite fullscreen.
    f.draws[0].tex = TEX_BG;
    addFullscreenQuad(f.draws[0].verts);

    // 2) tron — tronco vertical + 6 ramas. Sin rotación de fase
    // (el tronco es fijo; las ramas oscilan levemente con la fase).
    f.draws[1].tex = TEX_TRON;
    {
        auto& v = f.draws[1].verts;
        // Tronco: 2 quads (4 triángulos) que se estrechan hacia arriba,
        // de la base (240,272) a la copa (240,150).
        const float seg[3][4] = {
            {232, 272, 248, 272}, {234, 220, 246, 220}, {237, 150, 243, 150},
        };
        for (int i = 0; i < 2; ++i) {
            float u0, v0, u1, v1;
            cellUV(i, u0, v0, u1, v1);
            const float* a = seg[i];
            const float* b = seg[i + 1];
            const Vertex q[6] = {
                {a[0], a[1], u0, v1}, {a[2], a[3], u1, v1}, {b[2], b[3], u1, v0},
                {a[0], a[1], u0, v1}, {b[2], b[3], u1, v0}, {b[0], b[1], u0, v0},
            };
            v.insert(v.end(), q, q + 6);
        }
        // 6 ramas: pétalos cortos que salen del tronco, leve vaivén con fase.
        for (int i = 0; i < 6; ++i) {
            const float base = (i % 2 == 0) ? -0.9f : 0.9f;  // izq / der
            const float a = base * 1.1f + std::sin(phase * twoPi + i) * 0.08f;
            // ángulo medido desde la vertical hacia el lado
            const float ang = -1.5707963f + a;  // -90° = hacia arriba
            const float bx = 240.0f, by = 200.0f - (float)(i / 2) * 18.0f;
            addPetal(v, bx, by, ang, 52.0f, 9.0f, 2 + i);
        }
    }

    // 3) flower — 12 pétalos que giran con la fase (rotación completa
    // cada 100 frames). Centro en (240,136) + toque.
    f.draws[2].tex = TEX_FLOWER;
    {
        auto& v = f.draws[2].verts;
        for (int i = 0; i < 12; ++i) {
            const float a = phase * twoPi + (float)i * twoPi / 12.0f;
            addPetal(v, cx, cy, a, 95.0f, 16.0f, i);
        }
        // Centro de la flor: pequeño disco de 2 triángulos (quad).
        const float r = 14.0f;
        float u0, v0, u1, v1;
        cellUV(0, u0, v0, u1, v1);
        const Vertex q[6] = {
            {cx - r, cy - r, u0, v0}, {cx + r, cy - r, u1, v0}, {cx + r, cy + r, u1, v1},
            {cx - r, cy - r, u0, v0}, {cx + r, cy + r, u1, v1}, {cx - r, cy + r, u0, v1},
        };
        v.insert(v.end(), q, q + 6);
    }

    // 4) overlay — sprite fullscreen (RGB, sin alfa: cubre como el original).
    // Se dibuja con kOverlayAlpha (ver flower_scene.h): opaco taparía la flor.
    f.draws[3].tex = TEX_OVERLAY;
    f.draws[3].alpha = kOverlayAlpha;
    addFullscreenQuad(f.draws[3].verts);

    // 5) logo — sprite 256x64 centrado en X, 12 px sobre el borde inferior.
    f.draws[4].tex = TEX_LOGO;
    {
        auto& v = f.draws[4].verts;
        const float x0 = (kScreenW - 256.0f) / 2.0f;  // 112
        const float y0 = kScreenH - 64.0f - 12.0f;    // 196
        const Vertex q[6] = {
            {x0, y0, 0, 0}, {x0 + 256, y0, 1, 0}, {x0 + 256, y0 + 64, 1, 1},
            {x0, y0, 0, 0}, {x0 + 256, y0 + 64, 1, 1}, {x0, y0 + 64, 0, 1},
        };
        v.insert(v.end(), q, q + 6);
    }
    return f;
}

}  // namespace flower
