// flower_scene.h — Lógica de escena del demo, independiente de plataforma.
// Fiel al análisis de flower_decompilado_v03.c (main = func_0x89006b4):
//   pantalla PSP 480x272, draw buffer 512 de ancho, viewport (2048,2048,480,272),
//   scissor (0,0,480,272), offset (1808,1912), matrices orto.
//   fase float += 0.01/frame, envuelve en 1.0  => ciclo de 100 frames.
//   TexEnvColor 0x00FFFF00, TexFilter linear/linear.
//   BlendFunc ADD, SRC_ALPHA, ONE_MINUS_SRC_ALPHA (alpha clásico).
//   Orden de dibujo por frame:
//     1) bg     sprite fullscreen (GU_SPRITES, 2 vértices)
//     2) tron   triángulos (count variable, vértices generados con sin/cos)
//     3) flower triángulos (misma generación, rotada por la fase)
//     4) overlay sprite fullscreen
//     5) logo   sprite
//
// APROXIMACIÓN DECLARADA: la generación exacta de vértices de tron/flower
// (qué pétalos, cuántos triángulos, qué UV del atlas) no se resolvió
// vértice a vértice desde el binario. La malla de abajo es una
// reconstrucción del efecto con sin/cos: una flor de 12 pétalos que gira
// con la fase, sobre un tronco con 6 ramas. Los sprites, el orden, la
// fase, el blend y los filtros sí son fieles al análisis.
#pragma once
#include <cstdint>
#include <vector>

namespace flower {

// Constantes PSP originales.
constexpr int kScreenW = 480;
constexpr int kScreenH = 272;
constexpr int kDrawBufferW = 512;  // ancho del draw buffer en VRAM
constexpr float kPhaseStep = 0.01f;  // += 0.01/frame, envuelve en 1.0
constexpr int kFramesPerCycle = 100;
// TexEnvColor original 0x00FFFF00 (documentado; el shader usa tint blanco
// por defecto — ver README, "qué es aproximado").
constexpr uint32_t kTexEnvColor = 0x00FFFF00u;

// Alfa del overlay: DECISIÓN DE RECONSTRUCCIÓN, no medida del binario.
// overlay.tga es RGB 24bpp, sin canal alfa: dibujado opaco y a pantalla
// completa DESPUÉS de la flor, taparía por completo tron y flor. En el
// demo original la flor se ve a través de ese velo de luz, así que el
// port lo dibuja al 50% (el valor exacto del original — color de vértice
// o factor de blend — no se resolvió). Ver README.
constexpr float kOverlayAlpha = 0.5f;

// Texturas en el orden de carga original en VRAM desde 0x040cc000.
enum TexId { TEX_FLOWER = 0, TEX_TRON = 1, TEX_OVERLAY = 2, TEX_BG = 3, TEX_LOGO = 4, TEX_COUNT = 5 };
inline const char* texAssetPath(TexId id) {
    switch (id) {
        case TEX_FLOWER:  return "data/flower.tga";
        case TEX_TRON:    return "data/tron.tga";
        case TEX_OVERLAY: return "data/overlay.tga";
        case TEX_BG:      return "data/bg.tga";
        case TEX_LOGO:    return "data/logo.tga";
        default: return "";
    }
}

struct Vertex {
    float x, y;  // coords PSP (0..480, 0..272)
    float u, v;  // coords de textura (0..1)
};

// Un "draw" = una textura + una lista de triángulos, en el orden original.
struct DrawCall {
    TexId tex;
    std::vector<Vertex> verts;  // múltiplo de 3 (triángulos)
    // Alfa global del draw (multiplica al alfa de la textura en el blend).
    // 1.0 salvo el overlay (ver kOverlayAlpha).
    float alpha = 1.0f;
};

struct Frame {
    DrawCall draws[5];  // bg, tron, flower, overlay, logo — en ese orden
};

// Avanza la fase como el original: phase += 0.01; si >= 1.0, -= 1.0.
inline float advancePhase(float phase) {
    phase += kPhaseStep;
    if (phase >= 1.0f) phase -= 1.0f;
    return phase;
}

// Genera los triángulos de un frame completo.
// touchX/touchY: desplazamiento del centro de la flor en px PSP
// (el original lee el stick analógico con sceCtrlReadBufferPositive;
// aquí el toque de pantalla lo mueve; rango recomendado +-40).
// Colocación del logo (no resuelta en el análisis): sprite de 256x64
// centrado en X, a 12 px del borde inferior (y 196..260).
Frame buildFrame(float phase, float touchX, float touchY);

}  // namespace flower
