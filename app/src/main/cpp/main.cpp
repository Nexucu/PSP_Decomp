// main.cpp — NativeActivity (android_native_app_glue) del port PSP Flower.
// Loop por frame (igual que el original, func_0x89006b4):
//   fase += 0.01, envuelve en 1.0 (ciclo 100 frames); dibuja; presenta;
//   el original hace WaitVblankStart — aquí el present FIFO de la
//   swapchain cumple ese papel (vsync).
// Controles (el efecto exacto de SQUARE/CROSS/stick no se resolvió):
//   - tocar la pantalla: mueve la flor un poco hacia el toque.
//   - botón atrás: sale del demo (como sceKernelExitGame).
// Audio: pendiente — el MP3 (dentro del WAV) está en assets/data/ pero
// este port aún no lo decodifica ni lo reproduce (ver README).
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/input.h>
#include <android/keycodes.h>
#include "renderer_vulkan.h"
#include "flower_scene.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FlowerVk", __VA_ARGS__)

struct AppState {
    Renderer renderer;
    float phase = 0.0f;
    float touchX = 0.0f;  // desplazamiento de la flor, px PSP (+-40)
    float touchY = 0.0f;
    bool hasTouch = false;
    bool wantExit = false;
};

static int32_t onInput(struct android_app* app, AInputEvent* event) {
    auto* state = (AppState*)app->userData;
    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN ||
            action == AMOTION_EVENT_ACTION_MOVE) {
            // Mapea el toque (px de pantalla) a px PSP 480x272.
            const float x = AMotionEvent_getX(event, 0);
            const float y = AMotionEvent_getY(event, 0);
            const int32_t w = ANativeWindow_getWidth(app->window);
            const int32_t h = ANativeWindow_getHeight(app->window);
            if (w > 0 && h > 0) {
                const float px = x / (float)w * flower::kScreenW - 240.0f;
                const float py = y / (float)h * flower::kScreenH - 136.0f;
                // La flor sigue al dedo, limitada a +-40 px (como un stick).
                state->touchX = px < -40.0f ? -40.0f : (px > 40.0f ? 40.0f : px);
                state->touchY = py < -40.0f ? -40.0f : (py > 40.0f ? 40.0f : py);
                state->hasTouch = true;
            }
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_UP ||
            action == AMOTION_EVENT_ACTION_CANCEL) {
            state->touchX = 0.0f;
            state->touchY = 0.0f;
            state->hasTouch = false;
            return 1;
        }
    } else if (type == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEYCODE_BACK) {
            state->wantExit = true;
            return 1;
        }
    }
    return 0;
}

static void onAppCmd(struct android_app* app, int32_t cmd) {
    auto* state = (AppState*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                if (!state->renderer.init(app->window, app->activity->assetManager)) {
                    LOGI("Renderer::init falló — ¿dispositivo sin Vulkan?");
                    ANativeActivity_finish(app->activity);
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            state->renderer.cleanup();
            break;
        case APP_CMD_WINDOW_RESIZED:
            if (app->window) state->renderer.onWindowResized(app->window);
            break;
        case APP_CMD_DESTROY:
            state->wantExit = true;
            break;
        default:
            break;
    }
}

void android_main(struct android_app* app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInput;

    // El hilo "update_thread" del original solo registra el callback de
    // salida; aquí no hace falta hilo extra.
    while (!state.wantExit) {
        int events = 0;
        struct android_poll_source* source = nullptr;
        // Sin ventana aún: bloquear; con ventana: no bloquear (render loop).
        const int timeout = app->window ? 0 : -1;
        while (ALooper_pollAll(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) { state.wantExit = true; break; }
            if (timeout == 0) break;
        }
        if (state.wantExit || app->destroyRequested) break;
        if (app->window) {
            state.phase = flower::advancePhase(state.phase);
            if (!state.renderer.drawFrame(state.phase, state.touchX, state.touchY)) {
                // Swapchain desactualizada (rotación): recrearla.
                state.renderer.onWindowResized(app->window);
            }
        }
    }
    state.renderer.cleanup();
}
