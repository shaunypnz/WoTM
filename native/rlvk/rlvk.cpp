// rlvk.cpp: a API do rlgl (raylib/src/rlgl.h) sobre wgpu-native, so com Vulkan. Primeira fatia da Fase 2 (docs/guides/VULKAN.md):
// lote de vertices, texturas RGBA8, framebuffers com cor e profundidade, mistura, teste de profundidade, cull e matrizes. O resto do
// rlgl entra conforme a lista do documento. As imagens ficam na ordem do GL (linha 0 embaixo): o shader inverte o Y e converte o Z,
// entao os quadros, as coordenadas de textura e os "src" de altura negativa do codigo nativo valem sem mudanca.
#include "rlgl.h"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

namespace
{
WGPUStringView sv(const char *s) { return WGPUStringView{s, WGPU_STRLEN}; }

const char *const kShader = R"(
struct U { mvp: mat4x4<f32> }
@group(0) @binding(0) var<uniform> u: U;
@group(1) @binding(0) var tex: texture_2d<f32>;
@group(1) @binding(1) var smp: sampler;
struct V { @builtin(position) p: vec4<f32>, @location(0) uv: vec2<f32>, @location(1) c: vec4<f32> }
@vertex fn vs(@location(0) pos: vec3<f32>, @location(1) uv: vec2<f32>, @location(2) c: vec4<f32>) -> V {
    var o: V;
    var p = u.mvp * vec4<f32>(pos, 1.0);
    p.y = -p.y;                  // ordem de linhas do GL
    p.z = (p.z + p.w) * 0.5;     // Z de -1..1 para 0..1
    o.p = p; o.uv = uv; o.c = c;
    return o;
}
@fragment fn fs(i: V) -> @location(0) vec4<f32> { return textureSample(tex, smp, i.uv) * i.c; }
)";

struct Vertex { float x, y, z, u, v; unsigned char r, g, b, a; };
struct Tex { WGPUTexture tex = nullptr; WGPUTextureView view = nullptr; int w = 0, h = 0; bool depth = false; WGPUBindGroup bind = nullptr; };
struct Fbo { unsigned color = 0, depth = 0; };
struct PipeKey
{
    int src, dst, eq, srcA, dstA, eqA; bool blend, depthTest, depthWrite, cull, lines, hasDepth;
    bool operator<(const PipeKey &o) const { return std::memcmp(this, &o, sizeof *this) < 0; }
};
struct Draw { PipeKey key; unsigned tex; unsigned first, count; float mvp[16]; };

WGPUInstance g_inst; WGPUDevice g_dev; WGPUQueue g_q; WGPUShaderModule g_mod; WGPUSampler g_smp;
WGPUBindGroupLayout g_ubl, g_tbl; WGPUPipelineLayout g_pl; WGPUBuffer g_ubo; WGPUBindGroup g_ubg;
std::map<PipeKey, WGPURenderPipeline> g_pipes;
std::unordered_map<unsigned, Tex> g_tex; std::unordered_map<unsigned, Fbo> g_fbo;
unsigned g_nextTex = 1, g_nextFbo = 1, g_white = 0, g_curTex = 0, g_curFbo = 0, g_backbuffer = 0;
std::vector<Vertex> g_verts; std::vector<Draw> g_draws;
Vertex g_v{}; int g_mode = RL_TRIANGLES; unsigned g_primStart = 0;
bool g_clear = false; float g_clearC[4] = {0, 0, 0, 1};
int g_vp[4] = {0, 0, 0, 0}, g_w = 0, g_h = 0;
bool g_depthTest = false, g_depthWrite = true, g_cull = true, g_blendOn = true;
int g_bSrc = RL_SRC_ALPHA, g_bDst = RL_ONE_MINUS_SRC_ALPHA, g_bEq = RL_FUNC_ADD, g_bSrcA = RL_SRC_ALPHA, g_bDstA = RL_ONE_MINUS_SRC_ALPHA, g_bEqA = RL_FUNC_ADD, g_custom[6];
struct M { float m[16]; };
M g_proj, g_view; std::vector<M> g_stack; M *g_cur = &g_view;

M ident() { M r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f; return r; }
M mul(const M &a, const M &b)   // a depois de b, colunas como no GL
{
    M r{};
    for (int c = 0; c < 4; ++c)
        for (int l = 0; l < 4; ++l)
            for (int k = 0; k < 4; ++k)
                r.m[c * 4 + l] += a.m[k * 4 + l] * b.m[c * 4 + k];
    return r;
}
void pump() { wgpuInstanceProcessEvents(g_inst); if (g_dev) wgpuDevicePoll(g_dev, false, nullptr); }
void onError(WGPUDevice const *, WGPUErrorType, WGPUStringView m, void *, void *) { std::fprintf(stderr, "[rlvk] erro: %.*s\n", int(m.length), m.data); }

WGPUBlendFactor factor(int gl)
{
    switch (gl)
    {
    case RL_ZERO: return WGPUBlendFactor_Zero;
    case RL_ONE: return WGPUBlendFactor_One;
    case RL_SRC_COLOR: return WGPUBlendFactor_Src;
    case RL_ONE_MINUS_SRC_COLOR: return WGPUBlendFactor_OneMinusSrc;
    case RL_SRC_ALPHA: return WGPUBlendFactor_SrcAlpha;
    case RL_ONE_MINUS_SRC_ALPHA: return WGPUBlendFactor_OneMinusSrcAlpha;
    case RL_DST_ALPHA: return WGPUBlendFactor_DstAlpha;
    case RL_ONE_MINUS_DST_ALPHA: return WGPUBlendFactor_OneMinusDstAlpha;
    case RL_DST_COLOR: return WGPUBlendFactor_Dst;
    case RL_ONE_MINUS_DST_COLOR: return WGPUBlendFactor_OneMinusDst;
    default: return WGPUBlendFactor_One;
    }
}
WGPUBlendOperation blendOp(int gl)
{
    return gl == RL_FUNC_SUBTRACT ? WGPUBlendOperation_Subtract : gl == RL_FUNC_REVERSE_SUBTRACT ? WGPUBlendOperation_ReverseSubtract
         : gl == RL_MIN ? WGPUBlendOperation_Min : gl == RL_MAX ? WGPUBlendOperation_Max : WGPUBlendOperation_Add;
}

WGPURenderPipeline pipeline(const PipeKey &k)
{
    auto it = g_pipes.find(k);
    if (it != g_pipes.end()) return it->second;
    WGPUVertexAttribute a[3] = {};
    a[0].format = WGPUVertexFormat_Float32x3; a[0].offset = 0; a[0].shaderLocation = 0;
    a[1].format = WGPUVertexFormat_Float32x2; a[1].offset = 12; a[1].shaderLocation = 1;
    a[2].format = WGPUVertexFormat_Unorm8x4; a[2].offset = 20; a[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vb.arrayStride = sizeof(Vertex); vb.stepMode = WGPUVertexStepMode_Vertex; vb.attributeCount = 3; vb.attributes = a;
    WGPUBlendState bs = WGPU_BLEND_STATE_INIT;
    bs.color.srcFactor = factor(k.src); bs.color.dstFactor = factor(k.dst); bs.color.operation = blendOp(k.eq);
    bs.alpha.srcFactor = factor(k.srcA); bs.alpha.dstFactor = factor(k.dstA); bs.alpha.operation = blendOp(k.eqA);
    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
    ct.format = WGPUTextureFormat_RGBA8Unorm; ct.blend = k.blend ? &bs : nullptr; ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = g_mod; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &ct;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = k.depthWrite && k.depthTest ? WGPUOptionalBool_True : WGPUOptionalBool_False;   // GL: sem teste, nao grava
    ds.depthCompare = k.depthTest ? WGPUCompareFunction_LessEqual : WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor d = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    d.layout = g_pl; d.vertex.module = g_mod; d.vertex.entryPoint = sv("vs"); d.vertex.bufferCount = 1; d.vertex.buffers = &vb;
    d.fragment = &fs; d.depthStencil = k.hasDepth ? &ds : nullptr;
    d.primitive.topology = k.lines ? WGPUPrimitiveTopology_LineList : WGPUPrimitiveTopology_TriangleList;
    d.primitive.cullMode = k.cull ? WGPUCullMode_Back : WGPUCullMode_None;
    d.primitive.frontFace = WGPUFrontFace_CW;   // o Y invertido troca o sentido: CCW do GL chega aqui como CW
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(g_dev, &d);
    g_pipes[k] = p;
    return p;
}

WGPUBindGroup texBind(Tex &t)
{
    if (t.bind) return t.bind;
    WGPUBindGroupEntry e[2] = {};
    e[0].binding = 0; e[0].textureView = t.view;
    e[1].binding = 1; e[1].sampler = g_smp;
    WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    d.layout = g_tbl; d.entryCount = 2; d.entries = e;
    return t.bind = wgpuDeviceCreateBindGroup(g_dev, &d);
}

PipeKey curKey()
{
    const Fbo *f = nullptr;
    auto it = g_fbo.find(g_curFbo ? g_curFbo : g_backbuffer);
    if (it != g_fbo.end()) f = &it->second;
    PipeKey k{};
    k.src = g_bSrc; k.dst = g_bDst; k.eq = g_bEq; k.srcA = g_bSrcA; k.dstA = g_bDstA; k.eqA = g_bEqA;
    k.blend = g_blendOn; k.depthTest = g_depthTest; k.depthWrite = g_depthWrite; k.cull = g_cull;
    k.lines = g_mode == RL_LINES; k.hasDepth = f && f->depth;
    return k;
}

void flush()
{
    const unsigned target = g_curFbo ? g_curFbo : g_backbuffer;
    if (g_draws.empty() && !g_clear) return;
    Fbo &f = g_fbo[target];
    Tex &ct = g_tex[f.color];
    WGPUBuffer vb = nullptr;
    if (!g_verts.empty())
    {
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = g_verts.size() * sizeof(Vertex); bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vb = wgpuDeviceCreateBuffer(g_dev, &bd);
        wgpuQueueWriteBuffer(g_q, vb, 0, g_verts.data(), bd.size);
    }
    constexpr unsigned kAlign = 256, kMax = 4096;
    if (g_draws.size() > kMax) { std::fprintf(stderr, "[rlvk] mais de %u desenhos num lote\n", kMax); g_draws.resize(kMax); }
    for (size_t i = 0; i < g_draws.size(); ++i) wgpuQueueWriteBuffer(g_q, g_ubo, i * kAlign, g_draws[i].mvp, 64);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g_dev, nullptr);
    WGPURenderPassColorAttachment col = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    col.view = ct.view; col.storeOp = WGPUStoreOp_Store;
    col.loadOp = g_clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    col.clearValue = {g_clearC[0], g_clearC[1], g_clearC[2], g_clearC[3]};
    WGPURenderPassDepthStencilAttachment dep = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rp.colorAttachmentCount = 1; rp.colorAttachments = &col;
    if (f.depth)
    {
        dep.view = g_tex[f.depth].view; dep.depthStoreOp = WGPUStoreOp_Store;
        dep.depthLoadOp = g_clear ? WGPULoadOp_Clear : WGPULoadOp_Load; dep.depthClearValue = 1.f;
        rp.depthStencilAttachment = &dep;
    }
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    if (g_vp[2] > 0 && g_vp[3] > 0)
        wgpuRenderPassEncoderSetViewport(pass, float(g_vp[0]), float(ct.h - g_vp[1] - g_vp[3]), float(g_vp[2]), float(g_vp[3]), 0.f, 1.f);
    if (vb) wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    for (size_t i = 0; i < g_draws.size(); ++i)
    {
        const Draw &d = g_draws[i];
        if (!d.count) continue;
        wgpuRenderPassEncoderSetPipeline(pass, pipeline(d.key));
        const uint32_t off = uint32_t(i * kAlign);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, g_ubg, 1, &off);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBind(g_tex[d.tex ? d.tex : g_white]), 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, d.count, 1, d.first, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g_q, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuRenderPassEncoderRelease(pass); wgpuCommandEncoderRelease(enc);
    if (vb) wgpuBufferRelease(vb);
    g_verts.clear(); g_draws.clear(); g_clear = false;
}

void closeDraw()   // fecha o desenho atual no estado de agora; a mudanca de estado seguinte abre outro
{
    const unsigned n = unsigned(g_verts.size());
    if (n == g_primStart) return;
    Draw d{}; d.key = curKey(); d.tex = g_curTex; d.first = g_primStart; d.count = n - g_primStart;
    const M m = mul(g_proj, g_view);
    std::memcpy(d.mvp, m.m, 64);
    g_draws.push_back(d);
    g_primStart = n;
}
void stateChange() { closeDraw(); }

Tex makeTexture(int w, int h, WGPUTextureFormat fmt, const void *data)
{
    Tex t; t.w = w; t.h = h; t.depth = fmt == WGPUTextureFormat_Depth32Float;
    WGPUTextureDescriptor d = WGPU_TEXTURE_DESCRIPTOR_INIT;
    d.size = {uint32_t(w), uint32_t(h), 1}; d.format = fmt; d.mipLevelCount = 1; d.sampleCount = 1; d.dimension = WGPUTextureDimension_2D;
    d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    t.tex = wgpuDeviceCreateTexture(g_dev, &d);
    t.view = wgpuTextureCreateView(t.tex, nullptr);
    if (data)
    {
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = t.tex;
        WGPUTexelCopyBufferLayout l = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; l.bytesPerRow = uint32_t(w) * 4; l.rowsPerImage = uint32_t(h);
        WGPUExtent3D e{uint32_t(w), uint32_t(h), 1};
        wgpuQueueWriteTexture(g_q, &dst, data, size_t(w) * h * 4, &l, &e);
    }
    return t;
}
} // namespace

extern "C" {
void rlglInit(int width, int height)
{
    WGPUInstanceExtras extras = {};
    extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    extras.backends = WGPUInstanceBackend_Vulkan;   // so Vulkan: sem GL de reserva
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
    id.nextInChain = &extras.chain;
    g_inst = wgpuCreateInstance(&id);
    WGPUAdapter adapter = nullptr; bool done = false;
    WGPURequestAdapterOptions o = WGPU_REQUEST_ADAPTER_OPTIONS_INIT; o.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    ac.mode = WGPUCallbackMode_AllowSpontaneous;
    ac.callback = [](WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView, void *out, void *fin) {
        if (s == WGPURequestAdapterStatus_Success) *static_cast<WGPUAdapter *>(out) = a;
        *static_cast<bool *>(fin) = true;
    };
    ac.userdata1 = &adapter; ac.userdata2 = &done;
    wgpuInstanceRequestAdapter(g_inst, &o, ac);
    while (!done) pump();
    if (!adapter) { std::fprintf(stderr, "[rlvk] sem GPU Vulkan: instale o driver Vulkan da placa\n"); std::exit(1); }
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success)
    {
        std::fprintf(stderr, "[rlvk] %.*s (Vulkan)\n", int(info.device.length), info.device.data);
        wgpuAdapterInfoFreeMembers(info);
    }
    done = false;
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.uncapturedErrorCallbackInfo.callback = onError;
    WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dc.mode = WGPUCallbackMode_AllowSpontaneous;
    dc.callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView, void *out, void *fin) {
        if (s == WGPURequestDeviceStatus_Success) *static_cast<WGPUDevice *>(out) = d;
        *static_cast<bool *>(fin) = true;
    };
    dc.userdata1 = &g_dev; dc.userdata2 = &done;
    wgpuAdapterRequestDevice(adapter, &dd, dc);
    while (!done) pump();
    if (!g_dev) { std::fprintf(stderr, "[rlvk] sem device\n"); std::exit(1); }
    g_q = wgpuDeviceGetQueue(g_dev);

    WGPUShaderSourceWGSL src = WGPU_SHADER_SOURCE_WGSL_INIT; src.code = sv(kShader);
    WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT; md.nextInChain = &src.chain;
    g_mod = wgpuDeviceCreateShaderModule(g_dev, &md);
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT; sd.maxAnisotropy = 1;
    sd.addressModeU = sd.addressModeV = WGPUAddressMode_ClampToEdge;
    g_smp = wgpuDeviceCreateSampler(g_dev, &sd);

    WGPUBindGroupLayoutEntry ue = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    ue.binding = 0; ue.visibility = WGPUShaderStage_Vertex; ue.buffer.type = WGPUBufferBindingType_Uniform; ue.buffer.hasDynamicOffset = true; ue.buffer.minBindingSize = 64;
    WGPUBindGroupLayoutDescriptor ul = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT; ul.entryCount = 1; ul.entries = &ue;
    g_ubl = wgpuDeviceCreateBindGroupLayout(g_dev, &ul);
    WGPUBindGroupLayoutEntry te[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    te[0].binding = 0; te[0].visibility = WGPUShaderStage_Fragment; te[0].texture.sampleType = WGPUTextureSampleType_Float; te[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    te[1].binding = 1; te[1].visibility = WGPUShaderStage_Fragment; te[1].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor tl = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT; tl.entryCount = 2; tl.entries = te;
    g_tbl = wgpuDeviceCreateBindGroupLayout(g_dev, &tl);
    WGPUBindGroupLayout ls[2] = {g_ubl, g_tbl};
    WGPUPipelineLayoutDescriptor pl = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT; pl.bindGroupLayoutCount = 2; pl.bindGroupLayouts = ls;
    g_pl = wgpuDeviceCreatePipelineLayout(g_dev, &pl);
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT; bd.size = 256 * 4096; bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    g_ubo = wgpuDeviceCreateBuffer(g_dev, &bd);
    WGPUBindGroupEntry be = {}; be.binding = 0; be.buffer = g_ubo; be.offset = 0; be.size = 64;
    WGPUBindGroupDescriptor bg = WGPU_BIND_GROUP_DESCRIPTOR_INIT; bg.layout = g_ubl; bg.entryCount = 1; bg.entries = &be;
    g_ubg = wgpuDeviceCreateBindGroup(g_dev, &bg);

    const unsigned char white[4] = {255, 255, 255, 255};
    g_white = rlLoadTexture(white, 1, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    g_w = width; g_h = height; g_vp[2] = width; g_vp[3] = height;
    g_proj = ident(); g_view = ident();
    g_v.r = g_v.g = g_v.b = g_v.a = 255;
    // quadro padrao: uma imagem como outra qualquer, para a janela apresentar (proxima fatia)
    g_backbuffer = rlLoadFramebuffer();
    rlFramebufferAttach(g_backbuffer, rlLoadTexture(nullptr, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1), RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(g_backbuffer, rlLoadTextureDepth(width, height, false), RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
}

void rlglClose(void) { flush(); }
unsigned int rlGetTextureIdDefault(void) { return g_white; }

unsigned int rlLoadTexture(const void *data, int width, int height, int format, int)
{
    if (format != RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) { std::fprintf(stderr, "[rlvk] formato de textura %d ainda nao suportado\n", format); return 0; }
    const unsigned id = g_nextTex++;
    g_tex[id] = makeTexture(width, height, WGPUTextureFormat_RGBA8Unorm, data);
    return id;
}
unsigned int rlLoadTextureDepth(int width, int height, bool)
{
    const unsigned id = g_nextTex++;
    g_tex[id] = makeTexture(width, height, WGPUTextureFormat_Depth32Float, nullptr);
    return id;
}
void rlUnloadTexture(unsigned int id)
{
    closeDraw(); flush();
    auto it = g_tex.find(id);
    if (it == g_tex.end()) return;
    if (it->second.bind) wgpuBindGroupRelease(it->second.bind);
    wgpuTextureViewRelease(it->second.view); wgpuTextureDestroy(it->second.tex); wgpuTextureRelease(it->second.tex);
    g_tex.erase(it);
}
unsigned int rlLoadFramebuffer(void) { const unsigned id = g_nextFbo++; g_fbo[id] = Fbo{}; return id; }
void rlFramebufferAttach(unsigned int fbo, unsigned int tex, int attachType, int, int)
{
    if (attachType == RL_ATTACHMENT_DEPTH) g_fbo[fbo].depth = tex; else g_fbo[fbo].color = tex;
}
bool rlFramebufferComplete(unsigned int id) { return g_fbo.count(id) && g_fbo[id].color; }
void rlEnableFramebuffer(unsigned int id) { closeDraw(); flush(); g_curFbo = id; }
void rlDisableFramebuffer(void) { closeDraw(); flush(); g_curFbo = 0; }
void rlViewport(int x, int y, int w, int h) { closeDraw(); flush(); g_vp[0] = x; g_vp[1] = y; g_vp[2] = w; g_vp[3] = h; }

void rlClearColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { g_clearC[0] = r / 255.f; g_clearC[1] = g / 255.f; g_clearC[2] = b / 255.f; g_clearC[3] = a / 255.f; }
void rlClearScreenBuffers(void) { closeDraw(); flush(); g_clear = true; }
void rlEnableDepthTest(void) { stateChange(); g_depthTest = true; }
void rlDisableDepthTest(void) { stateChange(); g_depthTest = false; }
void rlEnableDepthMask(void) { stateChange(); g_depthWrite = true; }
void rlDisableDepthMask(void) { stateChange(); g_depthWrite = false; }
void rlEnableBackfaceCulling(void) { stateChange(); g_cull = true; }
void rlDisableBackfaceCulling(void) { stateChange(); g_cull = false; }
void rlSetBlendFactors(int s, int d, int e) { g_custom[0] = g_custom[3] = s; g_custom[1] = g_custom[4] = d; g_custom[2] = g_custom[5] = e; }
void rlSetBlendFactorsSeparate(int s, int d, int sa, int da, int e, int ea) { g_custom[0] = s; g_custom[1] = d; g_custom[2] = e; g_custom[3] = sa; g_custom[4] = da; g_custom[5] = ea; }
void rlSetBlendMode(int mode)
{
    stateChange();
    g_blendOn = true; g_bEq = g_bEqA = RL_FUNC_ADD;
    switch (mode)
    {
    case RL_BLEND_ADDITIVE: g_bSrc = g_bSrcA = RL_SRC_ALPHA; g_bDst = g_bDstA = RL_ONE; break;
    case RL_BLEND_MULTIPLIED: g_bSrc = g_bSrcA = RL_DST_COLOR; g_bDst = g_bDstA = RL_ONE_MINUS_SRC_ALPHA; break;
    case RL_BLEND_ADD_COLORS: g_bSrc = g_bSrcA = RL_ONE; g_bDst = g_bDstA = RL_ONE; break;
    case RL_BLEND_SUBTRACT_COLORS: g_bSrc = g_bSrcA = RL_ONE; g_bDst = g_bDstA = RL_ONE; g_bEq = g_bEqA = RL_FUNC_SUBTRACT; break;
    case RL_BLEND_ALPHA_PREMULTIPLY: g_bSrc = g_bSrcA = RL_ONE; g_bDst = g_bDstA = RL_ONE_MINUS_SRC_ALPHA; break;
    case RL_BLEND_CUSTOM: case RL_BLEND_CUSTOM_SEPARATE:
        g_bSrc = g_custom[0]; g_bDst = g_custom[1]; g_bEq = g_custom[2]; g_bSrcA = g_custom[3]; g_bDstA = g_custom[4]; g_bEqA = g_custom[5]; break;
    default: g_bSrc = g_bSrcA = RL_SRC_ALPHA; g_bDst = g_bDstA = RL_ONE_MINUS_SRC_ALPHA; break;
    }
}

void rlSetTexture(unsigned int id) { if (id != g_curTex) { stateChange(); g_curTex = id; } }
void rlBegin(int mode) { if (mode != g_mode) stateChange(); g_mode = mode; g_primStart = unsigned(g_verts.size()) > g_primStart ? g_primStart : unsigned(g_verts.size()); }
void rlEnd(void) {}
void rlTexCoord2f(float x, float y) { g_v.u = x; g_v.v = y; }
void rlNormal3f(float, float, float) {}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { g_v.r = r; g_v.g = g; g_v.b = b; g_v.a = a; }
void rlVertex3f(float x, float y, float z)
{
    g_v.x = x; g_v.y = y; g_v.z = z;
    g_verts.push_back(g_v);
    if (g_mode == RL_QUADS && (g_verts.size() - g_primStart) % 6 == 4)   // o 4o vertice fecha o quadrilatero em dois triangulos
    {
        const size_t n = g_verts.size();
        const Vertex v0 = g_verts[n - 4], v2 = g_verts[n - 2], v3 = g_verts[n - 1];
        g_verts[n - 1] = v2; g_verts.push_back(v3); g_verts.push_back(v0);
        g_verts[n - 3 + 1] = v2;
    }
}
void rlVertex2f(float x, float y) { rlVertex3f(x, y, 0.f); }
void rlDrawRenderBatchActive(void) { closeDraw(); flush(); g_primStart = 0; }
bool rlCheckRenderBatchLimit(int) { return false; }

void rlMatrixMode(int mode) { closeDraw(); g_cur = mode == RL_PROJECTION ? &g_proj : &g_view; }
void rlLoadIdentity(void) { closeDraw(); *g_cur = ident(); }
void rlPushMatrix(void) { g_stack.push_back(*g_cur); }
void rlPopMatrix(void) { closeDraw(); if (!g_stack.empty()) { *g_cur = g_stack.back(); g_stack.pop_back(); } }
void rlMultMatrixf(const float *m) { closeDraw(); M o; std::memcpy(o.m, m, 64); *g_cur = mul(*g_cur, o); }
void rlTranslatef(float x, float y, float z) { M t = ident(); t.m[12] = x; t.m[13] = y; t.m[14] = z; closeDraw(); *g_cur = mul(*g_cur, t); }
void rlOrtho(double l, double r, double b, double t, double n, double f)
{
    M o{};
    o.m[0] = float(2.0 / (r - l)); o.m[5] = float(2.0 / (t - b)); o.m[10] = float(-2.0 / (f - n));
    o.m[12] = float(-(r + l) / (r - l)); o.m[13] = float(-(t + b) / (t - b)); o.m[14] = float(-(f + n) / (f - n)); o.m[15] = 1.f;
    closeDraw(); *g_cur = mul(*g_cur, o);
}
void *rlReadTexturePixels(unsigned int id, int width, int height, int)
{
    closeDraw(); flush();
    const uint32_t row = (uint32_t(width) * 4 + 255) & ~255u;
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT; bd.size = uint64_t(row) * height; bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer rb = wgpuDeviceCreateBuffer(g_dev, &bd);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g_dev, nullptr);
    WGPUTexelCopyTextureInfo s = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; s.texture = g_tex[id].tex;
    WGPUTexelCopyBufferInfo d = WGPU_TEXEL_COPY_BUFFER_INFO_INIT; d.buffer = rb; d.layout.bytesPerRow = row; d.layout.rowsPerImage = uint32_t(height);
    WGPUExtent3D e{uint32_t(width), uint32_t(height), 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &s, &d, &e);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g_q, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    bool done = false;
    WGPUBufferMapCallbackInfo cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void *fin, void *) { *static_cast<bool *>(fin) = true; };
    cb.userdata1 = &done;
    wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, bd.size, cb);
    while (!done) { wgpuDevicePoll(g_dev, true, nullptr); wgpuInstanceProcessEvents(g_inst); }
    const unsigned char *src = static_cast<const unsigned char *>(wgpuBufferGetConstMappedRange(rb, 0, bd.size));
    unsigned char *out = static_cast<unsigned char *>(std::malloc(size_t(width) * height * 4));
    for (int y = 0; y < height; ++y) std::memcpy(out + size_t(y) * width * 4, src + size_t(y) * row, size_t(width) * 4);
    wgpuBufferUnmap(rb); wgpuBufferRelease(rb);
    return out;
}
} // extern "C"
