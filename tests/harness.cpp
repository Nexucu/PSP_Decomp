// harness.cpp — Validación en Linux del código Vulkan del port.
// Compila renderer_vulkan.cpp + flower_scene.cpp + tga.cpp contra
// libvulkan-dev y comprueba, sin ventana:
//   1. Las 5 TGA cargan (dimensiones y bpp esperados).
//   2. buildFrame() genera los 5 draws en orden, con la fase avanzando
//      0.01/frame y envolviendo en 1.0.
//   3. El renderer crea instancia/dispositivo, sube las 5 texturas,
//      compila los SPIR-V, crea el pipeline y renderiza 3 frames
//      offscreen (fases 0.0, 0.5, 0.99) a 480x272.
//   4. El frame renderizado no es negro: el píxel central y la media
//      de color son no nulos, y cambia entre fases (la flor gira).
//   5. Escribe frame_0.ppm / frame_1.ppm para inspección visual.
// Uso: ./harness  (con FLOWER_ASSET_ROOT=app/src/main/assets si hace falta)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include "../app/src/main/cpp/flower_scene.h"
#include "../app/src/main/cpp/renderer_vulkan.h"
#include "../app/src/main/cpp/tga.h"

static std::vector<uint8_t> readFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d((size_t)n);
    std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    return d;
}

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++failures; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    std::fflush(stdout); \
} while (0)

static void writePpm(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) std::fwrite(&rgba[(size_t)i * 4], 1, 3, f);
    std::fclose(f);
}

int main() {
    const char* root = std::getenv("FLOWER_ASSET_ROOT");
    const std::string base = root ? root : "app/src/main/assets";

    // 1) TGA
    struct Expect { const char* name; int w, h, bpp; };
    const Expect exp[5] = {
        {"data/flower.tga", 256, 128, 32},
        {"data/tron.tga", 256, 128, 32},
        {"data/overlay.tga", 256, 128, 24},
        {"data/bg.tga", 256, 256, 24},
        {"data/logo.tga", 256, 64, 32},
    };
    for (auto& e : exp) {
        auto data = readFile(base + "/" + e.name);
        TgaImage img;
        bool okLoad = !data.empty() && loadTga(data.data(), data.size(), img);
        CHECK(okLoad && img.width == e.w && img.height == e.h && img.bpp == e.bpp,
              "TGA %s %dx%d %dbpp", e.name, img.width, img.height, img.bpp);
    }

    // 2) Escena
    flower::Frame fr = flower::buildFrame(0.0f, 0.0f, 0.0f);
    const flower::TexId order[5] = {flower::TEX_BG, flower::TEX_TRON, flower::TEX_FLOWER,
                                    flower::TEX_OVERLAY, flower::TEX_LOGO};
    for (int i = 0; i < 5; ++i) {
        CHECK(fr.draws[i].tex == order[i] && !fr.draws[i].verts.empty() &&
              fr.draws[i].verts.size() % 3 == 0,
              "draw %d tex=%d verts=%zu", i, (int)fr.draws[i].tex, fr.draws[i].verts.size());
    }
    float phase = 0.0f;
    for (int i = 0; i < 100; ++i) phase = flower::advancePhase(phase);
    // En float32, 100 x 0.01f = 0.99999998 (no llega a 1.0 exacto: en la
    // PSP pasa lo mismo); envuelve en el frame 101.
    CHECK(phase > 0.99f && phase <= 1.0f, "fase tras 100 frames (phase=%.6f)", phase);
    phase = flower::advancePhase(phase);
    CHECK(phase < 0.02f, "fase envuelve en el frame 101 (phase=%.6f)", phase);
    flower::Frame f0 = flower::buildFrame(0.0f, 0, 0);
    flower::Frame f1 = flower::buildFrame(0.5f, 0, 0);
    bool moved = false;
    for (size_t i = 0; i < f0.draws[2].verts.size(); ++i)
        if (f0.draws[2].verts[i].x != f1.draws[2].verts[i].x ||
            f0.draws[2].verts[i].y != f1.draws[2].verts[i].y) { moved = true; break; }
    CHECK(moved, "la flor gira con la fase (fase 0 vs 0.5)");

    // 3) Renderer Vulkan headless
    Renderer r;
    if (!r.initHeadless()) {
        CHECK(false, "Renderer::initHeadless");
        std::printf("HARNESS: %d FALLOS\n", failures);
        return 1;
    }
    CHECK(true, "Renderer::initHeadless");
    std::vector<uint8_t> prev;
    for (int k = 0; k < 3; ++k) {
        const float ph = k == 0 ? 0.0f : (k == 1 ? 0.5f : 0.99f);
        CHECK(r.drawFrame(ph, 0.0f, 0.0f), "drawFrame fase %.2f", ph);
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        CHECK(r.readback(rgba, w, h), "readback %dx%d", w, h);
        // 4) no negro, y distinto entre fases
        uint64_t sum = 0;
        for (uint8_t c : rgba) sum += c;
        const size_t center = ((size_t)(h / 2) * w + w / 2) * 4;
        CHECK(sum > 0 && rgba[center] + rgba[center + 1] + rgba[center + 2] > 0,
              "frame %d no negro (media=%.1f, centro RGB %d,%d,%d)",
              k, (double)sum / rgba.size(), rgba[center], rgba[center + 1], rgba[center + 2]);
        if (k < 2) writePpm(k == 0 ? "frame_0.ppm" : "frame_1.ppm", rgba, w, h);
        if (!prev.empty()) CHECK(prev != rgba, "el frame cambia con la fase");
        prev = rgba;
    }
    r.cleanup();
    std::printf(failures == 0 ? "HARNESS: TODO OK\n" : "HARNESS: %d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
