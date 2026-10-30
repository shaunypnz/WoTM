// wotm_pad.inc: que controles e o teclado alimentam cada porta do jogo (porta 0 = jogador 1, porta 1 = jogador 2).
// Incluido por wotm_live.inc (escopo global de ps2_runtime.cpp). O runtime (ps2_pad.cpp) consulta g_ps2PadDeviceMap a cada leitura.
// Os controles conectados entram na ordem do raylib (o primeiro conectado e o jogador 1, o segundo o jogador 2), sem buracos:
// um controle desplugado passa a vez ao seguinte.
//   Padrao: o teclado e o mesmo jogador do primeiro controle (jogador 1).
//   WOTM_KBSEPARATE=1: o teclado e um dispositivo a parte, depois dos controles (sem controle, jogador 1; com um, jogador 2;
//   com dois, sobra).
//   Escolha de controles do multijogador (wotm_padsel.inc): enquanto g_manual, o jogador n recebe so os dispositivos que entraram
//   na coluna dele (g_manualDevs[n], mesmos bits do mapa); os que ficaram de fora nao jogam.
//   3 e 4 jogadores (WOTM_PLAYERS, wotm_split4.inc): o scePadRead do slot 1 de uma porta (jogadores 3 e 4) chega aqui com a porta
//   do slot 0 e g_portBase 2; o jogador e porta + base, com o controle de mesma ordem.
// Teclas e botoes de cada botao do PS2: o mapa do wotm_settings.hpp (aba CONTROLS do painel), instalado no ps2_pad.cpp do fork
// (g_ps2PadKeyboard, g_ps2PadGamepadButtons); os eixos dos gamepads seguem fixos.
namespace wotm_pad
{
static std::atomic<bool> g_manual{false};
static std::atomic<uint32_t> g_manualDevs[4] = {};
static std::atomic<int> g_portBase{0};

// Dispositivos do jogador `port` (0..3)
static uint32_t devicesOf(int port)
{
    const bool kbSeparate = wotm::settings::on(wotm::settings::KbSeparate);   // opcao ao vivo (wotm_settings.hpp)
    int pads[PS2_PAD_MAX_GAMEPADS];
    int n = 0;
    for (int i = 0; i < PS2_PAD_MAX_GAMEPADS; ++i)
        if (IsGamepadAvailable(i))
            pads[n++] = i;

    uint32_t devs = 0;
    if (g_manual.load(std::memory_order_relaxed))
        devs = port >= 0 && port < 4 ? g_manualDevs[port].load(std::memory_order_relaxed) : 0u;
    else
    {
        if (port >= 0 && port < n)
            devs = 1u << pads[port];
        if (kbSeparate ? port == n : port == 0)
            devs |= PS2_PAD_DEV_KEYBOARD;
    }

    // uma linha por jogador quando o que o alimenta muda (inicio, controle plugado ou desplugado)
    static uint32_t last[4] = {~0u, ~0u, ~0u, ~0u};
    if (port >= 0 && port < 4 && devs != last[port])
    {
        last[port] = devs;
        std::string what;
        for (int i = 0; i < PS2_PAD_MAX_GAMEPADS; ++i)
            if (devs & (1u << i))
                what += "controle " + std::to_string(i) + " (" + GetGamepadName(i) + ")";
        if (devs & PS2_PAD_DEV_KEYBOARD)
            what += what.empty() ? "teclado" : " + teclado";
        std::fprintf(stderr, "[pad] jogador %d: %s\n", port + 1, what.empty() ? "nenhum dispositivo" : what.c_str());
    }
    return devs;
}
static uint32_t deviceMap(int port) { return devicesOf(port + g_portBase.load(std::memory_order_relaxed)); }

// ------------------------------------------------------------------ mapa de teclas e botoes (wotm_settings.hpp, aba CONTROLS)
namespace st = wotm::settings;
// Rodinha como botao: cada passo (somado pela janela em mouseFrame) vira um toque de uma leitura do pad, com uma leitura solta
// depois, para a borda contar uma vez por passo. Avanca uma vez por leitura do teclado (keyboard).
static int g_wheelHeld = 0;   // +1 / -1 durante o toque, 0 solto
static int g_wheelGap = 0;
static void wheelStep()
{
    if (g_wheelHeld)
    {
        g_wheelHeld = 0;
        g_wheelGap = 1;
        return;
    }
    if (g_wheelGap > 0)
    {
        --g_wheelGap;
        return;
    }
    int w = wotm::mouse::g_wheel.load(std::memory_order_relaxed);
    while (w != 0 && !wotm::mouse::g_wheel.compare_exchange_weak(w, w > 0 ? w - 1 : w + 1, std::memory_order_relaxed)) {}
    if (w != 0)
        g_wheelHeld = w > 0 ? 1 : -1;
}
// Tecla, botao do mouse (kMouseBase + MouseButton) ou passo da rodinha (kWheelUp/kWheelDown) do mapa do teclado
inline bool inputDown(int code)
{
    if (code <= 0) return false;
    if (code == st::kWheelUp) return g_wheelHeld > 0;
    if (code == st::kWheelDown) return g_wheelHeld < 0;
    if (code >= st::kMouseBase && code < st::kMouseBase + 7) return IsMouseButtonDown(code - st::kMouseBase);
    return IsKeyDown(code);
}
inline bool kbDown(st::Bind b)
{
    const uint32_t v = st::kb(b);
    return inputDown(int(v & 0xFFFF)) || inputDown(int(v >> 16));
}
// Jogador (0..3) que o gamepad `pad` alimenta, pela regra do deviceMap: na escolha manual, a coluna em que ele entrou; senao, a ordem
// dos conectados. Fora disso (ou sem gamepad), o jogador 1. Escolhe o mapa de botoes dele (wotm_settings.hpp, um por jogador).
inline int playerOfPad(int pad)
{
    if (pad < 0 || pad >= PS2_PAD_MAX_GAMEPADS)
        return 0;
    if (g_manual.load(std::memory_order_relaxed))
    {
        for (int p = 0; p < 4; ++p)
            if (g_manualDevs[p].load(std::memory_order_relaxed) & (1u << pad)) return std::min(p, st::kPadPlayers - 1);
        return 0;
    }
    int n = 0;
    for (int i = 0; i < PS2_PAD_MAX_GAMEPADS; ++i)
        if (IsGamepadAvailable(i))
        {
            if (i == pad) return std::min(n, st::kPadPlayers - 1);
            ++n;
        }
    return 0;
}
inline bool padDown(int pad, st::Bind b, int player)
{
    const int btn = st::pad(b, player);
    return btn > 0 && IsGamepadButtonDown(pad, btn);
}

// Nos menus: o gancho do screenGetInput (wotm_mainmenu.inc) marca a hora a cada quadro de uma tela do menu
static std::atomic<double> g_menuTime{-1.0};
inline void markMenuFrame() { g_menuTime.store(GetTime(), std::memory_order_relaxed); }
inline bool inMenus() { return GetTime() - g_menuTime.load(std::memory_order_relaxed) < 0.25; }

// Teclado -> botoes do PS2 (bits ativos em 0 de btns) e analogico esquerdo (data[6] x, data[7] y), a cada leitura do pad do jogador
// do teclado. Acoes do teclado (wotm_settings.hpp), na configuracao 0 do jogo (CONTROL TYPE A):
//   lock-on alternado ligado (WOTM_LOCKTOGGLE, o mod troca os botoes; wotm_overrides.cpp): trava R1, defende L1, lados L2/R2;
//   desligado (os botoes do jogo): trava L1+R1 segurados, defende R2, lados L1/R1.
// Troca de alvo: pedido direto ao lock-on (wotm_mouse.hpp), na borda da tecla; o direcional nao entra (solto, ele andaria).
// Esc no Start: nos menus vale triangulo (voltar). Andar (Alt): o analogico vai a ~40% do curso.
// Nos menus o mouse nao passa pelo mapa (etapa 9): direito volta (triangulo), rodinha sobe e desce (direcional), esquerdo confirma (X),
// menos sobre um menu nativo que acha o item sob o cursor (wotm_mainmenu.inc: o clique e dele).
static void keyboard(uint16_t &btns, uint8_t *data)
{
    wheelStep();
    const bool menus = inMenus();
    const bool mouseMenu = menus || wotm::mouse::inGameMenu();   // menus do shell ou tela do jogo com mouse (pausa)
    auto down = [mouseMenu](st::Bind b) {
        if (!mouseMenu) return kbDown(b);
        const uint32_t v = st::kb(b);
        const int a = int(v & 0xFFFF), c = int(v >> 16);
        return (a < st::kMouseBase && inputDown(a)) || (c < st::kMouseBase && inputDown(c));
    };
    for (int b = 0; b < st::kBinds; ++b)
        if (st::kBindMask[b] && down(st::Bind(b)))
        {
            uint16_t mask = st::kBindMask[b];
            if (b == st::BStart && menus && IsKeyDown(KEY_ESCAPE))
            {
                const uint32_t v = st::kb(st::BStart);   // Esc no Start: so ele apertado vira voltar
                const bool other = (int(v & 0xFFFF) != KEY_ESCAPE && inputDown(int(v & 0xFFFF))) ||
                                   (int(v >> 16) != KEY_ESCAPE && inputDown(int(v >> 16)));
                if (!other) mask = 0x1000;   // triangulo
            }
            btns &= static_cast<uint16_t>(~mask);
        }
    if (mouseMenu && !wotm::mouse::g_captured.load(std::memory_order_relaxed))
    {
        uint16_t m = 0;
        // clique num item de uma tela do jogo (wotm_pausemenu.inc): o X por duas leituras, com o item ja escolhido
        if (wotm::mouse::nowMs() > wotm::mouse::g_crossReqUntil.load(std::memory_order_relaxed))
            wotm::mouse::g_crossReq.store(0, std::memory_order_relaxed);   // pedido velho (wotm_mouse.hpp, requestCross)
        int req = wotm::mouse::g_crossReq.load(std::memory_order_relaxed);
        while (req > 0 && !wotm::mouse::g_crossReq.compare_exchange_weak(req, req - 1, std::memory_order_relaxed)) {}
        if (req > 0) m |= 0x4000;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) m |= 0x1000;                                  // triangulo
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !wotm::mouse::claimed()) m |= 0x4000;        // X
        const bool side = wotm::mouse::sideActive();   // tela que anda para os lados (wotm_gamescreens.inc)
        if (g_wheelHeld && side && wotm::mouse::g_sideWheel.load(std::memory_order_relaxed))
            m |= g_wheelHeld > 0 ? 0x0080 : 0x0020;                                              // esquerda / direita
        else if (g_wheelHeld)
            m |= g_wheelHeld > 0 ? 0x0010 : 0x0040;                                              // cima / baixo
        if (side && IsKeyDown(KEY_Q)) m |= 0x0080;                                               // Q / E: esquerda / direita
        if (side && IsKeyDown(KEY_E)) m |= 0x0020;
        btns &= static_cast<uint16_t>(~m);
    }
    constexpr uint16_t kL1 = 0x0400, kR1 = 0x0800, kL2 = 0x0100, kR2 = 0x0200;
    const bool toggle = st::on(st::LockToggle);
    uint16_t act = 0;
    if (down(st::BLock)) act |= toggle ? kR1 : uint16_t(kL1 | kR1);
    if (down(st::BBlock)) act |= toggle ? kL1 : kR2;
    if (down(st::BStrafeL)) act |= toggle ? kL2 : kL1;
    if (down(st::BStrafeR)) act |= toggle ? kR2 : kR1;
    btns &= static_cast<uint16_t>(~act);

    static bool prevNext = false, prevPrev = false;
    const bool next = kbDown(st::BTargetNext), prev = kbDown(st::BTargetPrev);
    const int kp = wotm::mouse::g_kbPlayer.load(std::memory_order_relaxed);
    if (kp >= 0 && kp < 4 && !menus && ((next && !prevNext) || (prev && !prevPrev)))
        wotm::mouse::g_targetReq[kp].fetch_add(next && !prevNext ? 1 : -1, std::memory_order_relaxed);
    prevNext = next;
    prevPrev = prev;

    // analogico esquerdo (so com alguma das teclas dele apertada; senao fica o valor do gamepad ou o centro)
    const bool l = down(st::BStickLeft), r = down(st::BStickRight), u = down(st::BStickUp), d = down(st::BStickDown);
    const uint8_t lo = down(st::BWalk) ? 0x4D : 0x00, hi = static_cast<uint8_t>(0xFF - lo);
    if (l != r) data[6] = l ? lo : hi;
    if (u != d) data[7] = u ? lo : hi;

    static const bool log = std::getenv("WOTM_KBLOG") != nullptr;   // uma linha por mudanca do que o teclado manda
    static uint16_t lastB = 0xFFFF;
    static uint8_t lastX = 0x80, lastY = 0x80;
    if (log && (btns != lastB || data[6] != lastX || data[7] != lastY))
    {
        std::fprintf(stderr, "[kb] t=%.2f botoes %04x analogico %02x,%02x%s\n", GetTime(), unsigned(static_cast<uint16_t>(~btns)), data[6],
                     data[7], menus ? " (menus)" : "");
        lastB = btns;
        lastX = data[6];
        lastY = data[7];
    }
}
// MenuConfirm = 1 nos menus (gamepads): o botao do X passa a dar triangulo (voltar, no jogo), o da bola da X (confirmar) e o do
// triangulo da bola. Com o mapa padrao: o da direita confirma e o de baixo volta, como nos consoles da Nintendo.
inline bool menuSwap() { return st::get(st::MenuConfirm) == 1 && inMenus(); }
// Botao do PS2 que o botao fisico do botao b do mapa produz agora (a troca dos menus)
inline st::Bind effectiveBind(st::Bind b)
{
    if (!menuSwap()) return b;
    return b == st::BCross ? st::BTriangle : b == st::BCircle ? st::BCross : b == st::BTriangle ? st::BCircle : b;
}
// O inverso: qual botao do mapa da o botao do PS2 b agora (os icones mostram esse)
inline st::Bind sourceBind(st::Bind b)
{
    if (!menuSwap()) return b;
    return b == st::BTriangle ? st::BCross : b == st::BCross ? st::BCircle : b == st::BCircle ? st::BTriangle : b;
}
static uint16_t gamepadButtons(int pad)
{
    uint16_t m = 0;
    const int player = playerOfPad(pad);
    for (int b = 0; b < st::kBinds; ++b)
        if (st::bindOnPad(st::Bind(b)) && padDown(pad, st::Bind(b), player))
            m |= st::kBindMask[effectiveBind(st::Bind(b))];
    return m;
}

static int playerOfPort(int port) { return port + g_portBase.load(std::memory_order_relaxed); }

// Jogador (0..3) que o teclado alimenta, pela regra do deviceMap (-1: nenhum, na escolha manual sem o teclado ou com ele sobrando)
inline int keyboardPlayer()
{
    if (g_manual.load(std::memory_order_relaxed))
    {
        for (int p = 0; p < 4; ++p)
            if (g_manualDevs[p].load(std::memory_order_relaxed) & PS2_PAD_DEV_KEYBOARD) return p;
        return -1;
    }
    if (!st::on(st::KbSeparate)) return 0;
    int n = 0;
    for (int i = 0; i < PS2_PAD_MAX_GAMEPADS; ++i)
        if (IsGamepadAvailable(i)) ++n;
    return n < 4 ? n : -1;
}

// Mouse na camera moderna (wotm_mouse.hpp), a cada apresentacao na thread da janela: cursor preso enquanto a orbita do jogador do
// teclado dirige (aviso da thread do EE) e a janela tem o foco; preso, o deslocamento vai para a orbita. WOTM_MOUSECAM=0 desliga e
// =1 liga; sem a variavel, desliga se o RustDesk roda nesta maquina: o cursor preso nao anda pelo controle remoto e a sessao fica
// sem mouse. F10 alterna durante o jogo.
static bool rustdeskRunning()
{
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator("/proc", ec))
    {
        std::ifstream f(e.path() / "comm");
        std::string name;
        if (f && std::getline(f, name) && name.rfind("rustdesk", 0) == 0)
            return true;
    }
    return false;
}
static void mouseFrame()
{
    namespace ms = wotm::mouse;
    static bool off = [] {
        const char *e = std::getenv("WOTM_MOUSECAM");
        if (e && *e)
            return e[0] == '0';
        const bool remote = rustdeskRunning();
        if (remote)
            std::fprintf(stderr, "[mouse] RustDesk nesta maquina: cursor nao fica preso (F10 alterna)\n");
        return remote;
    }();
    if (IsKeyPressed(KEY_F10))
    {
        off = !off;
        std::fprintf(stderr, "[mouse] F10: camera pelo mouse %s\n", off ? "desligada" : "ligada");
    }
    ms::g_kbPlayer.store(keyboardPlayer(), std::memory_order_relaxed);
    const bool want = !off && ms::wanted() && !ms::inGameMenu() && IsWindowFocused();   // menu do jogo na fase (pausa, "salvar?"): solto
    static bool captured = false;
    static int skip = 0;
    if (want != captured)
    {
        captured = want;
        if (want)
        {
            DisableCursor();
            skip = 2;   // o DisableCursor recentra o cursor: o primeiro deslocamento e esse salto
        }
        else
            EnableCursor();
        ms::clear();
        ms::g_captured.store(want, std::memory_order_relaxed);
        std::fprintf(stderr, "[mouse] cursor %s (aviso ha %lld ms, foco %d)\n", want ? "preso (camera)" : "solto",
                     static_cast<long long>(ms::nowMs() - (ms::g_wantUntil.load(std::memory_order_relaxed) - 150)), IsWindowFocused() ? 1 : 0);
    }
    if (!captured)
        return;   // solto: rodinha e cliques no pointerFrame (menus)
    if (const float w = GetMouseWheelMove(); w != 0.f)
        ms::g_wheel.fetch_add(w > 0.f ? 1 : -1, std::memory_order_relaxed);
    const Vector2 d = GetMouseDelta();
    if (skip > 0)
    {
        --skip;
        return;
    }
    if (d.x != 0.f || d.y != 0.f)
        ms::add(d.x, d.y);
}

// Mouse nos menus (wotm_mouse.hpp), a cada apresentacao, com o retangulo `dst` em que o quadro nativo (rw x rh) foi para a janela:
// com o cursor solto e a janela com o foco, onde ele esta no quadro, se andou, os cliques (esquerdo e direito) e a rodinha.
static void pointerFrame(const Rectangle &dst, int rw, int rh)
{
    namespace ms = wotm::mouse;
    ms::Pointer &p = ms::g_ptr;
    p.rw = rw;
    p.rh = rh;
    if (ms::g_captured.load(std::memory_order_relaxed) || !IsWindowFocused() || dst.width <= 0.f || dst.height <= 0.f)
    {
        static int lastWhy = -1;
        const int why = ms::g_captured.load(std::memory_order_relaxed) ? 1 : !IsWindowFocused() ? 2 : 3;
        if (std::getenv("WOTM_MENUMOUSELOG") && why != lastWhy)
            std::fprintf(stderr, "[menumouse] ponteiro desligado (%s)\n", why == 1 ? "cursor preso" : why == 2 ? "janela sem foco" : "sem quadro");
        lastWhy = why;
        p.in = false;
        ms::publishPointer(false, 0.f, 0.f, rw, rh);
        return;
    }
    const Vector2 m = GetMousePosition();
    const float x = (m.x - dst.x) * float(rw) / dst.width, y = (m.y - dst.y) * float(rh) / dst.height;
    const bool in = x >= 0.f && y >= 0.f && x < float(rw) && y < float(rh);
    static const bool log = std::getenv("WOTM_MENUMOUSELOG") != nullptr;
    if (log && in != p.in)
        std::fprintf(stderr, "[menumouse] cursor %s do quadro (janela %.0f,%.0f, quadro %.0f,%.0f de %dx%d)\n", in ? "dentro" : "fora", m.x, m.y,
                     x, y, rw, rh);
    if (in && p.in && (std::fabs(x - p.x) > 0.5f || std::fabs(y - p.y) > 0.5f))
        ms::g_moveSeq.fetch_add(1, std::memory_order_relaxed);
    p.x = x;
    p.y = y;
    p.in = in;
    ms::publishPointer(in, x, y, rw, rh);
    if (!in)
        return;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ms::click(ms::g_clickL);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) ms::click(ms::g_clickR);
    if (const float w = GetMouseWheelMove(); w != 0.f)
        ms::g_wheel.fetch_add(w > 0.f ? 1 : -1, std::memory_order_relaxed);
}

static const bool s_installed = (g_ps2PadDeviceMap = &deviceMap, g_ps2PadKeyboard = &keyboard, g_ps2PadGamepadButtons = &gamepadButtons,
                                 g_ps2PadPlayerOfPort = &playerOfPort, true);
}

void wotmPadSetPortBase(int base) { wotm_pad::g_portBase.store(base, std::memory_order_relaxed); }
