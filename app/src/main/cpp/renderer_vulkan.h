// renderer_vulkan.h — Renderer Vulkan 1.0 del port PSP Flower.
// Un solo pipeline texturizado (blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
// filtro linear/linear, sin depth), 5 texturas (una por TGA), un vertex
// buffer dinámico que se rellena cada frame con buildFrame().
// En Android usa swapchain sobre ANativeWindow; en Linux (harness de
// pruebas) usa un target offscreen 480x272 sin swapchain.
#pragma once
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/native_window.h>
#else
struct AAssetManager;  // en Linux el harness carga los .spv/.tga desde disco
struct ANativeWindow;
#endif

class Renderer {
public:
    Renderer() = default;
    ~Renderer() { cleanup(); }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

#ifdef __ANDROID__
    // Inicializa con la ventana y los assets de la app. Devuelve false si
    // el dispositivo no tiene Vulkan 1.0+ o falla la creación.
    bool init(ANativeWindow* window, AAssetManager* assets);
    void onWindowResized(ANativeWindow* window);  // recrea swapchain
#else
    bool initHeadless();  // Linux: instancia + device + target offscreen
    // Lee el último frame renderizado (RGBA8, 480x272). Solo headless.
    bool readback(std::vector<uint8_t>& rgba, int& w, int& h);
#endif

    void cleanup();
    // Dibuja un frame completo (fase 0..1, toque en px PSP). En Android
    // presenta en la swapchain; en headless renderiza al target offscreen.
    bool drawFrame(float phase, float touchX, float touchY);

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        int width = 0, height = 0;
    };
    struct PushConstants {
        float ortho[16];
        float tint[4];
    };
    bool createInstance();
    bool pickPhysicalDevice();
    bool createDevice();
    bool createRenderPass(VkFormat format);
    bool createPipeline();
    bool createTextures(AAssetManager* assets);
    bool createTextureFromTga(const uint8_t* tgaData, size_t tgaSize, Texture& tex);
    bool createVertexBuffer();
    bool createCommandPool();
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
    void recordDrawCommands(VkCommandBuffer cmd, float phase, float touchX, float touchY);
    // Carga un asset ("data/....tga" o "shaders/....spv"). En Android vía
    // AAssetManager; en Linux vía disco (FLOWER_ASSET_ROOT o ./app/src/...).
    std::vector<uint8_t> loadAsset(AAssetManager* assets, const char* path);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
#ifdef __ANDROID__
    AAssetManager* m_assets = nullptr;  // para cargar SPIR-V/TGA en Android
#endif
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmdBuffer = VK_NULL_HANDLE;
    VkImage m_offscreenImage = VK_NULL_HANDLE;
    VkDeviceMemory m_offscreenMemory = VK_NULL_HANDLE;
    VkImageView m_offscreenView = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;  // headless
    VkSemaphore m_imageAvailableSem = VK_NULL_HANDLE;
    VkSemaphore m_renderFinishedSem = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
    std::vector<VkFramebuffer> m_swapFramebuffers;
    Texture m_textures[5];
    uint32_t m_queueFamily = 0;
    uint32_t m_swapWidth = 0, m_swapHeight = 0;
    bool m_headless = false;
    bool m_initialized = false;
};
