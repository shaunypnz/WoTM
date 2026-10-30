// test_rlvk.cpp: confere rlvk.cpp numa GPU Vulkan real, sem janela: desenha pela API do rlgl num framebuffer, le os pixels de volta e
// compara com o que o GL daria (ordem de linhas, profundidade LEQUAL, mistura, cull). Sai 0 so se tudo bater.
#include "rlgl.h"

#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
static const unsigned char *g_px;
static void expect(const char *what, int x, int ytop, int r, int g, int b)
{
    const unsigned char *p = g_px + size_t(63 - ytop) * 64 * 4 + size_t(x) * 4;   // y da ortho (0 em cima) para a linha do GL
    const bool ok = abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2;
    std::printf("%s %-28s got %3d %3d %3d want %3d %3d %3d\n", ok ? "ok  " : "FAIL", what, p[0], p[1], p[2], r, g, b);
    if (!ok) ++g_fail;
}
static void quad(float x, float y, float s, float z, unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool ccw = true)
{
    rlBegin(RL_QUADS);
    rlColor4ub(r, g, b, a);
    if (ccw)   // sentido anti-horario na tela, a frente no GL (Y da ortho para baixo)
    {
        rlTexCoord2f(0, 0); rlVertex3f(x, y, z);
        rlTexCoord2f(0, 1); rlVertex3f(x, y + s, z);
        rlTexCoord2f(1, 1); rlVertex3f(x + s, y + s, z);
        rlTexCoord2f(1, 0); rlVertex3f(x + s, y, z);
    }
    else
    {
        rlTexCoord2f(0, 0); rlVertex3f(x, y, z);
        rlTexCoord2f(1, 0); rlVertex3f(x + s, y, z);
        rlTexCoord2f(1, 1); rlVertex3f(x + s, y + s, z);
        rlTexCoord2f(0, 1); rlVertex3f(x, y + s, z);
    }
    rlEnd();
}
int main()
{
    rlglInit(64, 64);
    const unsigned color = rlLoadTexture(nullptr, 64, 64, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    const unsigned depth = rlLoadTextureDepth(64, 64, false);
    const unsigned fbo = rlLoadFramebuffer();
    rlFramebufferAttach(fbo, color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(fbo, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlEnableFramebuffer(fbo);
    rlViewport(0, 0, 64, 64);
    rlMatrixMode(RL_PROJECTION); rlLoadIdentity(); rlOrtho(0, 64, 64, 0, -10, 10);
    rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
    rlClearColor(0, 0, 255, 255); rlClearScreenBuffers();
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlEnableDepthTest();
    quad(0, 0, 10, 1, 255, 0, 0, 255);     // z=1 fica na frente (z do olho negativo olha para -z)
    quad(0, 0, 10, 0, 0, 255, 0, 255);     // atras: o teste de profundidade recusa
    rlDisableDepthTest();
    quad(20, 0, 10, 0, 255, 255, 255, 128);          // alfa sobre o azul
    rlSetBlendMode(RL_BLEND_ADD_COLORS);
    quad(40, 0, 10, 0, 255, 0, 0, 255);              // aditivo: magenta
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    quad(0, 20, 10, 0, 10, 20, 30, 7);               // sem mistura, como a apresentacao do quadro
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlEnableBackfaceCulling();
    quad(20, 20, 10, 0, 255, 255, 0, 255, false);    // costas: some
    quad(40, 20, 10, 0, 255, 255, 0, 255, true);     // frente: fica
    const unsigned char tp[4] = {0, 200, 0, 255};
    const unsigned t = rlLoadTexture(tp, 1, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    rlSetTexture(t);
    quad(0, 40, 10, 0, 255, 255, 255, 255);          // textura vezes cor
    rlSetTexture(0);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(255, 0, 255, 255);
    rlVertex3f(20, 40, 0); rlVertex3f(20, 50, 0); rlVertex3f(30, 50, 0);
    rlEnd();
    rlDrawRenderBatchActive();
    void *px = rlReadTexturePixels(color, 64, 64, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    g_px = static_cast<const unsigned char *>(px);
    expect("clear", 60, 60, 0, 0, 255);
    expect("depth keeps nearer", 5, 5, 255, 0, 0);
    expect("alpha blend", 25, 5, 128, 128, 255);
    expect("additive", 45, 5, 255, 0, 255);
    expect("custom no-blend", 5, 25, 10, 20, 30);
    expect("back face culled", 25, 25, 0, 0, 255);
    expect("front face kept", 45, 25, 255, 255, 0);
    expect("texture", 5, 45, 0, 200, 0);
    expect("triangle", 22, 48, 255, 0, 255);
    std::free(px);
    rlglClose();
    std::printf(g_fail ? "FAILED %d\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
