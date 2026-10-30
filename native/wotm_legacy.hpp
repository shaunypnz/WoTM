// wotm_legacy.hpp: aba LEGACY do painel "Recompiled options": o registro do motor original deste fork (legacy/ps2-web-recomp, anterior a
// ele) e o lancamento dele, se estiver construido (tools/linux/build_legacy.sh). O lancamento troca este processo pelo do outro motor.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif

#if __has_include("wotm_legacy.gen.h")
#include "wotm_legacy.gen.h"
#endif
#ifndef WOTM_LEGACY_COMMIT
#define WOTM_LEGACY_COMMIT "UNKNOWN"
#endif

namespace wotm::legacy
{
struct Row { const char *label, *value, *hint; };
inline constexpr Row kRows[] = {
    {"ENGINE", "PS2-WEB-RECOMP", "OUR OWN LINUX PORT OF THIS GAME, BUILT BEFORE THIS FORK. IT IS KEPT IN THE LEGACY FOLDER"},
    {"PREVIEWS", "0.1.0 TO 0.3.0", "THREE RELEASES, WITH THEIR HISTORY IN THIS REPOSITORY"},
    {"HOW IT DRAWS", "VULKAN", "A SEPARATE RECOMPILER THAT RUNS THE ORIGINAL GRAPHICS PIPELINE AND SENDS IT TO VULKAN"},
    {"LEGACY COMMIT", WOTM_LEGACY_COMMIT, "THE LAST CHANGE TO THE LEGACY FOLDER WHEN THIS BUILD WAS MADE"},
};
inline constexpr int kInfoRows = int(sizeof kRows / sizeof kRows[0]), kRunRow = kInfoRows, kRowCount = kInfoRows + 1;

inline std::filesystem::path launcher()
{
    std::error_code ec;
    if (const char *d = std::getenv("WOTM_LEGACY_DIR"); d && *d)
        return std::filesystem::path(d) / "play-pc.sh";
    return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path() / "legacy" / "play-pc.sh";
}
inline bool installed()
{
    std::error_code ec;
    return std::filesystem::exists(launcher(), ec);
}
inline void launch()
{
#ifndef _WIN32
    const std::string p = launcher().string();
    std::fprintf(stderr, "[legacy] abrindo o motor original: %s\n", p.c_str());
    std::fflush(stderr);
    unsetenv("LD_LIBRARY_PATH");
    execl("/bin/sh", "sh", p.c_str(), static_cast<char *>(nullptr));
    std::fprintf(stderr, "[legacy] nao abriu\n");
#endif
}
} // namespace wotm::legacy
