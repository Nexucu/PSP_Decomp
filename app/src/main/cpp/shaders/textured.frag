#version 450
// PSP Flower — Vulkan port. Fragment shader.
// Blend clásico SRC_ALPHA / ONE_MINUS_SRC_ALPHA (ADD), como el original
// (BlendFunc ADD, SRC_ALPHA, ONE_MINUS_SRC_ALPHA). Filtro linear/linear
// en el sampler. Sin blending aditivo.
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = texture(tex, fragUV);
    outColor = c * fragTint;
}
