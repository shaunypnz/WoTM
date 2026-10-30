// wotm_post.hpp: filtros da apresentacao, entre o quadro nativo e a janela: FXAA (WOTM_FXAA=1) e nitidez adaptativa ao contraste,
// segundo o CAS da AMD (WOTM_SHARPEN=0..1). Desligados por padrao. As fotos e o texId 0 (quadro anterior) nao passam por aqui.
#pragma once

#include <cstdlib>

#include "raylib.h"

namespace wotm::post
{
inline const char *const kFrag = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 texel;
uniform float fxaaOn;
uniform float sharpen;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 px(vec2 uv, float x, float y) { return texture(texture0, uv + vec2(x, y) * texel).rgb; }
vec3 fxaa(vec2 uv)
{
    vec3 m = px(uv, 0.0, 0.0);
    float lm = luma(m), ln = luma(px(uv, 0.0, -1.0)), ls = luma(px(uv, 0.0, 1.0)), le = luma(px(uv, 1.0, 0.0)), lw = luma(px(uv, -1.0, 0.0));
    float hi = max(lm, max(max(ln, ls), max(le, lw))), lo = min(lm, min(min(ln, ls), min(le, lw)));
    if (hi - lo < max(0.0312, hi * 0.125))
        return m;
    float lnw = luma(px(uv, -1.0, -1.0)), lne = luma(px(uv, 1.0, -1.0)), lsw = luma(px(uv, -1.0, 1.0)), lse = luma(px(uv, 1.0, 1.0));
    float edgeH = abs(-2.0 * lw + lnw + lsw) + 2.0 * abs(-2.0 * lm + ln + ls) + abs(-2.0 * le + lne + lse);
    float edgeV = abs(-2.0 * ln + lnw + lne) + 2.0 * abs(-2.0 * lm + lw + le) + abs(-2.0 * ls + lsw + lse);
    bool horizontal = edgeH >= edgeV;
    float l1 = horizontal ? ln : lw, l2 = horizontal ? ls : le;
    bool first = abs(l1 - lm) >= abs(l2 - lm);
    float stepN = (horizontal ? texel.y : texel.x) * (first ? -1.0 : 1.0);
    float grad = max(abs(l1 - lm), abs(l2 - lm)) * 0.25;
    float lEdge = 0.5 * (lm + (first ? l1 : l2));
    vec2 e = uv + (horizontal ? vec2(0.0, stepN * 0.5) : vec2(stepN * 0.5, 0.0));
    vec2 dir = horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 a = e - dir, b = e + dir;
    float da = 0.0, db = 0.0;
    bool doneA = false, doneB = false;
    const float steps[8] = float[8](1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0, 8.0);
    for (int i = 0; i < 8 && !(doneA && doneB); i++)
    {
        if (!doneA) { da = luma(texture(texture0, a).rgb) - lEdge; doneA = abs(da) >= grad; if (!doneA) a -= dir * steps[i]; }
        if (!doneB) { db = luma(texture(texture0, b).rgb) - lEdge; doneB = abs(db) >= grad; if (!doneB) b += dir * steps[i]; }
    }
    float toA = horizontal ? uv.x - a.x : uv.y - a.y, toB = horizontal ? b.x - uv.x : b.y - uv.y;
    float nearest = min(toA, toB), span = toA + toB;
    float off = (((toA < toB ? da : db) < 0.0) != (lm - lEdge < 0.0)) ? 0.5 - nearest / span : 0.0;
    float avg = (2.0 * (ln + ls + le + lw) + lnw + lne + lsw + lse) / 12.0;
    float sub = clamp(abs(avg - lm) / (hi - lo), 0.0, 1.0);
    sub = smoothstep(0.0, 1.0, sub);
    off = max(off, sub * sub * 0.75);
    vec2 uv2 = uv + (horizontal ? vec2(0.0, off * stepN) : vec2(off * stepN, 0.0));
    return texture(texture0, uv2).rgb;
}
vec3 cas(vec2 uv, vec3 c)
{
    vec3 n = px(uv, 0.0, -1.0), s = px(uv, 0.0, 1.0), e = px(uv, 1.0, 0.0), w = px(uv, -1.0, 0.0);
    vec3 lo = min(c, min(min(n, s), min(e, w))), hi = max(c, max(max(n, s), max(e, w)));
    vec3 amp = sqrt(clamp(min(lo, 1.0 - hi) / max(hi, vec3(1e-4)), 0.0, 1.0));
    vec3 k = -amp / mix(8.0, 5.0, sharpen);
    return clamp((c + (n + s + e + w) * k) / (1.0 + 4.0 * k), 0.0, 1.0);
}
void main()
{
    vec3 c = fxaaOn > 0.5 ? fxaa(fragTexCoord) : texture(texture0, fragTexCoord).rgb;
    if (sharpen > 0.0)
        c = cas(fragTexCoord, c);
    finalColor = vec4(c, 1.0);
}
)";

struct Filter
{
    Shader shader{};
    int texelLoc = -1;
    bool on = false, tried = false;
    float size[2] = {0.f, 0.f};

    // Na thread da janela, antes do desenho do quadro. Devolve false se nao ha filtro ligado (desenho normal).
    bool begin(float width, float height)
    {
        if (!tried)
        {
            tried = true;
            const char *a = std::getenv("WOTM_FXAA"), *b = std::getenv("WOTM_SHARPEN");
            const float fxaa = a && std::atof(a) > 0.f ? 1.f : 0.f;
            float sharp = b ? static_cast<float>(std::atof(b)) : 0.f;
            sharp = sharp < 0.f ? 0.f : (sharp > 1.f ? 1.f : sharp);
            if (fxaa > 0.f || sharp > 0.f)
            {
                shader = LoadShaderFromMemory(nullptr, kFrag);
                if (shader.id != 0 && IsShaderValid(shader))
                {
                    texelLoc = GetShaderLocation(shader, "texel");
                    SetShaderValue(shader, GetShaderLocation(shader, "fxaaOn"), &fxaa, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(shader, GetShaderLocation(shader, "sharpen"), &sharp, SHADER_UNIFORM_FLOAT);
                    on = true;
                }
                else
                    TraceLog(LOG_WARNING, "[post] filtro nao compilou: quadro sem filtro");
            }
        }
        if (!on)
            return false;
        if (size[0] != width || size[1] != height)
        {
            size[0] = width;
            size[1] = height;
            const float texel[2] = {1.f / width, 1.f / height};
            SetShaderValue(shader, texelLoc, texel, SHADER_UNIFORM_VEC2);
        }
        BeginShaderMode(shader);
        return true;
    }
    void end() { EndShaderMode(); }
};
inline Filter g_filter;
} // namespace wotm::post
