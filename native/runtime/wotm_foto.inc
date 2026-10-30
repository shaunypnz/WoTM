// wotm_foto.inc: pausa, avanco de quadro e fotos com nota (incluido por wotm_scene.inc, depois de wotm_live.inc e antes de
// wotm_vu1check.inc; escopo global de ps2_runtime.cpp).
//
// Teclas na janela (thread da apresentacao, wotmFotoKeys):
//  - F7 pausa e continua. A pausa segura a thread do EE no ponto de captura (wotmPauseBlock, chamado do wotm_overrides.cpp na
//    entrada de timerWaitForMinUpdateRate, logo depois do wotmCaptureFrame): o quadro na tela e o ultimo pronto e o seguinte nao
//    comecou. Filme (FMV) e som seguem por conta propria;
//  - F6, pausado, avanca um quadro do jogo;
//  - F8, com WOTM_FOTOS, tira uma foto: o jogo anda ate a checagem VU1 x nativo gravar um quadro inteiro (do hier(0, 0) ao seguinte,
//    wotm_vu1check.inc), para pausado nele (wotmFotoDone) e abre uma caixa de texto para a nota. A foto e o quadro seguinte ao que
//    estava na tela quando o F8 foi apertado; a tela passa a mostrar o fotografado enquanto a nota e escrita. Enter grava (vazia =
//    sem nota; o Esc fecharia a janela, e a tecla de saida do raylib). Sem WOTM_FOTOS, o F8 continua sendo so o disparo da checagem (WOTM_VU1CHECK).
// Cada foto vai para <pasta>/<AAAA-MM-DD_HHMMSS da sessao>/: foto_NNN.vu1snap (instantaneo: tools\vu1cmp.ps1 refaz a checagem e as
// imagens ref/nat/diff), foto_NNN_tela.png (o quadro nativo como estava na janela) e uma linha em notas.txt (numero, g_frame,
// currScreen, recursos carregados, nota). WOTM_FOTOS=1 usa <cwd>/../fotos (o run_live roda no discroot: <runtime>/fotos).
#include <condition_variable>
#include "wotm_syms.h"   // wotm::sym: enderecos do ELF por nome
#include <ctime>
#include <filesystem>
#include <fstream>

namespace wotm_foto
{
std::mutex m;
std::condition_variable cv;
bool paused = false;        // F7 / fim de foto
int steps = 0;              // F6: quadros a deixar andar antes de segurar de novo
bool photoRun = false;      // F8: o jogo anda ate a foto terminar, mesmo pausado
std::atomic<bool> blocked{false};        // a thread do EE esta parada aqui (vigia, overlay)
std::atomic<bool> tookFrame{false};      // houve parada no quadro em andamento (wotmPauseTookFrame)
std::string dir;            // pasta da sessao ("" = fotos desligadas); criada na primeira foto (makeDir)
unsigned next = 1;          // numero da proxima foto

// Foto pronta (thread do EE -> apresentacao)
struct Done { bool ready = false; unsigned n = 0; uint32_t frame = 0, screen = 0; std::string file; };
Done done;
// Estado da apresentacao
bool exported = false, noteOpen = false;
std::string note, msg;
double msgUntil = 0.0, photoAsked = 0.0;

Font font()
{
    // Consolas com Latin-1 (acentos da nota); sem o arquivo, a fonte padrao do raylib
    static Font f = [] {
        std::vector<int> cps;
        for (int c = 32; c < 256; ++c) cps.push_back(c);
        for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026}) cps.push_back(c);
        Font r = LoadFontEx(wotm::res::monoFontPath(), 32, cps.data(), int(cps.size()));
        if (r.texture.id == 0) return GetFontDefault();
        SetTextureFilter(r.texture, TEXTURE_FILTER_BILINEAR);
        return r;
    }();
    return f;
}

void say(const std::string &s, double secs = 3.0)
{
    msg = s;
    msgUntil = GetTime() + secs;
}

bool makeDir()
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) std::fprintf(stderr, "[foto] nao consegui criar %s: %s\n", dir.c_str(), ec.message().c_str());
    return !ec;
}

std::string name(unsigned n, const char *suffix)
{
    char b[64];
    std::snprintf(b, sizeof b, "foto_%03u%s", n, suffix);
    return (std::filesystem::path(dir) / b).string();
}
} // namespace wotm_foto

// Liga as fotos (wotmVu1CheckInstall, com WOTM_FOTOS): escolhe a pasta da sessao (criada so na primeira foto).
static bool wotmFotoInit(const char *base)
{
    namespace fs = std::filesystem;
    const fs::path root = (std::strcmp(base, "1") == 0) ? fs::path("..") / "fotos" : fs::path(base);
    const std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M%S", std::localtime(&t));
    std::error_code ec;
    wotm_foto::dir = fs::absolute(root / stamp, ec).string();
    if (ec) return false;
    std::fprintf(stderr, "[foto] F8 tira foto em %s (F7 pausa, F6 avanca um quadro)\n", wotm_foto::dir.c_str());
    return true;
}
static bool wotmFotosOn() { return !wotm_foto::dir.empty(); }
// Prefixo do instantaneo da foto em andamento (wotm_vu1check.inc, snap::begin)
static std::string wotmFotoSnapPath()
{
    wotm_foto::makeDir();
    return wotm_foto::name(wotm_foto::next, ".vu1snap");
}

// Thread do EE: segura enquanto pausado (sem passo nem foto em andamento). Devolve se parou.
bool wotmPauseBlock(PS2Runtime *rt)
{
    using namespace wotm_foto;
    std::unique_lock<std::mutex> lk(m);
    if (paused && steps > 0 && !photoRun) {
        --steps;
        return false;
    }
    if (!paused || photoRun) return false;
    blocked.store(true, std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    while (paused && steps == 0 && !photoRun && !(rt && rt->isStopRequested()))
        cv.wait_for(lk, std::chrono::milliseconds(50));
    if (steps > 0 && !photoRun) --steps;
    lk.unlock();
    // Os vblanks contados durante a pausa nao vencem na volta (shiftHostDeadlines zera o vblankDue). O prazo do proximo vblank NAO
    // anda: o relogio do vblank (EeSchedulerGame.cpp) segue rodando na pausa e o mantem perto de agora; somar a duracao da pausa
    // deixava o jogo parado por esse mesmo tempo depois de soltar (modo foto de um minuto = um minuto "congelado")
    (void)t0;
    if (rt) rt->eeScheduler().shiftHostDeadlines(std::chrono::steady_clock::duration::zero());
    blocked.store(false, std::memory_order_relaxed);
    tookFrame.store(true, std::memory_order_relaxed);
    return true;
}
// wotm_overrides.cpp, na entrada de timerWaitForMinUpdateRate: o quadro que acabou teve uma parada (o tempo dela nao conta)
bool wotmPauseTookFrame() { return wotm_foto::tookFrame.exchange(false, std::memory_order_relaxed); }
// Vigia (ps2_runtime.cpp): g_frame parado de proposito
bool wotmPaused() { return wotm_foto::blocked.load(std::memory_order_relaxed); }

// Thread do EE (wotm_vu1check.inc, entrada do hier(0, 0) seguinte ao quadro fotografado): a foto esta gravada; fica pausado aqui.
static void wotmFotoDone(const uint8_t *rdram, uint32_t frame, const std::string &file)
{
    using namespace wotm_foto;
    {
        std::lock_guard<std::mutex> lk(m);
        done.ready = true;
        done.n = next++;
        done.frame = frame;
        std::memcpy(&done.screen, rdram + wotm::sym::currScreen, 4);
        done.file = file;
        photoRun = false;
        paused = true;
        steps = 0;
    }
    std::fprintf(stderr, "[foto] %u: g_frame %u gravado em %s\n", done.n, frame, file.c_str());
}

// Modo foto (wotm_photo.inc): liga e desliga a mesma pausa do F7
void wotmPhotoPause(bool on)
{
    using namespace wotm_foto;
    std::lock_guard<std::mutex> lk(m);
    paused = on;
    steps = 0;
    cv.notify_all();
}

// Apresentacao, antes do desenho: F6/F7/F8 (a nota aberta fica com o teclado; o modo foto cuida da pausa sozinho)
static void wotmFotoKeys()
{
    using namespace wotm_foto;
    wotmPhotoWatchdog();
    if (noteOpen || wotmPhotoActive()) return;
    std::lock_guard<std::mutex> lk(m);
    if (IsKeyPressed(KEY_F7)) {
        paused = !paused;
        steps = 0;
        say(paused ? "PAUSED" : "resuming", paused ? 0.0 : 1.0);
    }
    if (IsKeyPressed(KEY_F6) && paused && !photoRun) ++steps;
    if (IsKeyPressed(KEY_F8)) {
        if (!wotmFotosOn()) {
            g_wotmShotKey = true;   // so a checagem (WOTM_VU1CHECK), como antes das fotos
        } else if (!photoRun) {
            g_wotmShotKey = true;
            photoRun = true;
            paused = true;
            steps = 0;
            photoAsked = GetTime();
            say("taking photo...", 5.0);
        }
    }
    // Sem hier(0, 0) (filme, carregamento) a checagem nao dispara: desiste e pausa no proximo quadro
    if (photoRun && GetTime() - photoAsked > 3.0) {
        photoRun = false;
        g_wotmShotKey = false;
        say("can't take a photo on this screen (no game frame)", 4.0);
    }
    cv.notify_all();
}

// Apresentacao, depois do desenho nativo (drewNative: a camada nativa desenhou o quadro): grava o PNG da foto, desenha a caixa da
// nota e o aviso de pausa.
static void wotmFotoOverlay(bool drewNative, const uint8_t *rdram)
{
    using namespace wotm_foto;
    Done d;
    {
        std::lock_guard<std::mutex> lk(m);
        d = done;
    }
    if (d.ready && !exported) {
        exported = true;
        const std::string png = name(d.n, "_tela.png");
        if (!(drewNative && wotm_live::exportFrame(png.c_str()))) {
            Image img = LoadImageFromScreen();
            ExportImage(img, png.c_str());
            UnloadImage(img);
        }
        noteOpen = true;
        note.clear();
        while (GetCharPressed()) {}   // o que foi digitado antes da caixa nao entra
    }
    const float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
    const Font f = font();
    if (noteOpen) {
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c >= 32 && note.size() < 400) {
                int n = 0;
                note += CodepointToUTF8(c, &n);
            }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !note.empty()) {
            size_t i = note.size() - 1;
            while (i > 0 && (uint8_t(note[i]) & 0xC0) == 0x80) --i;   // volta ao inicio do caractere UTF-8
            note.erase(i);
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            const std::string recursos = wotm_live::resources();
            std::ofstream o(std::filesystem::path(dir) / "notas.txt", std::ios::app | std::ios::binary);
            char head[160];
            std::snprintf(head, sizeof head, "foto_%03u  g_frame %u  currScreen %u  ", d.n, d.frame, d.screen);
            o << head << "recursos: " << (recursos.empty() ? "-" : recursos) << "\n    " << (note.empty() ? "(sem nota)" : note) << "\n";
            std::fprintf(stderr, "[foto] %u: nota \"%s\"\n", d.n, note.c_str());
            noteOpen = false;
            exported = false;
            {
                std::lock_guard<std::mutex> lk(m);
                done.ready = false;
            }
            say("photo " + std::to_string(d.n) + " saved (F7 resumes, F6 steps)", 3.0);
        }
    }
    if (noteOpen) {
        const float h = 96.f, y = sh - h - 16.f;
        DrawRectangle(16, int(y), int(sw) - 32, int(h), Fade(BLACK, 0.85f));
        DrawRectangleLines(16, int(y), int(sw) - 32, int(h), YELLOW);
        DrawTextEx(f, TextFormat("Photo %03u (g_frame %u, screen %u): what is wrong? Enter saves", d.n, d.frame, d.screen),
                   Vector2{28.f, y + 10.f}, 18.f, 0.f, YELLOW);
        const bool blink = std::fmod(GetTime(), 1.0) < 0.5;
        DrawTextEx(f, (note + (blink ? "_" : " ")).c_str(), Vector2{28.f, y + 44.f}, 22.f, 0.f, RAYWHITE);
        return;
    }
    if (wotmPhotoActive()) return;   // o modo foto tem a legenda dele
    bool isPaused, run;
    {
        std::lock_guard<std::mutex> lk(m);
        isPaused = paused;
        run = photoRun;
    }
    std::string line = GetTime() < msgUntil ? msg : "";
    if (isPaused && !run && line.empty()) line = wotmFotosOn() ? "PAUSED  F7 resumes  F6 steps one frame  F8 photo" : "PAUSED  F7 resumes  F6 steps one frame";
    if (!line.empty()) {
        if (isPaused && !run && rdram) {
            uint32_t g = 0;
            std::memcpy(&g, rdram + wotm::sym::g_frame, 4);
            line += "   (g_frame " + std::to_string(g) + ")";
        }
        const Vector2 sz = MeasureTextEx(f, line.c_str(), 20.f, 0.f);
        const float x = (sw - sz.x) * 0.5f, y = sh - sz.y - 24.f;   // rodape: no alto fica o HUD
        DrawRectangle(int(x - 10.f), int(y - 6.f), int(sz.x + 20.f), int(sz.y + 12.f), Fade(BLACK, 0.7f));
        DrawTextEx(f, line.c_str(), Vector2{x, y}, 20.f, 0.f, isPaused ? YELLOW : RAYWHITE);
    }
}
