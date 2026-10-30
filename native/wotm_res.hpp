// wotm_res.hpp: arquivos embutidos no executavel (icones de botao, fonte). No Windows, recursos do wotm.rc; no resto, arrays C
// que o CMake gera em wotm_gen/wotm_assets.inc a partir de native/assets.
#pragma once
#include <cstdint>
#include <cstring>

#ifdef _WIN32
// Sem windows.h (briga com os nomes do raylib): so o que le um recurso do proprio executavel
extern "C" __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
extern "C" __declspec(dllimport) void *__stdcall FindResourceA(void *module, const char *name, const char *type);
extern "C" __declspec(dllimport) void *__stdcall LoadResource(void *module, void *res);
extern "C" __declspec(dllimport) void *__stdcall LockResource(void *data);
extern "C" __declspec(dllimport) unsigned long __stdcall SizeofResource(void *module, void *res);
#else
#include "wotm_assets.inc"
#endif

namespace wotm::res
{
inline const unsigned char *find(const char *name, int &size)
{
#ifdef _WIN32
    void *mod = GetModuleHandleA(nullptr);
    void *res = FindResourceA(mod, name, reinterpret_cast<const char *>(static_cast<uintptr_t>(10)));   // RT_RCDATA
    void *h = res ? LoadResource(mod, res) : nullptr;
    size = res ? static_cast<int>(SizeofResource(mod, res)) : 0;
    return h ? static_cast<const unsigned char *>(LockResource(h)) : nullptr;
#else
    if (std::strcmp(name, "WOTM_PROMPTS") == 0) { size = int(sizeof kAssetPrompts); return kAssetPrompts; }
    if (std::strcmp(name, "FONT_OSWALD") == 0) { size = int(sizeof kAssetOswald); return kAssetOswald; }
    size = 0;
    return nullptr;
#endif
}
// Fonte monoespacada do texto de depuracao; se nao existir, fica a fonte do raylib
inline const char *monoFontPath()
{
#ifdef _WIN32
    return "C:/Windows/Fonts/consola.ttf";
#else
    return "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
#endif
}
} // namespace wotm::res
