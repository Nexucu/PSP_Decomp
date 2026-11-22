#version 450
// PSP Flower — Vulkan port. Vertex shader.
// Coordenadas de entrada en espacio PSP (480x272, origen arriba-izquierda),
// igual que el viewport (2048,2048,480,272) + offset (1808,1912) del original:
// el orto de sceGu se reproduce con la matriz push-constant (480x272 -> NDC).
layout(location = 0) in vec2 inPos;   // x,y en coords PSP
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 ortho;   // orto 480x272, Y invertida (Vulkan NDC: Y abajo)
    vec4 tint;    // color de vértice / TexEnv aproximado (blanco por defecto)
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

void main() {
    gl_Position = pc.ortho * vec4(inPos, 0.0, 1.0);
    fragUV = inUV;
    fragTint = pc.tint;
}
