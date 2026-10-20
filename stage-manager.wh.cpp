// ==WindhawkMod==
// @id              stage-manager
// @name            Stage Manager
// @description     macOS-like Stage Manager: the active window centered, the other windows as clickable previews on the left. Toggle with a keyboard shortcut (Ctrl+Win by default) or a four-finger sideways touchpad swipe
// @version         1.0
// @author          custom
// @include         windhawk.exe
// @compilerOptions -ldwmapi -lgdi32 -luser32 -lshell32 -lole32 -ladvapi32 -ld3d11 -ldxgi -ldcomp -luuid
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Stage Manager

Inspired by Stage Manager on macOS.

When turned on, on the monitor of the active window:
* The active window is centered on the screen ("on stage").
* The other windows are minimized and shown as previews in a column on the
  left. Clicking a preview puts that window on stage, and the previous one goes
  to the column. Both are animated, in a way chosen in the settings: Overlay
  (the chosen window comes over the current one, which then goes to the
  column), Genie (as on macOS, one is drawn into its preview, the other out
  of it) or Shrink (the window in use shrinks where it is and then slides to
  the column behind the chosen one, which grows out of its preview). So are
  the windows in sight when Stage Manager is turned on.
* **Shift+click** on a preview adds that window to the stage, next to the one
  already there (up to two windows). The two share the area to the right of
  the column, side by side:
  * Drag the handle between them, or the inner edge of either window, to
    resize both at once.
  * Double-click the handle to swap them.
  * **Ctrl+Shift+click** on one of them sends it back to the column; the
    other one returns to its usual size.
  * Clicking a preview replaces only the window in use at the moment; the
    other stays put.
* A new window opened while Stage Manager is on goes on stage at the usual
  size, even if it opens maximized.
* Maximizing a stage window, or making it full screen, turns Stage Manager
  off.
* Activating a window in any other way (taskbar, Alt+Tab, opening a new app)
  also puts it on stage, in place of the window in use.

Turning it off puts every window back where it was.

Each virtual desktop has its own Stage Manager: turning it on applies to the
current desktop only, and switching to another desktop shows that desktop's
own state. Windows of different desktops are never mixed.

## How to toggle

* **Shortcut**, `Ctrl+Win` by default (configurable): press both and
  release them without pressing any other key. `Ctrl+Win+Left/Right` and the
  other Windows shortcuts keep working.
* **Four-finger swipe** to the left or right on a precision touchpad. Set
  the Windows four-finger swipe gestures to "Nothing" (Settings > Bluetooth &
  devices > Touchpad > Three- and four-finger gestures), otherwise Windows also
  switches virtual desktops.

## Notes

* Windows minimized before Stage Manager was turned on are left alone.
* Windows of programs running as administrator can only be managed if Windhawk
  is running as administrator too.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- shortcut: Ctrl+Win
  $name: Atalho de teclado
  $description: >-
    Combinação para ligar/desligar. Modificadores: Win, Ctrl, Alt, Shift.
    Tecla: letra, número, F1-F24 ou Space. Só modificadores (ex.: Ctrl+Win)
    também vale: dispara ao soltar, se nenhuma outra tecla foi apertada junto.
    Deixe vazio para desativar.
- touchpadGesture: off
  $name: Gesto no touchpad
  $options:
  - swipe: Deslizar 4 dedos para a direita ou esquerda
  - off: Desativado
- gestureDistance: 12
  $name: Distância do deslize (% da largura do touchpad)
- cardWidth: 200
  $name: Largura das miniaturas (px)
- iconSize: 40
  $name: Tamanho dos ícones (%)
  $description: 100% é o tamanho grande da versão anterior.
- maxCards: 6
  $name: Máximo de miniaturas na coluna
- stageSizing: inches
  $name: Tamanho da janela central
  $options:
  - inches: Tamanho fixo em polegadas (diagonal)
  - fit: Manter o tamanho da janela (reduzir só se não couber)
  - fill: Sempre ocupar o tamanho máximo
- stageInches: "5"
  $name: Tamanho fixo da janela central (polegadas, diagonal)
  $description: >-
    Usado com "Tamanho fixo em polegadas". A janela tem a mesma proporção da
    tela. Aceita decimais, ex.: 7,5
- screenInches: "14"
  $name: Tamanho da sua tela (polegadas, diagonal)
  $description: Usado para converter polegadas em pixels.
- stageSize: 90
  $name: Tamanho máximo da janela central (% da área livre)
- switchAnimation: overlay
  $name: Animação de troca de janelas
  $description: >-
    Como a janela escolhida na coluna vai para o centro, e a do centro vai
    para a coluna.
  $options:
  - overlay: Sobreposição (a escolhida vem por cima da atual, que depois vai para a coluna)
  - genie: Genie (as janelas são "sugadas" para a coluna e saem dela)
  - shrink: Encolher (a do centro encolhe e vai para a coluna por trás, enquanto a escolhida cresce saindo da coluna)
  - off: Sem animação
- genieDuration: 350
  $name: Duração da animação de troca (ms)
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

////////////////////////////////////////////////////////////////////////////////
// Settings

enum class GestureMode { Off, Swipe };
enum class StageSizing { Inches, Fit, Fill };
// How windows go between the stage and the column. More can be added: each
// is a way of moving a window's picture (see genie::Motion).
enum class SwitchAnimation { Off, Overlay, Genie, Shrink };

// Accepts both "7.5" and "7,5".
double ReadDoubleSetting(PCWSTR name, double fallback) {
    PCWSTR text = Wh_GetStringSetting(name);
    std::wstring value = text;
    Wh_FreeStringSetting(text);

    std::replace(value.begin(), value.end(), L',', L'.');
    WCHAR* end = nullptr;
    double result = wcstod(value.c_str(), &end);
    if (end == value.c_str() || !(result > 0)) {
        return fallback;
    }
    return result;
}

struct Settings {
    UINT hotkeyMods = 0;
    UINT hotkeyVk = 0;
    GestureMode gesture = GestureMode::Swipe;
    int gestureDistance = 12;
    int cardWidth = 200;
    int maxCards = 6;
    int iconSize = 40;
    int stageSize = 90;
    StageSizing stageSizing = StageSizing::Inches;
    double stageInches = 5;
    double screenInches = 14;
    SwitchAnimation animation = SwitchAnimation::Overlay;
    // Any animation at all.
    bool genieEnabled = true;
    int genieDuration = 350;
} g_settings;

std::wstring TrimUpper(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) {
        s.erase(s.begin());
    }
    while (!s.empty() && iswspace(s.back())) {
        s.pop_back();
    }
    for (auto& c : s) {
        c = towupper(c);
    }
    return s;
}

bool ParseHotkey(PCWSTR text, UINT* mods, UINT* vk) {
    *mods = 0;
    *vk = 0;

    std::wstring s = text;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(L'+', start);
        if (end == std::wstring::npos) {
            end = s.size();
        }
        std::wstring token = TrimUpper(s.substr(start, end - start));
        start = end + 1;

        if (token.empty()) {
            continue;
        }
        if (token == L"WIN" || token == L"WINDOWS") {
            *mods |= MOD_WIN;
        } else if (token == L"CTRL" || token == L"CONTROL") {
            *mods |= MOD_CONTROL;
        } else if (token == L"ALT") {
            *mods |= MOD_ALT;
        } else if (token == L"SHIFT") {
            *mods |= MOD_SHIFT;
        } else if (token.size() == 1 && iswalnum(token[0])) {
            *vk = token[0];
        } else if (token.size() >= 2 && token[0] == L'F' &&
                   iswdigit(token[1])) {
            int n = _wtoi(token.c_str() + 1);
            if (n >= 1 && n <= 24) {
                *vk = VK_F1 + n - 1;
            }
        } else if (token == L"SPACE") {
            *vk = VK_SPACE;
        } else {
            return false;
        }
    }

    return *vk != 0 || *mods != 0;
}

void LoadSettings() {
    PCWSTR hotkey = Wh_GetStringSetting(L"shortcut");
    if (!ParseHotkey(hotkey, &g_settings.hotkeyMods, &g_settings.hotkeyVk)) {
        if (*hotkey) {
            Wh_Log(L"Invalid hotkey: %s", hotkey);
        }
        g_settings.hotkeyMods = 0;
        g_settings.hotkeyVk = 0;
    }
    Wh_FreeStringSetting(hotkey);

    PCWSTR gesture = Wh_GetStringSetting(L"touchpadGesture");
    g_settings.gesture = wcscmp(gesture, L"off") == 0 ? GestureMode::Off
                                                      : GestureMode::Swipe;
    Wh_FreeStringSetting(gesture);

    g_settings.gestureDistance =
        std::clamp(Wh_GetIntSetting(L"gestureDistance"), 3, 80);
    g_settings.cardWidth = std::clamp(Wh_GetIntSetting(L"cardWidth"), 80, 500);
    g_settings.maxCards = std::clamp(Wh_GetIntSetting(L"maxCards"), 1, 20);
    g_settings.iconSize = std::clamp(Wh_GetIntSetting(L"iconSize"), 10, 200);
    g_settings.stageSize = std::clamp(Wh_GetIntSetting(L"stageSize"), 30, 100);

    PCWSTR stageSizing = Wh_GetStringSetting(L"stageSizing");
    g_settings.stageSizing = wcscmp(stageSizing, L"fit") == 0 ? StageSizing::Fit
                             : wcscmp(stageSizing, L"fill") == 0
                                 ? StageSizing::Fill
                                 : StageSizing::Inches;
    Wh_FreeStringSetting(stageSizing);

    g_settings.stageInches =
        std::clamp(ReadDoubleSetting(L"stageInches", 5), 1.0, 100.0);
    g_settings.screenInches =
        std::clamp(ReadDoubleSetting(L"screenInches", 14), 5.0, 100.0);

    PCWSTR animation = Wh_GetStringSetting(L"switchAnimation");
    g_settings.animation = wcscmp(animation, L"off") == 0 ? SwitchAnimation::Off
                           : wcscmp(animation, L"genie") == 0
                               ? SwitchAnimation::Genie
                           : wcscmp(animation, L"shrink") == 0
                               ? SwitchAnimation::Shrink
                               : SwitchAnimation::Overlay;
    Wh_FreeStringSetting(animation);
    g_settings.genieEnabled = g_settings.animation != SwitchAnimation::Off;
    g_settings.genieDuration =
        std::clamp(Wh_GetIntSetting(L"genieDuration"), 100, 1500);
}

////////////////////////////////////////////////////////////////////////////////
// Helpers

constexpr UINT WM_APP_TOGGLE = WM_APP + 1;
constexpr UINT WM_APP_SWITCH = WM_APP + 2;
constexpr UINT WM_APP_ADOPT = WM_APP + 3;
constexpr UINT WM_APP_SETTINGS = WM_APP + 4;
constexpr UINT WM_APP_REMOVE = WM_APP + 5;
constexpr UINT WM_APP_GENIE_CARD = WM_APP + 6;
constexpr UINT WM_APP_MOUSEHOOK = WM_APP + 7;

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kZoomCheckTimer = 2;
constexpr UINT_PTR kAdoptRetryTimer = 3;
constexpr int kHotkeyId = 1;

constexpr DWORD kDwmWindowCornerPreference = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kDwmCornerDoNotRound = 1;         // DWMWCP_DONOTROUND

HWND g_msgWnd;

UINT MonitorDpi(HMONITOR monitor) {
    using GetDpiForMonitor_t = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static auto pGetDpiForMonitor = (GetDpiForMonitor_t)GetProcAddress(
        LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");

    UINT x = 96, y = 96;
    if (pGetDpiForMonitor && SUCCEEDED(pGetDpiForMonitor(monitor, 0, &x, &y))) {
        return x;
    }
    return 96;
}

bool IsCloaked(HWND hWnd) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &cloaked,
                                           sizeof(cloaked))) &&
           cloaked;
}

// A window that shows up in Alt+Tab, roughly.
bool IsCandidate(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd) || !IsWindowVisible(hWnd)) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId()) {
        return false;
    }

    LONG style = GetWindowLongW(hWnd, GWL_STYLE);
    LONG exStyle = GetWindowLongW(hWnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW) ||
        (exStyle & WS_EX_NOACTIVATE)) {
        return false;
    }

    if (GetWindow(hWnd, GW_OWNER) && !(exStyle & WS_EX_APPWINDOW)) {
        return false;
    }

    if (IsCloaked(hWnd) || GetWindowTextLengthW(hWnd) == 0) {
        return false;
    }

    WCHAR className[64];
    if (GetClassNameW(hWnd, className, ARRAYSIZE(className))) {
        static const PCWSTR kExcluded[] = {
            L"Progman",
            L"WorkerW",
            L"Shell_TrayWnd",
            L"Shell_SecondaryTrayWnd",
            L"Windows.UI.Core.CoreWindow",
            L"XamlExplorerHostIslandWindow",
        };
        for (PCWSTR excluded : kExcluded) {
            if (_wcsicmp(className, excluded) == 0) {
                return false;
            }
        }
    }

    return true;
}

void BringToFront(HWND hWnd) {
    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread =
        foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    DWORD currentThread = GetCurrentThreadId();

    bool attached = foregroundThread && foregroundThread != currentThread &&
                    AttachThreadInput(currentThread, foregroundThread, TRUE);

    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);

    if (attached) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }

    if (GetForegroundWindow() != hWnd) {
        SwitchToThisWindow(hWnd, TRUE);
    }
}

HICON GetWindowIcon(HWND hWnd) {
    DWORD_PTR icon = 0;
    SendMessageTimeoutW(hWnd, WM_GETICON, ICON_BIG, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &icon);
    if (!icon) {
        SendMessageTimeoutW(hWnd, WM_GETICON, ICON_SMALL2, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &icon);
    }
    if (!icon) {
        icon = GetClassLongPtrW(hWnd, GCLP_HICON);
    }
    if (!icon) {
        icon = GetClassLongPtrW(hWnd, GCLP_HICONSM);
    }
    if (!icon) {
        icon = (DWORD_PTR)LoadIconW(nullptr, IDI_APPLICATION);
    }
    return (HICON)icon;
}

////////////////////////////////////////////////////////////////////////////////
// Stage state

bool g_active;
HMONITOR g_monitor;
RECT g_workArea;
UINT g_dpi = 96;
// Windows on stage, left to right: one, or two side by side.
std::vector<HWND> g_stage;
// The stage window in use, the one a new window replaces.
HWND g_focused;
// How many windows the stage was last laid out for.
size_t g_laidOutCount;
// Windows in the column, most recently used first.
std::vector<HWND> g_strip;
// Where each managed window was before Stage Manager was turned on.
std::unordered_map<HWND, WINDOWPLACEMENT> g_saved;
// Width of the invisible resize borders around each window, measured while it
// wasn't minimized, to keep them out of its preview.
std::unordered_map<HWND, RECT> g_frameInsets;
// A picture of each window taken right before it was minimized into the
// column, already reduced to about twice the size of a card. The cards are
// drawn from it: DWM's own live thumbnails scale down coarsely, and can't be
// given a real perspective.
//
// The picture is also kept at its real size, for the windows that have a card:
// that's what's animated when the window comes back out of it.
struct Snapshot {
    int width = 0;
    int height = 0;
    std::vector<DWORD> pixels;  // BGRX, top-down
    // Changes whenever the picture does.
    unsigned serial = 0;
    int fullWidth = 0;
    int fullHeight = 0;
    std::vector<DWORD> full;
};

std::unordered_map<HWND, Snapshot> g_snapshots;

HWINEVENTHOOK g_winEventHooks[5];
HHOOK g_mouseHook;
bool g_swallowMouseUp;
// Windows that just appeared, until when: an app maximizing its new window
// isn't the user asking for full screen.
std::unordered_map<HWND, ULONGLONG> g_freshUntil;
// A window that became the foreground one but wasn't ready to go on stage
// (e.g. no title yet), and how many times it was looked at.
HWND g_pendingAdopt;
int g_adoptTries;

void RememberFrameInsets(HWND hWnd) {
    RECT windowRect, frame;
    if (IsIconic(hWnd) || !GetWindowRect(hWnd, &windowRect) ||
        FAILED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame,
                                     sizeof(frame)))) {
        return;
    }
    g_frameInsets[hWnd] = {frame.left - windowRect.left,
                           frame.top - windowRect.top,
                           windowRect.right - frame.right,
                           windowRect.bottom - frame.bottom};
}

int Scale(int value) {
    return MulDiv(value, g_dpi, 96);
}

int Margin() {
    return Scale(24);
}

void SavePlacement(HWND hWnd) {
    RememberFrameInsets(hWnd);
    if (g_saved.count(hWnd)) {
        return;
    }
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(hWnd, &wp)) {
        g_saved[hWnd] = wp;
    }
}

// The most a full-size picture kept for later may hold. Those of larger
// windows aren't kept: the reduced one is used in their place.
constexpr size_t kMaxKeptPixels = 3'000'000;

unsigned g_snapshotSerial;

HBITMAP CreateDib(int width, int height, void** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
}

// Copies the window's frame from what's on screen, if no other window covers
// any of it. Much quicker than asking the program to draw it (PrintWindow),
// which large windows of some programs take a good part of a second to do.
// Only valid while none of our animation windows are on screen.
bool GrabFromScreen(HWND hWnd, const RECT& frame, Snapshot* picture) {
    // Entirely on its screen, too.
    RECT overlap;
    MONITORINFO mi{sizeof(mi)};
    if (IsCloaked(hWnd) ||
        !GetMonitorInfoW(MonitorFromRect(&frame, MONITOR_DEFAULTTONEAREST),
                         &mi) ||
        !IntersectRect(&overlap, &frame, &mi.rcMonitor) ||
        !EqualRect(&overlap, &frame)) {
        return false;
    }

    HWND above = GetWindow(hWnd, GW_HWNDPREV);
    for (int i = 0; above; i++, above = GetWindow(above, GW_HWNDPREV)) {
        if (i > 1000) {
            return false;
        }
        if (!IsWindowVisible(above) || IsIconic(above) || IsCloaked(above)) {
            continue;
        }
        // The cards: they're beside the stage, never over it.
        DWORD pid = 0;
        GetWindowThreadProcessId(above, &pid);
        if (pid == GetCurrentProcessId()) {
            continue;
        }
        RECT rect;
        if (GetWindowRect(above, &rect) &&
            IntersectRect(&overlap, &rect, &frame)) {
            return false;
        }
    }

    int width = frame.right - frame.left;
    int height = frame.bottom - frame.top;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDib(width, height, &bits);
    HDC dc = CreateCompatibleDC(nullptr);
    HDC screenDc = GetDC(nullptr);
    bool ok = false;
    if (bitmap && dc && screenDc) {
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        ok = BitBlt(dc, 0, 0, width, height, screenDc, frame.left, frame.top,
                    SRCCOPY) != FALSE;
        GdiFlush();
        if (ok) {
            const DWORD* pixels = (const DWORD*)bits;
            picture->width = width;
            picture->height = height;
            picture->pixels.assign(pixels, pixels + (size_t)width * height);
        }
        SelectObject(dc, oldBitmap);
    }
    if (screenDc) {
        ReleaseDC(nullptr, screenDc);
    }
    if (dc) {
        DeleteDC(dc);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    return ok;
}

// Takes the window's picture at its real size, without the invisible resize
// borders, and gives the visible frame it's a picture of. The window must be
// visible and not minimized. `fromScreen`: may copy it from the screen.
bool GrabWindow(HWND hWnd, Snapshot* picture, RECT* frame,
                bool fromScreen = false) {
    RECT windowRect;
    if (!IsWindowVisible(hWnd) || IsIconic(hWnd) ||
        !GetWindowRect(hWnd, &windowRect)) {
        return false;
    }

    int fullWidth = windowRect.right - windowRect.left;
    int fullHeight = windowRect.bottom - windowRect.top;
    if (fullWidth <= 0 || fullHeight <= 0) {
        return false;
    }

    RECT dwmFrame;
    if (FAILED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &dwmFrame, sizeof(dwmFrame))) ||
        !IntersectRect(frame, &dwmFrame, &windowRect)) {
        *frame = windowRect;
    }
    int width = frame->right - frame->left;
    int height = frame->bottom - frame->top;

    if (fromScreen && GrabFromScreen(hWnd, *frame, picture)) {
        return true;
    }

    void* bits = nullptr;
    HBITMAP bitmap = CreateDib(fullWidth, fullHeight, &bits);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    constexpr UINT kRenderFullContent = 2;  // PW_RENDERFULLCONTENT
    bool ok = PrintWindow(hWnd, dc, kRenderFullContent) != FALSE;
    GdiFlush();

    if (ok) {
        picture->width = width;
        picture->height = height;
        picture->pixels.resize((size_t)width * height);
        const DWORD* source = (const DWORD*)bits +
                              (size_t)(frame->top - windowRect.top) * fullWidth +
                              (frame->left - windowRect.left);
        for (int y = 0; y < height; y++) {
            memcpy(&picture->pixels[(size_t)y * width],
                   source + (size_t)y * fullWidth, width * sizeof(DWORD));
        }

        // An all-black picture means the window couldn't be captured.
        bool blank = true;
        for (size_t i = 0; i < picture->pixels.size() && blank; i += 7) {
            blank = (picture->pixels[i] & 0x00FFFFFF) == 0;
        }
        ok = !blank;
    }

    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return ok;
}

// Keeps a window's picture: reduced to about twice the size of a card, which
// is what the card is drawn from, and at its real size, for the window to be
// animated out of its card later.
void StoreSnapshot(HWND hWnd, Snapshot&& picture) {
    if (picture.width <= 0 || picture.height <= 0) {
        return;
    }

    int width = std::min(picture.width, 2 * Scale(g_settings.cardWidth));
    int height =
        std::max(1, (int)((double)picture.height * width / picture.width));

    void* bits = nullptr;
    HBITMAP bitmap = CreateDib(width, height, &bits);
    HDC dc = CreateCompatibleDC(nullptr);
    if (bitmap && dc) {
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = picture.width;
        bi.bmiHeader.biHeight = -picture.height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        // Averages the source pixels, instead of skipping some.
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, 0, 0, width, height, 0, 0, picture.width,
                      picture.height, picture.pixels.data(), &bi,
                      DIB_RGB_COLORS, SRCCOPY);
        GdiFlush();

        const DWORD* pixels = (const DWORD*)bits;
        Snapshot& snapshot = g_snapshots[hWnd];
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(pixels, pixels + (size_t)width * height);
        snapshot.serial = ++g_snapshotSerial;

        if ((size_t)picture.width * picture.height <= kMaxKeptPixels) {
            snapshot.fullWidth = picture.width;
            snapshot.fullHeight = picture.height;
            snapshot.full = std::move(picture.pixels);
        } else {
            std::vector<DWORD>().swap(snapshot.full);
        }

        SelectObject(dc, oldBitmap);
    }

    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (dc) {
        DeleteDC(dc);
    }
}

bool OnStage(HWND hWnd) {
    return std::find(g_stage.begin(), g_stage.end(), hWnd) != g_stage.end();
}

// Restores the window to its normal state, and gives its visible frame and the
// width of the invisible resize borders around it.
bool RestoreForStage(HWND hWnd, RECT* frame, RECT* insets) {
    // Shown without being activated rather than restored: it goes straight to
    // its normal state even if it was minimized while maximized, and the
    // Windows Animations mod, which animates restores from the taskbar,
    // doesn't take this for one.
    for (int i = 0; i < 2 && (IsIconic(hWnd) || IsZoomed(hWnd)); i++) {
        ShowWindow(hWnd, SW_SHOWNOACTIVATE);
    }

    RECT windowRect;
    if (!GetWindowRect(hWnd, &windowRect)) {
        return false;
    }

    if (FAILED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, frame,
                                     sizeof(*frame)))) {
        *frame = windowRect;
    }

    *insets = {frame->left - windowRect.left, frame->top - windowRect.top,
               windowRect.right - frame->right,
               windowRect.bottom - frame->bottom};
    g_frameInsets[hWnd] = *insets;
    return true;
}

// Moves the window so that its visible frame is the given rectangle.
void SetWindowFrame(HWND hWnd, const RECT& insets, int x, int y, int width,
                    int height) {
    SetWindowPos(hWnd, nullptr, x - insets.left, y - insets.top,
                 width + insets.left + insets.right,
                 height + insets.top + insets.bottom,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

struct StagePlacement {
    RECT insets{};
    int width = 0;
    int height = 0;
};

// The size a window should have alone on stage, given the room it has.
// `width` and `height` come in as its current size.
void StageSize(HWND hWnd,
               const RECT& insets,
               int availableWidth,
               int availableHeight,
               int* width,
               int* height) {
    int maxWidth = availableWidth * g_settings.stageSize / 100;
    int maxHeight = availableHeight * g_settings.stageSize / 100;

    switch (g_settings.stageSizing) {
        case StageSizing::Inches: {
            // Same proportions as the screen, with the given diagonal, the
            // screen's pixels per inch worked out from its diagonal.
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(g_monitor, &mi);
            double screenWidth = mi.rcMonitor.right - mi.rcMonitor.left;
            double screenHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
            double pixelsPerInch = std::hypot(screenWidth, screenHeight) /
                                   g_settings.screenInches;
            double diagonal = g_settings.stageInches * pixelsPerInch;
            double aspect = screenWidth / screenHeight;
            double h = diagonal / std::sqrt(1 + aspect * aspect);
            double w = h * aspect;

            // Shrink, keeping the proportions, if it doesn't fit.
            double scale =
                std::min({1.0, availableWidth / w, availableHeight / h});
            *width = (int)(w * scale);
            *height = (int)(h * scale);
            break;
        }

        case StageSizing::Fill:
            *width = maxWidth;
            *height = maxHeight;
            break;

        case StageSizing::Fit: {
            // The window's own size from before Stage Manager, so that it
            // gets it back after having shared the stage.
            auto saved = g_saved.find(hWnd);
            if (saved != g_saved.end()) {
                const RECT& normal = saved->second.rcNormalPosition;
                *width =
                    normal.right - normal.left - insets.left - insets.right;
                *height =
                    normal.bottom - normal.top - insets.top - insets.bottom;
            }
            *width = std::min(*width, maxWidth);
            *height = std::min(*height, maxHeight);
            break;
        }
    }
}

// Restores the window to its normal state and works out the size it should
// have alone on stage, given the room it has.
bool PrepareStageWindow(HWND hWnd,
                        int availableWidth,
                        int availableHeight,
                        StagePlacement* placement) {
    RECT frame;
    if (!RestoreForStage(hWnd, &frame, &placement->insets)) {
        return false;
    }

    placement->width = frame.right - frame.left;
    placement->height = frame.bottom - frame.top;
    StageSize(hWnd, placement->insets, availableWidth, availableHeight,
              &placement->width, &placement->height);
    return true;
}

// The room a window alone on stage has. It's centered on the screen, its width
// limited so that it stays clear of the column on the left: half of it can
// reach from the center up to the column's right edge, plus a margin.
bool GetSingleArea(int* centerX,
                   int* centerY,
                   int* availableWidth,
                   int* availableHeight) {
    *centerX = (g_workArea.left + g_workArea.right) / 2;
    *centerY = (g_workArea.top + g_workArea.bottom) / 2;
    int columnRight = g_workArea.left + Margin() + Scale(g_settings.cardWidth) +
                      Margin();

    *availableWidth = 2 * (*centerX - columnRight);
    *availableHeight = g_workArea.bottom - g_workArea.top - 2 * Margin();
    return *availableWidth > 0 && *availableHeight > 0;
}

// ── Two windows on stage ─────────────────────────────────────────────────────
// They share the whole area to the right of the column, side by side, full
// height, split by a thin handle: dragging it (or the inner edge of either
// window) resizes both, double-clicking it swaps them.

constexpr WCHAR kDividerClass[] = L"Windhawk_StageManager_Divider";

// Share of the pair's width taken by the left window.
double g_splitRatio = 0.5;
HWND g_divider;
bool g_dividerDragging;
// The stage window whose edge the user is dragging, while it lasts.
HWND g_resizing;

bool GetPairArea(RECT* area, int* gap) {
    *gap = Scale(16);
    *area = {g_workArea.left + Margin() + Scale(g_settings.cardWidth) +
                 Margin(),
             g_workArea.top + Margin(), g_workArea.right - Margin(),
             g_workArea.bottom - Margin()};
    return area->right - area->left > 2 * *gap + Scale(200) &&
           area->bottom > area->top;
}

// Width of the left window for a given split, kept within reason.
int PairLeftWidth(int inner, double ratio) {
    int minWidth = std::min(Scale(220), inner / 3);
    return std::clamp((int)std::lround(inner * ratio), minWidth,
                      inner - minWidth);
}

void DestroyDivider() {
    if (g_divider) {
        DestroyWindow(g_divider);
        g_divider = nullptr;
    }
    g_dividerDragging = false;
}

// The divider is nearly transparent everywhere (but still catches the mouse),
// with a small rounded bar in its middle.
void PaintDivider(int x, int y, int width, int height) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap =
        CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    int barWidth = std::max(Scale(4), width / 3);
    int barHeight = std::min(height, Scale(56));
    double barLeft = (width - barWidth) / 2.0;
    double barTop = (height - barHeight) / 2.0;
    double radius = barWidth / 2.0;

    DWORD* pixels = (DWORD*)bits;
    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            // Distance outside the bar's rounded rectangle.
            double cx = std::clamp(px + 0.5, barLeft + radius,
                                   barLeft + barWidth - radius);
            double cy = std::clamp(py + 0.5, barTop + radius,
                                   barTop + barHeight - radius);
            double d = std::hypot(px + 0.5 - cx, py + 0.5 - cy) - radius;
            double coverage = std::clamp(0.5 - d, 0.0, 1.0);

            BYTE alpha = (BYTE)std::max(1.0, 190 * coverage);
            BYTE value = (BYTE)(alpha * 235 / 255);  // premultiplied
            pixels[py * width + px] =
                ((DWORD)alpha << 24) | (value << 16) | (value << 8) | value;
        }
    }

    POINT position = {x, y};
    SIZE size = {width, height};
    POINT sourcePoint = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_divider, screenDc, &position, &size, memDc,
                        &sourcePoint, 0, &blend, ULW_ALPHA);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

void PositionDivider(const RECT& area, int leftWidth, int gap) {
    if (!g_divider) {
        g_divider = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kDividerClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!g_divider) {
            return;
        }
    }

    PaintDivider(area.left + leftWidth, area.top, gap, area.bottom - area.top);
    SetWindowPos(g_divider, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(g_divider, SW_SHOWNOACTIVATE);
}

void LayoutPair() {
    RECT area;
    int gap;
    if (g_stage.size() != 2 || !GetPairArea(&area, &gap)) {
        return;
    }

    int inner = area.right - area.left - gap;
    int height = area.bottom - area.top;
    int leftWidth = PairLeftWidth(inner, g_splitRatio);

    for (int i = 0; i < 2; i++) {
        RECT frame, insets;
        if (!RestoreForStage(g_stage[i], &frame, &insets)) {
            continue;
        }
        int x = i == 0 ? area.left : area.left + leftWidth + gap;
        int width = i == 0 ? leftWidth : inner - leftWidth;
        SetWindowFrame(g_stage[i], insets, x, area.top, width, height);
    }

    PositionDivider(area, leftWidth, gap);
}

// The user is dragging the edge of one of the two stage windows: the other one
// follows, so that together they still fill the area without overlapping.
void FollowPairResize(HWND resized) {
    RECT area;
    int gap;
    if (g_stage.size() != 2 || !OnStage(resized) ||
        !GetPairArea(&area, &gap)) {
        return;
    }

    RECT frame;
    if (FAILED(DwmGetWindowAttribute(resized, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &frame, sizeof(frame)))) {
        return;
    }

    int inner = area.right - area.left - gap;
    bool isLeft = resized == g_stage[0];
    int leftWidth = isLeft ? frame.right - area.left
                           : frame.left - gap - area.left;
    leftWidth = PairLeftWidth(inner, (double)leftWidth / inner);
    g_splitRatio = (double)leftWidth / inner;

    HWND other = g_stage[isLeft ? 1 : 0];
    auto insets = g_frameInsets.find(other);
    if (insets != g_frameInsets.end()) {
        int x = isLeft ? area.left + leftWidth + gap : area.left;
        int width = isLeft ? inner - leftWidth : leftWidth;
        SetWindowFrame(other, insets->second, x, area.top, width,
                       area.bottom - area.top);
    }

    PositionDivider(area, leftWidth, gap);
}

// Where LayoutStage will put the visible frame of a stage window, worked out
// without touching the window: its animation can head there before the window
// itself is there, which takes a while.
bool PredictStageFrame(HWND hWnd, RECT* frame) {
    if (g_stage.size() == 2) {
        RECT area;
        int gap;
        if (!OnStage(hWnd) || !GetPairArea(&area, &gap)) {
            return false;
        }
        int inner = area.right - area.left - gap;
        int leftWidth = PairLeftWidth(inner, g_splitRatio);
        if (hWnd == g_stage[0]) {
            *frame = {area.left, area.top, area.left + leftWidth, area.bottom};
        } else {
            *frame = {area.left + leftWidth + gap, area.top, area.right,
                      area.bottom};
        }
        return frame->right > frame->left && frame->bottom > frame->top;
    }

    int centerX, centerY, availableWidth, availableHeight;
    auto saved = g_saved.find(hWnd);
    if (g_stage.size() != 1 || g_stage[0] != hWnd || saved == g_saved.end() ||
        !GetSingleArea(&centerX, &centerY, &availableWidth,
                       &availableHeight)) {
        return false;
    }

    RECT insets{};
    auto known = g_frameInsets.find(hWnd);
    if (known != g_frameInsets.end()) {
        insets = known->second;
    }

    const RECT& normal = saved->second.rcNormalPosition;
    int width = normal.right - normal.left - insets.left - insets.right;
    int height = normal.bottom - normal.top - insets.top - insets.bottom;
    StageSize(hWnd, insets, availableWidth, availableHeight, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }

    *frame = {centerX - width / 2, centerY - height / 2,
              centerX - width / 2 + width, centerY - height / 2 + height};
    return true;
}

void UpdateMouseHook();

// Places the stage windows. One window gets its own size, centered on the
// screen. Two windows share the whole area to the right of the column.
void LayoutStage() {
    g_laidOutCount = g_stage.size();
    UpdateMouseHook();
    if (g_stage.size() != 2) {
        DestroyDivider();
        g_splitRatio = 0.5;
    }
    if (g_stage.empty()) {
        return;
    }
    if (g_stage.size() == 2) {
        LayoutPair();
        return;
    }

    int centerX, centerY, availableWidth, availableHeight;
    if (!GetSingleArea(&centerX, &centerY, &availableWidth,
                       &availableHeight)) {
        return;
    }

    StagePlacement placement;
    if (PrepareStageWindow(g_stage[0], availableWidth, availableHeight,
                           &placement)) {
        SetWindowFrame(g_stage[0], placement.insets,
                       centerX - placement.width / 2,
                       centerY - placement.height / 2, placement.width,
                       placement.height);
    }
}

LRESULT CALLBACK DividerWndProc(HWND hWnd,
                                UINT msg,
                                WPARAM wParam,
                                LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            g_dividerDragging = true;
            return 0;

        case WM_MOUSEMOVE:
            if (g_dividerDragging) {
                RECT area;
                int gap;
                POINT pt;
                if (GetPairArea(&area, &gap) && GetCursorPos(&pt)) {
                    int inner = area.right - area.left - gap;
                    g_splitRatio =
                        (double)(pt.x - gap / 2 - area.left) / inner;
                    g_splitRatio =
                        (double)PairLeftWidth(inner, g_splitRatio) / inner;
                    LayoutPair();
                }
            }
            return 0;

        case WM_LBUTTONUP:
            g_dividerDragging = false;
            ReleaseCapture();
            return 0;

        case WM_CAPTURECHANGED:
            g_dividerDragging = false;
            return 0;

        case WM_LBUTTONDBLCLK:
            // Swap the two windows' places.
            if (g_stage.size() == 2) {
                std::swap(g_stage[0], g_stage[1]);
                LayoutPair();
            }
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

////////////////////////////////////////////////////////////////////////////////
// Preview cards
//
// As on macOS, each window in the column is shown turned slightly away, its
// right edge further than its left, with the app's icon over its lower left
// corner. Cards are drawn from a picture of the window (PaintSnapshotCard).
// Without one, live DWM thumbnails are used, and since Windows can't transform
// those the tilt is approximated: the preview is cut into vertical slices,
// each its own thumbnail, each a little higher and shorter than the one on its
// left, and the card window is clipped to the resulting shape. The icon lives
// in a small window of its own, since it sticks out of the card.

constexpr WCHAR kCardClass[] = L"Windhawk_StageManager_Card";
constexpr WCHAR kIconClass[] = L"Windhawk_StageManager_Icon";

constexpr int kSlices = 20;
// Height of the right edge relative to the left one.
constexpr double kFarEdgeScale = 0.92;
// How much higher the right edge sits than the left one, relative to the
// card's height: the card rises from left to right.
constexpr double kRise = 0.14;

int CardRise(int height) {
    return (int)(height * kRise);
}

struct Card {
    HWND wnd = nullptr;
    HWND iconWnd = nullptr;
    HWND target = nullptr;
    std::vector<HTHUMBNAIL> slices;
    // Drawn from the window's picture rather than from live thumbnails.
    const Snapshot* snapshot = nullptr;
    bool thumbUsable = false;
    SIZE source{};
    HICON icon = nullptr;
    bool ownsIcon = false;
    // What the card and icon windows currently show, to tell whether they
    // need drawing again.
    unsigned paintedSerial = 0;
    int paintedWidth = 0;
    int paintedHeight = 0;
    int paintedIcon = 0;
    // Where it is, as if it weren't tilted: where an animation aims.
    RECT flat{};
};

std::vector<std::unique_ptr<Card>> g_cards;

// The icon the window shows for itself, as on the taskbar, or null.
HICON GetWindowOwnIcon(HWND hWnd) {
    DWORD_PTR icon = 0;
    SendMessageTimeoutW(hWnd, WM_GETICON, ICON_BIG, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &icon);
    if (!icon) {
        SendMessageTimeoutW(hWnd, WM_GETICON, ICON_SMALL2, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &icon);
    }
    if (!icon) {
        icon = GetClassLongPtrW(hWnd, GCLP_HICON);
    }
    return (HICON)icon;
}

// The window's own icon, which is what the taskbar shows: some programs, the
// Windhawk interface for one, run from an executable whose icon isn't theirs.
// Without one, the icon of the window's executable at the given size, and the
// generic icon as a last resort.
HICON GetAppIcon(HWND hWnd, int size, bool* owned) {
    *owned = false;

    if (HICON icon = GetWindowOwnIcon(hWnd)) {
        return icon;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);

    WCHAR path[MAX_PATH] = {};
    if (HANDLE process =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        DWORD length = ARRAYSIZE(path);
        if (!QueryFullProcessImageNameW(process, 0, path, &length)) {
            path[0] = L'\0';
        }
        CloseHandle(process);
    }

    using PrivateExtractIconsW_t = UINT(WINAPI*)(LPCWSTR, int, int, int,
                                                 HICON*, UINT*, UINT, UINT);
    static auto pPrivateExtractIconsW = (PrivateExtractIconsW_t)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "PrivateExtractIconsW");

    PCWSTR fileName = wcsrchr(path, L'\\');
    if (path[0] && pPrivateExtractIconsW &&
        !(fileName && _wcsicmp(fileName + 1, L"ApplicationFrameHost.exe") ==
                          0)) {
        HICON icon = nullptr;
        UINT id = 0;
        if (pPrivateExtractIconsW(path, 0, size, size, &icon, &id, 1, 0) == 1 &&
            icon) {
            *owned = true;
            return icon;
        }
    }

    return GetWindowIcon(hWnd);
}

void DestroyCard(Card& card) {
    for (HTHUMBNAIL slice : card.slices) {
        DwmUnregisterThumbnail(slice);
    }
    card.slices.clear();
    if (card.iconWnd) {
        DestroyWindow(card.iconWnd);
        card.iconWnd = nullptr;
    }
    if (card.wnd) {
        DestroyWindow(card.wnd);
        card.wnd = nullptr;
    }
    if (card.ownsIcon && card.icon) {
        DestroyIcon(card.icon);
    }
    card.icon = nullptr;
}

void DestroyCards() {
    for (auto& card : g_cards) {
        DestroyCard(*card);
    }
    g_cards.clear();
}

// The card's outline, with rounded corners: full height on the left, a little
// shorter on the right, and raised towards the right. The window is `height`
// plus the rise tall.
HRGN CreateCardRegion(int width, int height, int radius) {
    int rise = CardRise(height);
    int farHeight = (int)(height * kFarEdgeScale);
    int farTop = (height - farHeight) / 2;
    POINT corners[4] = {
        {0, rise},
        {width, farTop},
        {width, farTop + farHeight},
        {0, rise + height},
    };

    std::vector<POINT> points;
    for (int i = 0; i < 4; i++) {
        POINT prev = corners[(i + 3) % 4];
        POINT corner = corners[i];
        POINT next = corners[(i + 1) % 4];

        auto toward = [&](POINT target) {
            double dx = target.x - corner.x;
            double dy = target.y - corner.y;
            double length = std::hypot(dx, dy);
            double t = length > 0 ? std::min(1.0, radius / length) : 0;
            return std::pair<double, double>{corner.x + dx * t,
                                             corner.y + dy * t};
        };

        // A quadratic curve from a point on the incoming edge, through the
        // corner, to a point on the outgoing edge.
        auto [x0, y0] = toward(prev);
        auto [x2, y2] = toward(next);
        constexpr int kSteps = 6;
        for (int step = 0; step <= kSteps; step++) {
            double t = (double)step / kSteps;
            double u = 1 - t;
            double x = u * u * x0 + 2 * u * t * corner.x + t * t * x2;
            double y = u * u * y0 + 2 * u * t * corner.y + t * t * y2;
            points.push_back({(LONG)std::lround(x), (LONG)std::lround(y)});
        }
    }

    return CreatePolygonRgn(points.data(), (int)points.size(), WINDING);
}

void UpdateCardSlices(Card* card, int width, int height) {
    if (card->slices.empty()) {
        return;
    }

    // Leave out the invisible resize borders around the window, if known.
    RECT source = {0, 0, card->source.cx, card->source.cy};
    auto insets = g_frameInsets.find(card->target);
    if (insets != g_frameInsets.end()) {
        source.left += insets->second.left;
        source.top += insets->second.top;
        source.right -= insets->second.right;
        source.bottom -= insets->second.bottom;
        if (source.right <= source.left || source.bottom <= source.top) {
            source = {0, 0, card->source.cx, card->source.cy};
        }
    }

    // Crop the source to the card's proportions instead of stretching it: a
    // tall window keeps its top part, a wide one its middle.
    double cardAspect = (double)width / height;
    int fullWidth = source.right - source.left;
    int fullHeight = source.bottom - source.top;
    if (fullWidth > 0 && fullHeight > 0) {
        if ((double)fullWidth / fullHeight < cardAspect) {
            source.bottom = source.top + (int)(fullWidth / cardAspect);
        } else {
            int croppedWidth = (int)(fullHeight * cardAspect);
            source.left += (fullWidth - croppedWidth) / 2;
            source.right = source.left + croppedWidth;
        }
    }

    int sourceWidth = source.right - source.left;

    for (int i = 0; i < (int)card->slices.size(); i++) {
        int n = (int)card->slices.size();
        double t = (i + 0.5) / n;
        int sliceHeight = (int)(height * (1 - (1 - kFarEdgeScale) * t));
        double center = CardRise(height) * (1 - t) + height / 2.0;
        int top = (int)(center - sliceHeight / 2.0);

        DWM_THUMBNAIL_PROPERTIES props{};
        props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION |
                        DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY |
                        DWM_TNP_SOURCECLIENTAREAONLY;
        props.fVisible = TRUE;
        props.opacity = 255;
        props.fSourceClientAreaOnly = FALSE;
        // One pixel wider than its share, so that no gap shows between slices.
        props.rcDestination = {width * i / n, top,
                               std::min(width, width * (i + 1) / n + 1),
                               top + sliceHeight};
        props.rcSource = {source.left + sourceWidth * i / n, source.top,
                          source.left + sourceWidth * (i + 1) / n,
                          source.bottom};

        DwmUpdateThumbnailProperties(card->slices[i], &props);
    }
}

// Room around a card drawn from a picture, for its shadow.
int CardShadowPad() {
    return Scale(18);
}

// Height of a card drawn from a picture, its left edge `height` tall: the
// right edge is shorter and higher.
int SnapshotCardTotal(int height) {
    return (int)std::ceil(height * (0.5 + kRise + kFarEdgeScale / 2));
}

// The size of a card drawn from a picture, at most `boxWidth` wide: the whole
// window, in its own proportions, as on macOS. A tall window gets a narrower
// card rather than a taller one; only extreme proportions are cropped.
SIZE SnapshotCardSize(const Snapshot& snapshot, int boxWidth) {
    double aspect = std::clamp((double)snapshot.height / snapshot.width, 0.45,
                               1.25);
    double maxHeight = boxWidth * 0.68;
    double width = boxWidth;
    double height = width * aspect;
    if (height > maxHeight) {
        height = maxHeight;
        width = height / aspect;
    }
    return {std::max(1, (int)width), std::max(1, (int)height)};
}

// Draws the card from the window's picture, the way macOS shows the windows
// in its column: turned slightly away, its right edge further, in a true
// perspective, with rounded corners and a soft shadow. Each pixel is traced
// back to the points of the picture it shows, 3x3 per pixel, which smooths the
// edges and shrinks the picture without losing detail.
//
// (x, y): top left of the card itself, the shadow around it going beyond.
void PaintSnapshotCard(Card* card, int x, int y, int width, int height) {
    const Snapshot& snapshot = *card->snapshot;
    int total = SnapshotCardTotal(height);
    int pad = CardShadowPad();
    int bitmapWidth = width + 2 * pad;
    int bitmapHeight = total + 2 * pad;
    float radius = (float)Scale(10);

    // The part of the picture shown: all of it, unless its proportions are
    // extreme, in which case a tall window keeps its top part, a wide one its
    // middle.
    float sourceLeft = 0;
    float sourceWidth = (float)snapshot.width;
    float sourceHeight = (float)snapshot.height;
    float cardAspect = (float)width / height;
    if (sourceWidth / sourceHeight < cardAspect) {
        sourceHeight = sourceWidth / cardAspect;
    } else {
        float cropped = sourceHeight * cardAspect;
        sourceLeft = (sourceWidth - cropped) / 2;
        sourceWidth = cropped;
    }

    // The perspective. At a fraction u of the card's width, things are
    // scaled by s(u) = 1 / (1 + k u): 1 at the left edge, kFarEdgeScale at
    // the right one. Columns are squeezed towards the right the same way, and
    // their middles rise towards a horizon above the card, by kRise of the
    // height at the right edge.
    float k = (float)(1 / kFarEdgeScale - 1);
    float spanX = width * (1 + k);
    float leftCenter = pad + total - height / 2.0f;
    float horizonDistance = (float)(kRise * height / (1 - kFarEdgeScale));
    float horizon = leftCenter - horizonDistance;

    const DWORD* source = snapshot.pixels.data();
    int sw = snapshot.width;
    int sh = snapshot.height;
    auto sample = [&](float fx, float fy, float out[3]) {
        fx = std::clamp(fx, 0.0f, sw - 1.0f);
        fy = std::clamp(fy, 0.0f, sh - 1.0f);
        int x0 = (int)fx;
        int y0 = (int)fy;
        int x1 = std::min(x0 + 1, sw - 1);
        int y1 = std::min(y0 + 1, sh - 1);
        float tx = fx - x0;
        float ty = fy - y0;
        DWORD c00 = source[y0 * sw + x0];
        DWORD c10 = source[y0 * sw + x1];
        DWORD c01 = source[y1 * sw + x0];
        DWORD c11 = source[y1 * sw + x1];
        for (int channel = 0; channel < 3; channel++) {
            int shift = channel * 8;
            float top = ((c00 >> shift) & 0xFF) * (1 - tx) +
                        ((c10 >> shift) & 0xFF) * tx;
            float bottom = ((c01 >> shift) & 0xFF) * (1 - tx) +
                           ((c11 >> shift) & 0xFF) * tx;
            out[channel] += top * (1 - ty) + bottom * ty;
        }
    };

    // The card, premultiplied, and how much of each pixel it covers.
    std::vector<float> color((size_t)bitmapWidth * bitmapHeight * 3, 0);
    std::vector<float> coverage((size_t)bitmapWidth * bitmapHeight, 0);

    constexpr int kSub = 3;
    for (int px = pad - 1; px < pad + width + 1; px++) {
        for (int subX = 0; subX < kSub; subX++) {
            float cardX = px + (subX + 0.5f) / kSub - pad;
            if (cardX < 0 || cardX >= width) {
                continue;
            }
            float u = cardX / (spanX - k * cardX);
            float scale = 1 / (1 + k * u);
            float columnHeight = height * scale;
            float columnTop =
                horizon + horizonDistance * scale - columnHeight / 2;
            float sourceX = sourceLeft + u * sourceWidth - 0.5f;

            int firstRow = std::max(0, (int)std::floor(columnTop));
            int lastRow = std::min(bitmapHeight - 1,
                                   (int)std::ceil(columnTop + columnHeight));
            for (int py = firstRow; py <= lastRow; py++) {
                size_t index = (size_t)py * bitmapWidth + px;
                for (int subY = 0; subY < kSub; subY++) {
                    float v =
                        (py + (subY + 0.5f) / kSub - columnTop) / columnHeight;
                    if (v < 0 || v >= 1) {
                        continue;
                    }

                    // Rounded corners, worked out on the card as if it were
                    // flat.
                    float flatX = u * width;
                    float flatY = v * height;
                    float nearestX = std::clamp(flatX, radius, width - radius);
                    float nearestY = std::clamp(flatY, radius, height - radius);
                    float dx = flatX - nearestX;
                    float dy = flatY - nearestY;
                    if (dx * dx + dy * dy > radius * radius) {
                        continue;
                    }

                    sample(sourceX, v * sourceHeight - 0.5f,
                           &color[index * 3]);
                    coverage[index] += 1;
                }
            }
        }
    }

    constexpr float kSamples = kSub * kSub;
    for (size_t i = 0; i < coverage.size(); i++) {
        coverage[i] /= kSamples;
        color[i * 3] /= kSamples;
        color[i * 3 + 1] /= kSamples;
        color[i * 3 + 2] /= kSamples;
    }

    // The shadow: the card's outline, a little lower, blurred (three box
    // blurs come close to a Gaussian one).
    std::vector<float> shadow((size_t)bitmapWidth * bitmapHeight, 0);
    int offset = Scale(6);
    for (int py = 0; py + offset < bitmapHeight; py++) {
        memcpy(&shadow[(size_t)(py + offset) * bitmapWidth],
               &coverage[(size_t)py * bitmapWidth],
               bitmapWidth * sizeof(float));
    }
    std::vector<float> line(std::max(bitmapWidth, bitmapHeight));
    int blur = std::max(1, Scale(5));
    for (int pass = 0; pass < 3; pass++) {
        for (int py = 0; py < bitmapHeight; py++) {
            float* row = &shadow[(size_t)py * bitmapWidth];
            float sum = 0;
            for (int i = -blur; i <= blur; i++) {
                sum += row[std::clamp(i, 0, bitmapWidth - 1)] * (i >= 0);
            }
            for (int px = 0; px < bitmapWidth; px++) {
                line[px] = sum / (2 * blur + 1);
                int out = px - blur;
                int in = px + blur + 1;
                sum -= out >= 0 ? row[out] : 0;
                sum += in < bitmapWidth ? row[in] : 0;
            }
            memcpy(row, line.data(), bitmapWidth * sizeof(float));
        }
        for (int px = 0; px < bitmapWidth; px++) {
            float sum = 0;
            for (int i = 0; i <= blur && i < bitmapHeight; i++) {
                sum += shadow[(size_t)i * bitmapWidth + px];
            }
            for (int py = 0; py < bitmapHeight; py++) {
                line[py] = sum / (2 * blur + 1);
                int out = py - blur;
                int in = py + blur + 1;
                sum -= out >= 0 ? shadow[(size_t)out * bitmapWidth + px] : 0;
                sum += in < bitmapHeight ? shadow[(size_t)in * bitmapWidth + px]
                                         : 0;
            }
            for (int py = 0; py < bitmapHeight; py++) {
                shadow[(size_t)py * bitmapWidth + px] = line[py];
            }
        }
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bitmapWidth;
    bi.bmiHeader.biHeight = -bitmapHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap =
        CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    // The card over its shadow, which is black.
    constexpr float kShadowStrength = 0.42f;
    DWORD* pixels = (DWORD*)bits;
    for (size_t i = 0; i < coverage.size(); i++) {
        float alpha = coverage[i] +
                      (1 - coverage[i]) * shadow[i] * kShadowStrength;
        auto channel = [](float value) {
            return (DWORD)std::clamp((int)std::lround(value), 0, 255);
        };
        pixels[i] = (channel(alpha * 255) << 24) |
                    (channel(color[i * 3 + 2]) << 16) |
                    (channel(color[i * 3 + 1]) << 8) | channel(color[i * 3]);
    }

    POINT position = {x - pad, y - pad};
    SIZE size = {bitmapWidth, bitmapHeight};
    POINT sourcePoint = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(card->wnd, screenDc, &position, &size, memDc,
                        &sourcePoint, 0, &blend, ULW_ALPHA);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

void PaintIconWindow(Card* card, int x, int y, int size) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap =
        CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
    memset(bits, 0, (size_t)size * size * 4);
    DrawIconEx(memDc, 0, 0, card->icon, size, size, 0, nullptr, DI_NORMAL);
    GdiFlush();

    // Icons without an alpha channel leave it empty: make what was drawn
    // opaque.
    DWORD* pixels = (DWORD*)bits;
    bool hasAlpha = false;
    for (int i = 0; i < size * size && !hasAlpha; i++) {
        hasAlpha = (pixels[i] >> 24) != 0;
    }
    if (!hasAlpha) {
        for (int i = 0; i < size * size; i++) {
            if (pixels[i] & 0x00FFFFFF) {
                pixels[i] |= 0xFF000000;
            }
        }
    }

    POINT position = {x, y};
    SIZE windowSize = {size, size};
    POINT sourcePoint = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(card->iconWnd, screenDc, &position, &windowSize, memDc,
                        &sourcePoint, 0, &blend, ULW_ALPHA);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

LRESULT CALLBACK CardWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Card* card = (Card*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_LBUTTONUP:
            if (card) {
                // Posted: switching rebuilds the cards, this one included.
                // Shift adds the window to the stage instead of swapping.
                PostMessageW(g_msgWnd, WM_APP_SWITCH, (WPARAM)card->target,
                             (wParam & MK_SHIFT) ? 1 : 0);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hWnd, &ps);

            RECT rc;
            GetClientRect(hWnd, &rc);

            // Shows only where the preview doesn't cover the card.
            HBRUSH background = CreateSolidBrush(RGB(42, 42, 42));
            FillRect(dc, &rc, background);
            DeleteObject(background);

            // No preview available: the icon, in the middle.
            if (card && !card->thumbUsable && card->icon) {
                int size = std::min(rc.right, rc.bottom) / 2;
                DrawIconEx(dc, (rc.right - size) / 2, (rc.bottom - size) / 2,
                           card->icon, size, size, 0, nullptr, DI_NORMAL);
            }

            EndPaint(hWnd, &ps);
            return 0;
        }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK IconWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Card* card = (Card*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_LBUTTONUP:
            if (card) {
                PostMessageW(g_msgWnd, WM_APP_SWITCH, (WPARAM)card->target,
                             (wParam & MK_SHIFT) ? 1 : 0);
            }
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

namespace genie {
bool HidesCard(HWND target);
void CardsRebuilt();
}  // namespace genie

std::unique_ptr<Card> CreateCard(HWND target,
                                 const Snapshot* snapshot,
                                 int iconSize) {
    HINSTANCE instance = GetModuleHandleW(nullptr);

    auto card = std::make_unique<Card>();
    card->target = target;
    card->icon = GetAppIcon(target, iconSize, &card->ownsIcon);

    if (snapshot) {
        card->snapshot = snapshot;
        card->source = {snapshot->width, snapshot->height};
        card->thumbUsable = true;
    }

    card->wnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
            (card->snapshot ? WS_EX_LAYERED : 0),
        kCardClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance,
        nullptr);
    if (!card->wnd) {
        DestroyCard(*card);
        return nullptr;
    }
    SetWindowLongPtrW(card->wnd, GWLP_USERDATA, (LONG_PTR)card.get());
    DwmSetWindowAttribute(card->wnd, kDwmWindowCornerPreference,
                          &kDwmCornerDoNotRound, sizeof(kDwmCornerDoNotRound));

    card->iconWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kIconClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance,
        nullptr);
    if (card->iconWnd) {
        SetWindowLongPtrW(card->iconWnd, GWLP_USERDATA, (LONG_PTR)card.get());
    }

    // Without a picture (the window was already minimized), live thumbnails
    // are the fallback.
    HTHUMBNAIL first = nullptr;
    if (!card->snapshot &&
        SUCCEEDED(DwmRegisterThumbnail(card->wnd, target, &first))) {
        DwmQueryThumbnailSourceSize(first, &card->source);
        // A minimized window may only offer a tiny caption-sized image.
        card->thumbUsable =
            card->source.cx > 0 && card->source.cy >= Scale(100);
        card->slices.push_back(first);

        for (int i = 1; card->thumbUsable && i < kSlices; i++) {
            HTHUMBNAIL slice = nullptr;
            if (FAILED(DwmRegisterThumbnail(card->wnd, target, &slice))) {
                break;
            }
            card->slices.push_back(slice);
        }

        if (!card->thumbUsable) {
            DwmUnregisterThumbnail(first);
            card->slices.clear();
        }
    }

    return card;
}

// The cards are not above everything: they're at the top of the ordinary
// windows when laid out, the stage windows being brought in front of them
// right after. The picture of a window leaving the stage can then go between
// the two, behind the stage window but over the cards, and be seen sliding
// into the column (see genie::Behind).
void RebuildCards(bool force) {
    std::vector<HWND> targets;
    for (HWND hWnd : g_strip) {
        if ((int)targets.size() >= g_settings.maxCards) {
            break;
        }
        if (IsWindow(hWnd)) {
            targets.push_back(hWnd);
        }
    }

    // Full-size pictures are only kept for the windows that have a card.
    for (auto& [hWnd, snapshot] : g_snapshots) {
        if (!snapshot.full.empty() &&
            std::find(targets.begin(), targets.end(), hWnd) == targets.end()) {
            std::vector<DWORD>().swap(snapshot.full);
        }
    }

    if (!force && targets.size() == g_cards.size()) {
        bool same = true;
        for (size_t i = 0; i < targets.size(); i++) {
            same = same && g_cards[i]->target == targets[i];
        }
        if (same) {
            genie::CardsRebuilt();
            return;
        }
    }

    // The cards of windows still in the column are kept, and only moved:
    // making them all again takes long enough to be noticed.
    if (force) {
        DestroyCards();
    }
    std::vector<std::unique_ptr<Card>> previous = std::move(g_cards);
    g_cards.clear();

    int cardWidth = Scale(g_settings.cardWidth);
    // 100% is a third of the card's width or so.
    auto iconSizeFor = [](int cardWidth) {
        int full = std::max(Scale(32), cardWidth * 30 / 100);
        return std::max(Scale(12), full * g_settings.iconSize / 100);
    };
    int iconSize = iconSizeFor(cardWidth);

    for (HWND target : targets) {
        auto found = g_snapshots.find(target);
        const Snapshot* snapshot =
            found != g_snapshots.end() ? &found->second : nullptr;

        std::unique_ptr<Card> card;
        for (auto& candidate : previous) {
            if (candidate && candidate->target == target &&
                candidate->snapshot == snapshot) {
                card = std::move(candidate);
                break;
            }
        }
        if (!card) {
            card = CreateCard(target, snapshot, iconSize);
        }
        if (card) {
            card->source = snapshot ? SIZE{snapshot->width, snapshot->height}
                                    : card->source;
            g_cards.push_back(std::move(card));
        }
    }

    for (auto& leftover : previous) {
        if (leftover) {
            DestroyCard(*leftover);
        }
    }

    // Lay the cards out in a vertically centered column, shrinking them if they
    // don't fit. The gap leaves room for the icons, which stick out below.
    int available = g_workArea.bottom - g_workArea.top - 2 * Margin();
    int gap = std::max(Scale(16), iconSize * 40 / 100);

    auto heightFor = [](const Card& card, int width) {
        if (!card.thumbUsable) {
            return width * 65 / 100;
        }
        // Within a landscape range, so that a window left tall or wide by
        // sharing the stage doesn't get an odd-sized card.
        return std::clamp(
            (int)((double)width * card.source.cy / card.source.cx),
            width * 50 / 100, width * 72 / 100);
    };

    // A card's width, height (of its left edge) and total height.
    struct Size {
        int width;
        int height;
        int total;
    };
    auto sizeFor = [&](const Card& card, int boxWidth) {
        if (card.snapshot) {
            SIZE size = SnapshotCardSize(*card.snapshot, boxWidth);
            return Size{size.cx, size.cy, SnapshotCardTotal(size.cy)};
        }
        int height = heightFor(card, boxWidth);
        return Size{boxWidth, height, height + CardRise(height)};
    };

    auto totalFor = [&](int width) {
        int total = 0;
        for (auto& card : g_cards) {
            total += sizeFor(*card, width).total;
        }
        return total + gap * std::max(0, (int)g_cards.size() - 1);
    };

    int total = totalFor(cardWidth);
    if (total > available && total > 0) {
        cardWidth = std::max(Scale(80), cardWidth * available / total);
        iconSize = iconSizeFor(cardWidth);
        total = totalFor(cardWidth);
    }

    int y = g_workArea.top + (g_workArea.bottom - g_workArea.top - total) / 2;

    int left = g_workArea.left + Margin();

    for (auto& card : g_cards) {
        Size size = sizeFor(*card, cardWidth);
        int height = size.height;
        int rise = size.total - height;
        // Narrower cards, of tall windows, are centered in the column.
        int x = left + (cardWidth - size.width) / 2;

        if (card->snapshot) {
            if (card->paintedSerial == card->snapshot->serial &&
                card->paintedWidth == size.width &&
                card->paintedHeight == height) {
                // Already drawn as it should be: it only moves.
                SetWindowPos(card->wnd, HWND_TOP, x - CardShadowPad(),
                             y - CardShadowPad(), 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                PaintSnapshotCard(card.get(), x, y, size.width, height);
                card->paintedSerial = card->snapshot->serial;
                card->paintedWidth = size.width;
                card->paintedHeight = height;
                SetWindowPos(card->wnd, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            // Its left edge, raised by half the rise of the card, half as
            // short again as the far edge.
            int top = y + size.total - height - (int)(kRise * height / 2);
            card->flat = {x, top, x + size.width,
                          top + (int)(height * (1 + kFarEdgeScale) / 2)};
        } else {
            card->flat = {x, y + rise / 2, x + cardWidth,
                          y + rise / 2 + height};
            SetWindowRgn(card->wnd,
                         CreateCardRegion(cardWidth, height, Scale(10)), FALSE);
            SetWindowPos(card->wnd, HWND_TOP, x, y, cardWidth,
                         height + rise, SWP_NOACTIVATE);
            UpdateCardSlices(card.get(), cardWidth, height);
        }

        // The card of a window being animated into it shows up at the end.
        bool hidden = genie::HidesCard(card->target);
        if (!hidden) {
            ShowWindow(card->wnd, SW_SHOWNOACTIVATE);
        }

        if (card->iconWnd) {
            int iconX = x - iconSize * 20 / 100;
            int iconY = y + rise + height - iconSize * 75 / 100;
            if (card->paintedIcon == iconSize) {
                SetWindowPos(card->iconWnd, HWND_TOP, iconX, iconY, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                PaintIconWindow(card.get(), iconX, iconY, iconSize);
                card->paintedIcon = iconSize;
                SetWindowPos(card->iconWnd, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            if (!hidden) {
                ShowWindow(card->iconWnd, SW_SHOWNOACTIVATE);
            }
        }

        y += size.total + gap;
    }

    genie::CardsRebuilt();
}

////////////////////////////////////////////////////////////////////////////////
// Genie animation
//
// A window leaving the stage is drawn into its card, and one coming from the
// column is drawn out of it, the way the Genie effect of macOS does: the side
// nearest the column moves first and the rest follows, squeezed through a neck.
// The effect is the Genie of the "Windows Animations" mod by ReDrag, aimed at
// the card instead of the taskbar.
//
// The real window can't be animated, so a picture of it is: it's shown in a
// click-through window of its own (the "ghost") while the real one is already
// minimized, or not shown yet.
//
// The animations run on a thread of their own. Moving, restoring and
// minimizing the windows of other programs waits for those programs, which
// would otherwise hold the animation back before it starts and make it stutter
// while it runs. The main thread only asks for things (Depart, Arrive, ...);
// the animation thread never waits for the main one, and tells it when a card
// is to be shown with a posted message.
//
// The Windows Animations mod, when in use, is loaded into this process as
// well, and would animate the same minimize towards the taskbar at the same
// time. See ForeignAnimationGuard and RestoreForStage for how it's kept out.

namespace genie {

constexpr WCHAR kGhostClass[] = L"Windhawk_StageManager_Ghost";
// An arriving window is revealed under its picture, which then fades away.
constexpr double kCrossFadeMs = 120;
// ... after keeping it, opaque, over the window for a while.
constexpr double kHoldMs = 60;
// The picture of a leaving window fades over its card at the end.
constexpr double kFadeFrom = 0.8;
// A still picture waits this long at most for what it's waiting for.
constexpr double kPatienceMs = 3000;
constexpr int kSteps = 128;

// How a picture moves between the window and the card, which is what tells the
// animations apart. Genie: the side nearest the column goes first, the rest
// follows (spread). Overlay: it moves all at once, as a rectangle, the window
// coming in passing over the one going out, which only leaves afterwards.
struct Motion {
    // How much later the far side starts moving than the near one.
    double spread = 0.65;
    double duration = 350;
    // Pictures in a higher layer pass over those in a lower one.
    int layer = 0;
    // Shrink, for a leaving window: first it shrinks where it is, to the size
    // of its card (taking `shrinkShare` of the time, with `shrinkSpread`),
    // then, that small, it slides to the card.
    bool shrink = false;
    double shrinkShare = 0.45;
    double shrinkSpread = 0.35;
};

Motion MotionFor(bool arriving) {
    Motion motion;
    double total = g_settings.genieDuration;
    if (g_settings.animation == SwitchAnimation::Overlay) {
        // The arriving window takes the first 70% of the time, the leaving
        // one the last 70% (see SwitchTo): together, the same total time.
        motion.spread = 0;
        motion.duration = total * 0.7;
        motion.layer = arriving ? 1 : 0;
    } else if (g_settings.animation == SwitchAnimation::Shrink) {
        // Both at once, for the whole time: the arriving window grows out of
        // its card like in Genie, over the leaving one, which shrinks and
        // slides to the column underneath it.
        motion.duration = total;
        motion.layer = arriving ? 1 : 0;
        motion.shrink = !arriving;
        motion.spread = arriving ? 0.65 : 0;
    } else {
        motion.duration = total;
        motion.layer = arriving ? 0 : 1;
    }
    return motion;
}

// ── Drawing with the graphics card ──────────────────────────────────────────
// When the card allows it, it draws the animation: the picture becomes a
// texture, bent by a vertex shader, smoothed by the card's own filtering, and
// shown through DirectComposition. Drawn by the processor instead, every pixel
// of a ghost the size of a window is worked out and copied to the screen at
// every frame, which on a high-resolution screen falls to about 15 frames per
// second. The processor is kept as the fallback.
namespace gpu {

using D3DCompile_t = HRESULT(WINAPI*)(LPCVOID,
                                      SIZE_T,
                                      LPCSTR,
                                      const D3D_SHADER_MACRO*,
                                      ID3DInclude*,
                                      LPCSTR,
                                      LPCSTR,
                                      UINT,
                                      UINT,
                                      ID3DBlob**,
                                      ID3DBlob**);

// The picture as a strip of thin columns, each one placed like a column of
// pixels in Render below.
constexpr char kShader[] = R"hlsl(
cbuffer Frame : register(b0)
{
    float2 outputSize;
    float2 padding;
    float4 window;  // left, top, width, height
    float4 card;
    float t;
    float spread;
    float opacity;
    float steps;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint id : SV_VertexID)
{
    float a = min((float)(id >> 1) / steps, 1.0f);
    float side = (float)(id & 1);
    float m = saturate(t * (1.0f + spread) - a * spread);
    m = m * m * (3.0f - 2.0f * m);

    float x = lerp(window.x + a * window.z, card.x + a * card.z, m);
    float h = lerp(window.w, card.w, m);
    float centerY = lerp(window.y + window.w * 0.5f, card.y + card.w * 0.5f, m);
    float y = centerY + (side - 0.5f) * h;

    VertexOutput output;
    output.position = float4(x * 2.0f / outputSize.x - 1.0f,
                             1.0f - y * 2.0f / outputSize.y, 0.0f, 1.0f);
    output.uv = float2(a, side);
    return output;
}

Texture2D picture : register(t0);
SamplerState linearSampler : register(s0);

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return picture.Sample(linearSampler, input.uv) * opacity;
}
)hlsl";

constexpr int kSteps = 384;

struct alignas(16) Constants {
    float outputSize[2];
    float padding[2];
    float window[4];
    float card[4];
    float t;
    float spread;
    float opacity;
    float steps;
};
static_assert(sizeof(Constants) % 16 == 0);

template <typename T>
void SafeRelease(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

bool g_tried;
bool g_ready;
HMODULE g_compiler;
ID3D11Device* g_device;
ID3D11DeviceContext* g_context;
IDXGIFactory2* g_factory;
IDCompositionDevice* g_composition;
ID3D11VertexShader* g_vertexShader;
ID3D11PixelShader* g_pixelShader;
ID3D11Buffer* g_constants;
ID3D11SamplerState* g_sampler;
ID3D11BlendState* g_blend;
ID3D11RasterizerState* g_rasterizer;

void Shutdown() {
    if (g_context) {
        g_context->ClearState();
        g_context->Flush();
    }
    SafeRelease(g_rasterizer);
    SafeRelease(g_blend);
    SafeRelease(g_sampler);
    SafeRelease(g_constants);
    SafeRelease(g_pixelShader);
    SafeRelease(g_vertexShader);
    SafeRelease(g_composition);
    SafeRelease(g_factory);
    SafeRelease(g_context);
    SafeRelease(g_device);
    if (g_compiler) {
        FreeLibrary(g_compiler);
        g_compiler = nullptr;
    }
    g_ready = false;
}

bool Fail(PCWSTR step, HRESULT hr) {
    Wh_Log(L"Graphics card unavailable (%s, 0x%08X), drawing with the processor",
           step, (unsigned)hr);
    Shutdown();
    return false;
}

// Set up once, on the animation thread, the first time it's needed.
bool Init() {
    if (g_tried) {
        return g_ready;
    }
    g_tried = true;

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                  D3D_FEATURE_LEVEL_10_1,
                                  D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &g_device, nullptr, &g_context);
    if (FAILED(hr)) {
        return Fail(L"device", hr);
    }

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    hr = g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr)) {
        hr = dxgiDevice->GetAdapter(&adapter);
    }
    if (SUCCEEDED(hr)) {
        hr = adapter->GetParent(IID_PPV_ARGS(&g_factory));
    }
    if (SUCCEEDED(hr)) {
        hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice),
                                      (void**)&g_composition);
    }
    SafeRelease(adapter);
    SafeRelease(dxgiDevice);
    if (FAILED(hr)) {
        return Fail(L"composition", hr);
    }

    g_compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = g_compiler ? (D3DCompile_t)GetProcAddress(g_compiler,
                                                              "D3DCompile")
                              : nullptr;
    if (!compile) {
        return Fail(L"compiler", E_NOINTERFACE);
    }

    ID3DBlob* vertexCode = nullptr;
    ID3DBlob* pixelCode = nullptr;
    ID3DBlob* errors = nullptr;
    hr = compile(kShader, sizeof(kShader) - 1, "stage-manager-genie", nullptr,
                 nullptr, "VSMain", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                 &vertexCode, &errors);
    SafeRelease(errors);
    if (SUCCEEDED(hr)) {
        hr = compile(kShader, sizeof(kShader) - 1, "stage-manager-genie",
                     nullptr, nullptr, "PSMain", "ps_4_0",
                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelCode, &errors);
        SafeRelease(errors);
    }
    if (SUCCEEDED(hr)) {
        hr = g_device->CreateVertexShader(vertexCode->GetBufferPointer(),
                                          vertexCode->GetBufferSize(), nullptr,
                                          &g_vertexShader);
    }
    if (SUCCEEDED(hr)) {
        hr = g_device->CreatePixelShader(pixelCode->GetBufferPointer(),
                                         pixelCode->GetBufferSize(), nullptr,
                                         &g_pixelShader);
    }
    SafeRelease(vertexCode);
    SafeRelease(pixelCode);
    if (FAILED(hr)) {
        return Fail(L"shaders", hr);
    }

    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = sizeof(Constants);
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = g_device->CreateBuffer(&constantsDesc, nullptr, &g_constants);

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) {
        hr = g_device->CreateSamplerState(&samplerDesc, &g_sampler);
    }

    // The picture's colors are premultiplied by its alpha.
    D3D11_BLEND_DESC blendDesc{};
    auto& target = blendDesc.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) {
        hr = g_device->CreateBlendState(&blendDesc, &g_blend);
    }

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) {
        hr = g_device->CreateRasterizerState(&rasterizerDesc, &g_rasterizer);
    }
    if (FAILED(hr)) {
        return Fail(L"states", hr);
    }

    g_ready = true;
    return true;
}

// What one ghost window shows.
struct Surface {
    IDXGISwapChain1* swapChain = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    IDCompositionTarget* compositionTarget = nullptr;
    IDCompositionVisual* visual = nullptr;
    ID3D11ShaderResourceView* picture = nullptr;
};

void FreeOutput(Surface& surface) {
    SafeRelease(surface.target);
    SafeRelease(surface.swapChain);
}

void Free(Surface& surface) {
    if (surface.compositionTarget) {
        surface.compositionTarget->SetRoot(nullptr);
        if (g_composition) {
            g_composition->Commit();
        }
    }
    FreeOutput(surface);
    SafeRelease(surface.visual);
    SafeRelease(surface.compositionTarget);
    SafeRelease(surface.picture);
}

void ReleasePicture(Surface& surface) {
    SafeRelease(surface.picture);
}

// The picture, with smaller copies of itself for when it's shown shrunk.
bool Upload(Surface& surface, const Snapshot& picture) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = picture.width;
    desc.Height = picture.height;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &texture);
    if (SUCCEEDED(hr)) {
        hr = g_device->CreateShaderResourceView(texture, nullptr,
                                                &surface.picture);
    }
    if (SUCCEEDED(hr)) {
        g_context->UpdateSubresource(texture, 0, nullptr, picture.pixels.data(),
                                     picture.width * sizeof(DWORD), 0);
        g_context->GenerateMips(surface.picture);
    }
    SafeRelease(texture);
    return SUCCEEDED(hr);
}

// Gives the ghost window an output of the given size.
bool Resize(Surface& surface, HWND ghost, int width, int height) {
    FreeOutput(surface);
    if (!g_ready) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    HRESULT hr = g_factory->CreateSwapChainForComposition(
        g_device, &desc, nullptr, &surface.swapChain);

    ID3D11Texture2D* buffer = nullptr;
    if (SUCCEEDED(hr)) {
        hr = surface.swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    }
    if (SUCCEEDED(hr)) {
        hr = g_device->CreateRenderTargetView(buffer, nullptr, &surface.target);
    }
    SafeRelease(buffer);

    if (SUCCEEDED(hr) && !surface.compositionTarget) {
        hr = g_composition->CreateTargetForHwnd(ghost, TRUE,
                                                &surface.compositionTarget);
        if (SUCCEEDED(hr)) {
            hr = g_composition->CreateVisual(&surface.visual);
        }
        if (SUCCEEDED(hr)) {
            hr = surface.compositionTarget->SetRoot(surface.visual);
        }
    }
    if (SUCCEEDED(hr)) {
        hr = surface.visual->SetContent(surface.swapChain);
    }
    if (SUCCEEDED(hr)) {
        hr = g_composition->Commit();
    }
    return SUCCEEDED(hr);
}

// Draws the picture `t` of the way from `window` to `card`, rectangles given
// within the ghost's `bounds`.
bool Draw(Surface& surface,
          const RECT& bounds,
          const RECT& window,
          const RECT& card,
          double t,
          double opacity,
          double spread) {
    if (!g_ready || !surface.target || !surface.picture) {
        return false;
    }
    float width = (float)(bounds.right - bounds.left);
    float height = (float)(bounds.bottom - bounds.top);
    Constants constants = {
        {width, height},
        {0, 0},
        {(float)(window.left - bounds.left), (float)(window.top - bounds.top),
         (float)(window.right - window.left),
         (float)(window.bottom - window.top)},
        {(float)(card.left - bounds.left), (float)(card.top - bounds.top),
         (float)(card.right - card.left), (float)(card.bottom - card.top)},
        (float)t,
        (float)spread,
        (float)std::clamp(opacity, 0.0, 1.0),
        (float)kSteps,
    };
    g_context->UpdateSubresource(g_constants, 0, nullptr, &constants, 0, 0);

    const FLOAT clear[4] = {0, 0, 0, 0};
    g_context->ClearRenderTargetView(surface.target, clear);
    g_context->OMSetRenderTargets(1, &surface.target, nullptr);
    const FLOAT blendFactor[4] = {0, 0, 0, 0};
    g_context->OMSetBlendState(g_blend, blendFactor, 0xFFFFFFFF);
    D3D11_VIEWPORT viewport{0, 0, width, height, 0, 1};
    g_context->RSSetViewports(1, &viewport);
    g_context->RSSetState(g_rasterizer);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_context->VSSetShader(g_vertexShader, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_constants);
    g_context->PSSetShader(g_pixelShader, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_constants);
    g_context->PSSetShaderResources(0, 1, &surface.picture);
    g_context->PSSetSamplers(0, 1, &g_sampler);
    g_context->Draw((kSteps + 1) * 2, 0);

    ID3D11ShaderResourceView* none = nullptr;
    g_context->PSSetShaderResources(0, 1, &none);
    g_context->OMSetRenderTargets(0, nullptr, nullptr);

    // Not waiting for the screen here: the animation thread waits once per
    // frame for all the ghosts together.
    return SUCCEEDED(surface.swapChain->Present(0, 0));
}

}  // namespace gpu

struct Job {
    HWND target = nullptr;
    HWND ghost = nullptr;
    Motion motion;
    // Drawn by the graphics card, rather than the processor.
    bool onGpu = false;
    gpu::Surface surface;
    // The `t` last drawn, so that a change of opacity alone redraws nothing.
    double drawnT = -1;
    // What's on screen, to draw it again when the output is made anew.
    bool shown = false;
    double shownT = 0;
    double shownOpacity = 1;
    double shownSpread = 0;
    RECT shownFrom{};
    RECT shownTo{};
    RECT drawnFrom{};
    RECT drawnTo{};
    double drawnSpread = 0;
    // From the card to the stage, rather than the other way around.
    bool arriving = false;
    bool topmost = true;
    Snapshot picture;
    // The window's visible frame on stage, and its card, flat.
    RECT window{};
    RECT card{};
    // A leaving window waits, still, until its card exists.
    bool hasCard = false;
    // An arriving one waits, at the end, until the real window is in place.
    bool ready = false;
    bool landed = false;
    double created = 0;
    bool started = false;
    double start = 0;
    double delay = 0;
    bool cardShown = false;
    bool windowShown = false;
    double fadeStart = 0;

    // What the ghost shows: the area both rectangles fit in.
    RECT bounds{};
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    DWORD* bits = nullptr;
    // The part of it drawn on last.
    RECT drawn{};
    // Per column of that area: where its strip of the picture starts, the
    // picture rows per pixel, and the picture column shown (negative: none).
    std::vector<float> top, rowsPerPixel, sourceX;
};

struct Reply {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool result = false;
    ~Reply() {
        if (done) {
            CloseHandle(done);
        }
    }
};

enum class CommandType {
    Depart,
    Arrive,
    SetCard,
    Ready,
    Update,
    Behind,
    Settle,
    Raise,
    CancelAll
};

struct Command {
    CommandType type = CommandType::Raise;
    HWND target = nullptr;
    HWND above = nullptr;
    bool topmost = true;
    Snapshot picture;
    RECT card{};
    RECT frame{};
    double delay = 0;
    Motion motion;
    std::shared_ptr<Reply> reply;
};

// Animation thread only.
std::vector<std::unique_ptr<Job>> g_jobs;

// Shared between the two threads.
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Command> g_commands;
// Animations running plus requests not looked at yet.
std::atomic<int> g_busy;
std::atomic<bool> g_quit;
HANDLE g_thread;
HANDLE g_wake;

// Main thread only: the cards of windows still on their way there, which
// aren't shown yet.
std::vector<HWND> g_hiddenCards;

double NowMs() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * 1000.0 / frequency;
}

// Windows' own minimize and restore animations, which would run under ours.
void SetTransitions(HWND hWnd, bool enabled) {
    BOOL disabled = !enabled;
    DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled,
                          sizeof(disabled));
}

// Makes the window invisible without hiding it. Another process's window
// can't be cloaked, so it's made transparent. Not done to windows which manage
// their own transparency, nor to the frames of Store apps, whose content is a
// separate window.
bool Conceal(HWND hWnd) {
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED) {
        return false;
    }

    WCHAR className[64];
    if (GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
        _wcsicmp(className, L"ApplicationFrameWindow") == 0) {
        return false;
    }

    SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    if (!SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA)) {
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
        return false;
    }

    SetTransitions(hWnd, false);
    return true;
}

// Undoes Conceal. Only for a window it was done to.
void Reveal(HWND hWnd) {
    if (!IsWindow(hWnd)) {
        return;
    }
    SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
    SetWindowLongPtrW(hWnd, GWL_EXSTYLE,
                      GetWindowLongPtrW(hWnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
    SetTransitions(hWnd, true);
}

////////////////////////////////////////////////////////////////////////////////
// Animation thread

void FreeCanvas(Job& job) {
    if (job.dc) {
        SelectObject(job.dc, job.oldBitmap);
        DeleteDC(job.dc);
        job.dc = nullptr;
    }
    if (job.bitmap) {
        DeleteObject(job.bitmap);
        job.bitmap = nullptr;
    }
    job.bits = nullptr;
}

bool Draw(Job& job,
          double t,
          double opacity,
          const RECT* from = nullptr,
          const RECT* to = nullptr,
          double spread = -1);

bool CreateCanvas(Job& job) {
    FreeCanvas(job);

    UnionRect(&job.bounds, &job.window, &job.card);
    InflateRect(&job.bounds, 2, 2);
    int width = job.bounds.right - job.bounds.left;
    int height = job.bounds.bottom - job.bounds.top;
    job.drawnT = -1;

    if (job.onGpu) {
        SetWindowPos(job.ghost, nullptr, job.bounds.left, job.bounds.top, width,
                     height, SWP_NOZORDER | SWP_NOACTIVATE);
        // A new output starts empty, and would stay so until the next frame
        // is drawn, which for a picture waiting to move could be several.
        return gpu::Resize(job.surface, job.ghost, width, height) &&
               (!job.shown ||
                Draw(job, job.shownT, job.shownOpacity, &job.shownFrom,
                     &job.shownTo, job.shownSpread));
    }

    void* bits = nullptr;
    job.dc = CreateCompatibleDC(nullptr);
    job.bitmap = CreateDib(width, height, &bits);
    if (!job.dc || !job.bitmap || !bits) {
        FreeCanvas(job);
        return false;
    }
    job.oldBitmap = SelectObject(job.dc, job.bitmap);
    job.bits = (DWORD*)bits;
    job.drawn = {};

    job.top.assign(width, 0);
    job.rowsPerPixel.assign(width, 0);
    job.sourceX.assign(width, -1);
    return true;
}

// Opaque, with the rounded corners windows have cut out.
void PreparePicture(Snapshot& picture, int radius) {
    for (DWORD& pixel : picture.pixels) {
        pixel |= 0xFF000000;
    }

    radius = std::min({radius, picture.width / 2, picture.height / 2});
    for (int y = 0; y < radius; y++) {
        for (int x = 0; x < radius; x++) {
            double dx = radius - (x + 0.5);
            double dy = radius - (y + 0.5);
            if (dx * dx + dy * dy <= (double)radius * radius) {
                continue;
            }
            int right = picture.width - 1 - x;
            int bottom = picture.height - 1 - y;
            picture.pixels[(size_t)y * picture.width + x] = 0;
            picture.pixels[(size_t)y * picture.width + right] = 0;
            picture.pixels[(size_t)bottom * picture.width + x] = 0;
            picture.pixels[(size_t)bottom * picture.width + right] = 0;
        }
    }
}

inline DWORD Mix(DWORD a, DWORD b, DWORD t) {
    DWORD rb = (((a & 0x00FF00FF) * (256 - t) + (b & 0x00FF00FF) * t) >> 8) &
               0x00FF00FF;
    DWORD ag = (((a >> 8) & 0x00FF00FF) * (256 - t) +
                ((b >> 8) & 0x00FF00FF) * t) &
               0xFF00FF00;
    return rb | ag;
}

// Draws the picture `t` of the way from the window (0) to the card (1). Each
// column of the picture has its own progress: the ones nearest the column of
// cards are ahead. A column moves sideways towards its place in the card,
// while its height and vertical position go from the window's to the card's.
void Render(Job& job,
            double t,
            const RECT& from,
            const RECT& to,
            double spread) {
    int width = job.bounds.right - job.bounds.left;
    int height = job.bounds.bottom - job.bounds.top;

    // Only what the previous frame drew on needs clearing.
    for (int y = job.drawn.top; y < job.drawn.bottom; y++) {
        memset(job.bits + (size_t)y * width + job.drawn.left, 0,
               (size_t)(job.drawn.right - job.drawn.left) * sizeof(DWORD));
    }

    const Snapshot& picture = job.picture;
    const DWORD* source = picture.pixels.data();
    int sourceWidth = picture.width;
    int sourceHeight = picture.height;

    double windowLeft = from.left - job.bounds.left;
    double windowWidth = from.right - from.left;
    double windowHeight = from.bottom - from.top;
    double windowCenterY =
        (from.top + from.bottom) / 2.0 - job.bounds.top;
    double cardLeft = to.left - job.bounds.left;
    double cardWidth = to.right - to.left;
    double cardHeight = to.bottom - to.top;
    double cardCenterY = (to.top + to.bottom) / 2.0 - job.bounds.top;

    auto progressAt = [t, spread](double a) {
        double m = std::clamp(t * (1 + spread) - a * spread, 0.0, 1.0);
        return m * m * (3 - 2 * m);
    };

    // Where some columns of the picture land.
    double landing[kSteps + 1];
    for (int k = 0; k <= kSteps; k++) {
        double a = (double)k / kSteps;
        double m = progressAt(a);
        landing[k] = (windowLeft + a * windowWidth) * (1 - m) +
                     (cardLeft + a * cardWidth) * m;
    }

    int first = std::clamp((int)std::floor(landing[0]), 0, width - 1);
    int last = std::clamp((int)std::ceil(landing[kSteps]), 0, width - 1);
    double minTop = height;
    double maxBottom = 0;

    int k = 0;
    for (int x = first; x <= last; x++) {
        double center = x + 0.5;
        while (k < kSteps - 1 && landing[k + 1] <= center) {
            k++;
        }
        double span = landing[k + 1] - landing[k];
        if (center < landing[k] || center >= landing[k + 1] || span <= 0) {
            job.sourceX[x] = -1;
            continue;
        }

        double a = (k + (center - landing[k]) / span) / kSteps;
        double m = progressAt(a);
        double columnHeight =
            std::max(1.0, windowHeight * (1 - m) + cardHeight * m);
        double columnTop = windowCenterY * (1 - m) + cardCenterY * m -
                           columnHeight / 2;

        job.top[x] = (float)columnTop;
        job.rowsPerPixel[x] = (float)(sourceHeight / columnHeight);
        job.sourceX[x] = (float)std::min(a * sourceWidth, sourceWidth - 1.0);
        minTop = std::min(minTop, columnTop);
        maxBottom = std::max(maxBottom, columnTop + columnHeight);
    }

    int firstRow = std::clamp((int)std::floor(minTop), 0, height);
    int lastRow = std::clamp((int)std::ceil(maxBottom), 0, height);
    job.drawn = {first, firstRow, last + 1, std::max(firstRow, lastRow)};

    // A reduced picture being enlarged is smoothed; one at its real size or
    // being shrunk is copied as is, which is quicker.
    bool smooth = sourceWidth < (int)windowWidth;

    const float* tops = job.top.data();
    const float* rowsPerPixel = job.rowsPerPixel.data();
    const float* sourceXs = job.sourceX.data();

    for (int y = firstRow; y < lastRow; y++) {
        DWORD* row = job.bits + (size_t)y * width;
        float centerY = y + 0.5f;

        for (int x = first; x <= last; x++) {
            float fx = sourceXs[x];
            if (fx < 0) {
                continue;
            }
            float fy = (centerY - tops[x]) * rowsPerPixel[x];
            if (fy < 0 || fy >= sourceHeight) {
                continue;
            }

            if (!smooth) {
                row[x] = source[(int)fy * sourceWidth + (int)fx];
                continue;
            }

            float gx = std::max(0.0f, fx - 0.5f);
            float gy = std::max(0.0f, fy - 0.5f);
            int x0 = (int)gx;
            int y0 = (int)gy;
            int x1 = std::min(x0 + 1, sourceWidth - 1);
            int y1 = std::min(y0 + 1, sourceHeight - 1);
            DWORD tx = (DWORD)((gx - x0) * 256);
            DWORD ty = (DWORD)((gy - y0) * 256);
            const DWORD* row0 = source + (size_t)y0 * sourceWidth;
            const DWORD* row1 = source + (size_t)y1 * sourceWidth;
            row[x] = Mix(Mix(row0[x0], row0[x1], tx),
                         Mix(row1[x0], row1[x1], tx), ty);
        }
    }
}

void Present(Job& job, double opacity) {
    POINT position = {job.bounds.left, job.bounds.top};
    SIZE size = {job.bounds.right - job.bounds.left,
                 job.bounds.bottom - job.bounds.top};
    POINT sourcePoint = {0, 0};
    BLENDFUNCTION blend = {
        AC_SRC_OVER, 0, (BYTE)std::lround(255 * std::clamp(opacity, 0.0, 1.0)),
        AC_SRC_ALPHA};
    UpdateLayeredWindow(job.ghost, nullptr, &position, &size, job.dc,
                        &sourcePoint, 0, &blend, ULW_ALPHA);
}

// Shows the picture `t` of the way from the window to the card. Returns false
// if the graphics card stopped drawing (the animation then ends at once).
bool Draw(Job& job,
          double t,
          double opacity,
          const RECT* from,
          const RECT* to,
          double spread) {
    RECT start = from ? *from : job.window;
    RECT end = to ? *to : job.card;
    if (spread < 0) {
        spread = job.motion.spread;
    }
    job.shown = true;
    job.shownT = t;
    job.shownOpacity = opacity;
    job.shownFrom = start;
    job.shownTo = end;
    job.shownSpread = spread;
    if (job.onGpu) {
        if (gpu::Draw(job.surface, job.bounds, start, end, t, opacity,
                      spread)) {
            return true;
        }
        Wh_Log(L"Graphics card stopped drawing");
        gpu::Free(job.surface);
        gpu::Shutdown();
        job.onGpu = false;
        return false;
    }

    if (t != job.drawnT || spread != job.drawnSpread ||
        !EqualRect(&start, &job.drawnFrom) || !EqualRect(&end, &job.drawnTo)) {
        Render(job, t, start, end, spread);
        job.drawnT = t;
        job.drawnFrom = start;
        job.drawnTo = end;
        job.drawnSpread = spread;
    }
    Present(job, opacity);
    return true;
}

// Puts the pictures above everything, each layer above the one below it.
void RaiseAll() {
    for (int layer = 0; layer <= 1; layer++) {
        for (auto& job : g_jobs) {
            if (job->topmost && job->motion.layer == layer) {
                SetWindowPos(job->ghost, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
    }
}

void NotifyCard(Job& job) {
    if (!job.cardShown) {
        job.cardShown = true;
        PostMessageW(g_msgWnd, WM_APP_GENIE_CARD, (WPARAM)job.target, 0);
    }
}

// Ends the animation wherever it is, leaving everything as it would be at its
// end.
void Finish(Job& job) {
    if (job.arriving) {
        if (!job.windowShown) {
            Reveal(job.target);
        }
    } else {
        NotifyCard(job);
        if (IsWindow(job.target)) {
            SetTransitions(job.target, true);
        }
    }

    FreeCanvas(job);
    gpu::Free(job.surface);
    if (job.ghost) {
        DestroyWindow(job.ghost);
        job.ghost = nullptr;
    }
    g_busy--;
}

void FinishFor(HWND target) {
    for (size_t i = 0; i < g_jobs.size();) {
        if (g_jobs[i]->target == target) {
            auto job = std::move(g_jobs[i]);
            g_jobs.erase(g_jobs.begin() + i);
            Finish(*job);
        } else {
            i++;
        }
    }
}

Job* FindJob(HWND target, bool arriving) {
    for (auto& job : g_jobs) {
        if (job->target == target && job->arriving == arriving) {
            return job.get();
        }
    }
    return nullptr;
}

// Makes the animation of a window, showing its picture still: over the window
// for a leaving one, over the card for an arriving one.
bool Begin(Command& command, bool arriving) {
    FinishFor(command.target);

    auto job = std::make_unique<Job>();
    g_busy++;
    job->target = command.target;
    job->arriving = arriving;
    job->topmost = command.topmost;
    job->picture = std::move(command.picture);
    job->window = command.frame;
    job->card = arriving ? command.card : command.frame;
    job->hasCard = arriving;
    job->delay = command.delay;
    job->motion = command.motion;
    job->created = NowMs();

    int frameWidth =
        std::max(1, (int)(command.frame.right - command.frame.left));
    if (job->picture.width > 0 && job->picture.height > 0) {
        PreparePicture(job->picture,
                       Scale(8) * job->picture.width / frameWidth);
    }
    if (job->picture.pixels.empty()) {
        Finish(*job);
        return false;
    }

    // With the graphics card if possible, else with the processor.
    for (bool onGpu : {true, false}) {
        if (onGpu && !gpu::Init()) {
            continue;
        }
        job->onGpu = onGpu;
        job->ghost = CreateWindowExW(
            (onGpu ? WS_EX_NOREDIRECTIONBITMAP : WS_EX_LAYERED) |
                WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                (job->topmost ? WS_EX_TOPMOST : 0),
            kGhostClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (job->ghost &&
            (!onGpu || gpu::Upload(job->surface, job->picture)) &&
            CreateCanvas(*job) && Draw(*job, arriving ? 1 : 0, 1)) {
            break;
        }

        FreeCanvas(*job);
        gpu::Free(job->surface);
        if (job->ghost) {
            DestroyWindow(job->ghost);
            job->ghost = nullptr;
        }
    }
    if (!job->ghost) {
        Finish(*job);
        return false;
    }

    // Where the window is among the others, or above everything.
    SetWindowPos(job->ghost,
                 job->topmost ? HWND_TOPMOST
                              : (command.above ? command.above : HWND_TOP),
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // A leaving window is minimized once this returns: its picture must be
    // on screen by then, or there'd be a moment with neither. A window just
    // shown may only be composed one screen refresh later, hence two.
    if (!arriving) {
        for (int i = 0; i < 2; i++) {
            if (FAILED(DwmFlush())) {
                Sleep(16);
            }
        }
    }

    g_jobs.push_back(std::move(job));
    RaiseAll();
    return true;
}

void Process(Command& command) {
    bool result = true;

    switch (command.type) {
        case CommandType::Depart:
            result = Begin(command, false);
            break;

        case CommandType::Arrive:
            Begin(command, true);
            break;

        case CommandType::SetCard:
            if (Job* job = FindJob(command.target, false)) {
                if (!job->hasCard || !EqualRect(&command.card, &job->card)) {
                    job->card = command.card;
                    job->hasCard = true;
                    if (!CreateCanvas(*job)) {
                        FinishFor(command.target);
                        break;
                    }
                    if (!job->started && !Draw(*job, 0, 1)) {
                        FinishFor(command.target);
                        break;
                    }
                }
                RaiseAll();
            }
            break;

        case CommandType::Ready:
            if (Job* job = FindJob(command.target, true)) {
                job->ready = true;
                // Not quite where it was expected.
                if (!EqualRect(&command.frame, &job->window)) {
                    job->window = command.frame;
                    job->landed = false;
                    if (!CreateCanvas(*job)) {
                        FinishFor(command.target);
                    }
                }
            }
            break;

        case CommandType::Update:
            // The arriving window's picture, taken again now that the window
            // is back: it replaces the old one, which may be out of date.
            if (Job* job = FindJob(command.target, true)) {
                int frameWidth = std::max(
                    1, (int)(command.frame.right - command.frame.left));
                job->picture = std::move(command.picture);
                PreparePicture(job->picture,
                               Scale(8) * job->picture.width / frameWidth);
                job->drawnT = -1;
                job->landed = false;
                if (job->onGpu) {
                    gpu::ReleasePicture(job->surface);
                    if (!gpu::Upload(job->surface, job->picture)) {
                        FinishFor(command.target);
                    }
                }
            }
            break;

        case CommandType::Behind:
            // Leaves the layer above everything, for the place right below
            // the window in `above`.
            if (Job* job = FindJob(command.target, false)) {
                job->topmost = false;
                SetWindowPos(job->ghost, command.above, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            break;

        case CommandType::Settle:
            FinishFor(command.target);
            break;

        case CommandType::Raise:
            RaiseAll();
            break;

        case CommandType::CancelAll:
            while (!g_jobs.empty()) {
                auto job = std::move(g_jobs.back());
                g_jobs.pop_back();
                Finish(*job);
            }
            break;
    }

    if (command.reply) {
        command.reply->result = result;
        SetEvent(command.reply->done);
    }
    g_busy--;
}

// Where a leaving window is after shrinking (Shrink): the size of its card,
// or smaller if the window is smaller, in the middle of where it was.
RECT ShrunkRect(const Job& job) {
    double width = job.card.right - job.card.left;
    double height = job.card.bottom - job.card.top;
    double windowWidth = job.window.right - job.window.left;
    double windowHeight = job.window.bottom - job.window.top;
    double scale = std::min({1.0, windowWidth / width, windowHeight / height});
    width *= scale;
    height *= scale;
    double centerX = (job.window.left + job.window.right) / 2.0;
    double centerY = (job.window.top + job.window.bottom) / 2.0;
    return {(LONG)(centerX - width / 2), (LONG)(centerY - height / 2),
            (LONG)(centerX + width / 2 + 0.5), (LONG)(centerY + height / 2 + 0.5)};
}

// Draws the next frame of one animation. Returns true once it's over.
bool Advance(Job& job, double now) {
    if (!IsWindow(job.target)) {
        return true;
    }
    if (!job.hasCard) {
        return now - job.created > kPatienceMs;
    }

    // The clock starts at the first frame, so that what's done between
    // preparing the animation and drawing it isn't skipped over.
    if (!job.started) {
        job.started = true;
        job.start = now + job.delay;
    }
    double duration = job.motion.duration;
    double progress = (now - job.start) / duration;
    if (progress <= 0) {
        return false;
    }

    if (!job.arriving) {
        if (progress >= 1) {
            return true;
        }
        if (progress >= kFadeFrom) {
            NotifyCard(job);
        }
        double opacity =
            progress > kFadeFrom ? (1 - progress) / (1 - kFadeFrom) : 1;
        if (job.motion.shrink) {
            double share = job.motion.shrinkShare;
            RECT mid = ShrunkRect(job);
            if (progress < share) {
                return !Draw(job, progress / share, opacity, &job.window,
                             &mid, job.motion.shrinkSpread);
            }
            return !Draw(job, (progress - share) / (1 - share), opacity,
                         &mid, &job.card, 0);
        }
        return !Draw(job, progress, opacity);
    }

    if (progress < 1) {
        return !Draw(job, 1 - progress, 1);
    }

    // Over the place of the window, until it's there.
    if (!job.landed) {
        job.landed = true;
        if (!Draw(job, 0, 1)) {
            return true;
        }
    }
    if (!job.ready) {
        return now - job.start - duration > kPatienceMs;
    }

    // The window is shown under its picture, which stays a moment longer:
    // some programs take that long to draw themselves again once shown, and
    // would otherwise show up empty for a moment.
    if (!job.windowShown) {
        job.windowShown = true;
        job.fadeStart = now + kHoldMs;
        Reveal(job.target);
    }
    if (now < job.fadeStart) {
        return false;
    }
    double fade = (now - job.fadeStart) / kCrossFadeMs;
    if (fade >= 1) {
        return true;
    }
    return !Draw(job, 0, 1 - fade);
}

DWORD WINAPI ThreadProc(LPVOID) {
    // Window and monitor coordinates in physical pixels, as on the main thread.
    using SetThreadDpiAwarenessContext_t =
        DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto pSetThreadDpiAwarenessContext =
            (SetThreadDpiAwarenessContext_t)GetProcAddress(
                GetModuleHandleW(L"user32.dll"),
                "SetThreadDpiAwarenessContext")) {
        pSetThreadDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-4);
    }

    while (!g_quit) {
        std::vector<Command> commands;
        AcquireSRWLockExclusive(&g_lock);
        commands.swap(g_commands);
        ReleaseSRWLockExclusive(&g_lock);
        for (Command& command : commands) {
            Process(command);
        }

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_jobs.empty()) {
            // The event stays set if something was asked for meanwhile.
            MsgWaitForMultipleObjects(1, &g_wake, FALSE, INFINITE,
                                      QS_ALLINPUT);
            continue;
        }

        // A frame per screen refresh.
        double now = NowMs();
        for (size_t i = 0; i < g_jobs.size();) {
            if (Advance(*g_jobs[i], now)) {
                auto job = std::move(g_jobs[i]);
                g_jobs.erase(g_jobs.begin() + i);
                Finish(*job);
            } else {
                i++;
            }
        }
        if (FAILED(DwmFlush())) {
            Sleep(8);
        }
    }

    while (!g_jobs.empty()) {
        auto job = std::move(g_jobs.back());
        g_jobs.pop_back();
        Finish(*job);
    }
    gpu::Shutdown();
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Main thread

// Hands a request to the animation thread. With `wait`, returns its result
// once it's done; the animation thread never waits for this one, so that's
// safe.
bool Send(Command&& command, bool wait = false) {
    if (!g_thread) {
        return false;
    }

    std::shared_ptr<Reply> reply;
    if (wait) {
        reply = std::make_shared<Reply>();
        if (!reply->done) {
            return false;
        }
        command.reply = reply;
    }

    g_busy++;
    AcquireSRWLockExclusive(&g_lock);
    g_commands.push_back(std::move(command));
    ReleaseSRWLockExclusive(&g_lock);
    SetEvent(g_wake);

    return reply && WaitForSingleObject(reply->done, 2000) == WAIT_OBJECT_0 &&
           reply->result;
}

void Start() {
    g_quit = false;
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_wake) {
        g_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    }
    if (g_thread) {
        SetThreadPriority(g_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    }
}

void Stop() {
    if (g_thread) {
        g_quit = true;
        SetEvent(g_wake);
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = nullptr;
    }
    g_commands.clear();
    g_hiddenCards.clear();
}

Card* FindCard(HWND target) {
    for (auto& card : g_cards) {
        if (card->target == target && card->wnd) {
            return card.get();
        }
    }
    return nullptr;
}

// The card's place as if it weren't tilted.
bool CardRect(HWND target, RECT* rect) {
    Card* card = FindCard(target);
    if (!card) {
        return false;
    }
    *rect = card->flat;
    return rect->right > rect->left && rect->bottom > rect->top;
}

// The card of a window still on its way there isn't shown yet.
bool HidesCard(HWND target) {
    return std::find(g_hiddenCards.begin(), g_hiddenCards.end(), target) !=
           g_hiddenCards.end();
}

// The window's picture is nearly at its card, or its animation ended.
void CardReached(HWND target) {
    auto it = std::find(g_hiddenCards.begin(), g_hiddenCards.end(), target);
    if (it == g_hiddenCards.end()) {
        return;
    }
    g_hiddenCards.erase(it);

    if (Card* card = FindCard(target)) {
        ShowWindow(card->wnd, SW_SHOWNOACTIVATE);
        if (card->iconWnd) {
            ShowWindow(card->iconWnd, SW_SHOWNOACTIVATE);
        }
    }
}

// Ends the window's animation, if it's in one.
void Settle(HWND target) {
    if (g_busy == 0) {
        return;
    }
    Command command;
    command.type = CommandType::Settle;
    command.target = target;
    Send(std::move(command), true);
    CardReached(target);
}

void CancelAll() {
    if (g_busy != 0) {
        Command command;
        command.type = CommandType::CancelAll;
        Send(std::move(command), true);
    }
    while (!g_hiddenCards.empty()) {
        CardReached(g_hiddenCards.back());
    }
}

// A window about to be minimized into the column: its picture takes its place
// at once, and stays still until the card it goes to is known (CardsRebuilt),
// then `delay` ms more. `frame` is the window's visible frame, which the
// picture is of. With `above`, the picture goes right below that window
// rather than above everything.
bool Depart(HWND target,
            Snapshot&& picture,
            const RECT& frame,
            double delay,
            bool topmost,
            HWND above) {
    Command command;
    command.type = CommandType::Depart;
    command.target = target;
    command.picture = std::move(picture);
    command.frame = frame;
    command.delay = delay;
    command.topmost = topmost;
    command.above = above;
    command.motion = MotionFor(false);
    if (!Send(std::move(command), true)) {
        return false;
    }
    g_hiddenCards.push_back(target);
    return true;
}

// A window coming out of its card, concealed, towards where its frame will
// be. Ready is to follow, once the window is there.
void Arrive(HWND target,
            Snapshot&& picture,
            const RECT& card,
            const RECT& frame) {
    Command command;
    command.type = CommandType::Arrive;
    command.target = target;
    command.picture = std::move(picture);
    command.card = card;
    command.frame = frame;
    command.motion = MotionFor(true);
    Send(std::move(command));
}

// The picture of the window `leaving` goes on behind `window`, the one now on
// stage: as it heads for the column it passes under it, and under the cards,
// instead of over everything.
void Behind(HWND leaving, HWND window) {
    if (!HidesCard(leaving) || !IsWindow(window)) {
        return;
    }
    Command command;
    command.type = CommandType::Behind;
    command.target = leaving;
    command.above = window;
    Send(std::move(command));
}

// The window's picture, taken again now that it's back on stage.
void Update(HWND target, Snapshot&& picture, const RECT& frame) {
    Command command;
    command.type = CommandType::Update;
    command.target = target;
    command.picture = std::move(picture);
    command.frame = frame;
    Send(std::move(command));
}

void Ready(HWND target, const RECT& frame) {
    Command command;
    command.type = CommandType::Ready;
    command.target = target;
    command.frame = frame;
    Send(std::move(command));
}

// The cards were laid out again: the leaving windows learn where to go.
void CardsRebuilt() {
    for (size_t i = 0; i < g_hiddenCards.size();) {
        Command command;
        command.target = g_hiddenCards[i];
        if (CardRect(command.target, &command.card)) {
            command.type = CommandType::SetCard;
            Send(std::move(command));
            i++;
        } else {
            // No card to go to.
            command.type = CommandType::Settle;
            Send(std::move(command));
            g_hiddenCards.erase(g_hiddenCards.begin() + i);
        }
    }

    // Above the cards, which were just raised.
    if (g_busy != 0) {
        Send(Command{});
    }
}

}  // namespace genie

////////////////////////////////////////////////////////////////////////////////
// Turning on and off, switching

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hWnd, LONG idObject,
                           LONG idChild, DWORD, DWORD);

// Ctrl+Shift+click on one of two stage windows sends it back to the column.
// The click itself is kept from the window.
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_active) {
        const MSLLHOOKSTRUCT* mouse = (const MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_LBUTTONDOWN && g_stage.size() == 2 &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            HWND target =
                GetAncestor(WindowFromPoint(mouse->pt), GA_ROOTOWNER);
            if (OnStage(target)) {
                PostMessageW(g_msgWnd, WM_APP_REMOVE, (WPARAM)target, 0);
                g_swallowMouseUp = true;
                return 1;
            }
        } else if (wParam == WM_LBUTTONUP && g_swallowMouseUp) {
            g_swallowMouseUp = false;
            PostMessageW(g_msgWnd, WM_APP_MOUSEHOOK, 0, 0);
            return 1;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// The hook is only there while two windows share the stage, which is when
// it's of use: every movement of the mouse goes through this thread while a
// low-level mouse hook is installed, and waits whenever the thread is busy.
void UpdateMouseHook() {
    bool wanted = g_active && (g_stage.size() == 2 || g_swallowMouseUp);
    if (wanted && !g_mouseHook) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc,
                                        GetModuleHandleW(nullptr), 0);
    } else if (!wanted && g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

void InstallWinEventHooks() {
    DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    g_winEventHooks[0] =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                        nullptr, WinEventProc, 0, 0, flags);
    g_winEventHooks[1] =
        SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZESTART,
                        nullptr, WinEventProc, 0, 0, flags);
    g_winEventHooks[2] =
        SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr,
                        WinEventProc, 0, 0, flags);
    g_winEventHooks[3] =
        SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND,
                        nullptr, WinEventProc, 0, 0, flags);
    g_winEventHooks[4] =
        SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,
                        EVENT_OBJECT_LOCATIONCHANGE, nullptr, WinEventProc, 0,
                        0, flags);

    UpdateMouseHook();
}

void RemoveWinEventHooks() {
    for (auto& hook : g_winEventHooks) {
        if (hook) {
            UnhookWinEvent(hook);
            hook = nullptr;
        }
    }

    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    g_swallowMouseUp = false;

    KillTimer(g_msgWnd, kZoomCheckTimer);
    KillTimer(g_msgWnd, kAdoptRetryTimer);
    g_pendingAdopt = nullptr;
    g_resizing = nullptr;
    DestroyDivider();
}

// A hidden window of ours, only there to carry the property below.
HWND g_markerWnd;

// The Windows Animations mod, when in use, is loaded into this process too,
// and animates towards the taskbar any window minimized from here. It leaves
// a window alone when another process is already animating it, which it tells
// from a window with this property (its kPropAnimationGhost) naming it. Set
// while one of ours is minimized, so that it's only minimized.
struct ForeignAnimationGuard {
    static constexpr WCHAR kProp[] = L"windows-animations.AnimationGhostV1";

    explicit ForeignAnimationGuard(HWND target) {
        if (g_markerWnd) {
            SetPropW(g_markerWnd, kProp, (HANDLE)target);
        }
    }
    ~ForeignAnimationGuard() {
        if (g_markerWnd) {
            RemovePropW(g_markerWnd, kProp);
        }
    }
};

// Takes the window's picture and minimizes it into the column.
// `animate`: the picture is animated into the window's card, starting `delay`
// ms after the card is there. `ownSize`: the window gets back the size it had
// before Stage Manager, rather than the one it had on stage. `inPlace`: the
// picture stays where the window is among the others, rather than above them.
// `taken`: its picture, already taken (with GrabWindow), and its frame.
void SendToStrip(HWND hWnd,
                 bool animate,
                 double delay = 0,
                 bool ownSize = true,
                 bool inPlace = false,
                 Snapshot* taken = nullptr,
                 const RECT* takenFrame = nullptr) {
    // Whatever animation it was in ends here.
    if (!taken) {
        genie::Settle(hWnd);
    }

    Snapshot picture;
    RECT frame{};
    bool grabbed = false;
    if (taken && takenFrame) {
        picture = std::move(*taken);
        frame = *takenFrame;
        grabbed = true;
    } else {
        // From the screen while no animation is on it.
        grabbed = GrabWindow(hWnd, &picture, &frame, genie::g_busy == 0);
    }

    // Its picture takes its place before it's minimized, and is animated into
    // its card once that exists.
    bool departing = false;
    if (grabbed && animate) {
        Snapshot copy = picture;
        departing = genie::Depart(hWnd, std::move(copy), frame, delay,
                                  !inPlace, GetWindow(hWnd, GW_HWNDPREV));
    }

    {
        ForeignAnimationGuard guard(hWnd);
        genie::SetTransitions(hWnd, false);

        // Minimized first, and only then given back its own size: resized
        // while still on screen, it would show up at that size for a moment.
        ShowWindow(hWnd, SW_SHOWMINNOACTIVE);

        WINDOWPLACEMENT wp{sizeof(wp)};
        auto saved = g_saved.find(hWnd);
        if (ownSize && saved != g_saved.end() && IsIconic(hWnd) &&
            GetWindowPlacement(hWnd, &wp)) {
            wp.rcNormalPosition = saved->second.rcNormalPosition;
            wp.showCmd = SW_SHOWMINNOACTIVE;
            // Restored by Stage Manager itself, to its normal state.
            wp.flags &= ~WPF_RESTORETOMAXIMIZED;
            SetWindowPlacement(hWnd, &wp);
        }

        // Otherwise turned back on when the animation ends.
        if (!departing) {
            genie::SetTransitions(hWnd, true);
        }
    }

    if (grabbed) {
        StoreSnapshot(hWnd, std::move(picture));
    }
}

// A cheap fingerprint of a picture, to tell whether it changed.
unsigned long long Fingerprint(const Snapshot& picture) {
    unsigned long long hash = 1469598103934665603ull;
    for (size_t i = 0; i < picture.pixels.size(); i += 13) {
        hash = (hash ^ picture.pixels[i]) * 1099511628211ull;
    }
    return hash;
}

// The window as it is now, once it has finished drawing itself: a window just
// restored from the column may show its old contents for a moment, so it's
// photographed again until two pictures in a row come out the same.
bool GrabSettled(HWND hWnd, Snapshot* picture, RECT* frame) {
    DWORD start = GetTickCount();
    unsigned long long last = 0;
    bool have = false;
    for (int attempt = 0; attempt < 4; attempt++) {
        Snapshot candidate;
        RECT rect{};
        if (!GrabWindow(hWnd, &candidate, &rect)) {
            return have;
        }
        unsigned long long print = Fingerprint(candidate);
        bool same = have && print == last;
        *picture = std::move(candidate);
        *frame = rect;
        have = true;
        last = print;
        if (same || GetTickCount() - start > 350) {
            break;
        }
        Sleep(45);
    }
    return have;
}

// Brings the stage windows in front of the cards, without activating them,
// `top` in front of the other one.
void RaiseStage(HWND top) {
    for (HWND hWnd : g_stage) {
        if (hWnd != top && IsWindow(hWnd)) {
            SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if (IsWindow(top)) {
        SetWindowPos(top, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// Puts `target` on stage. With `add`, next to the window already there, if
// there's room for one more; otherwise in place of the stage window in use,
// which goes to the column while any other stage window stays put.
//
// What's seen comes first, so that it starts at once: the pictures of the
// window leaving and of the one arriving begin to move, and only then is the
// arriving window, unseen, restored and put in place, which waits for its
// program and can take a while.
void SwitchTo(HWND target, bool activate, bool add = false) {
    if (!g_active || !IsWindow(target)) {
        return;
    }

    if (OnStage(target)) {
        g_focused = target;
        if (activate) {
            BringToFront(target);
        }
        return;
    }

    SavePlacement(target);

    // A window taken from the column comes out of its card.
    RECT card{};
    bool arriving = g_settings.genieEnabled && IsIconic(target) &&
                    g_snapshots.count(target) && genie::CardRect(target, &card);

    g_strip.erase(std::remove(g_strip.begin(), g_strip.end(), target),
                  g_strip.end());

    // Changed before anything moves, so that the events this causes are
    // recognized as ours.
    HWND replaced = nullptr;
    if (g_stage.empty() || (add && g_stage.size() < 2)) {
        g_stage.push_back(target);
    } else {
        auto slot = std::find(g_stage.begin(), g_stage.end(), g_focused);
        if (slot == g_stage.end()) {
            slot = g_stage.end() - 1;
        }
        replaced = *slot;
        *slot = target;
    }
    g_focused = target;

    // The picture of the window leaving, first: copied from the screen, which
    // is quick, while nothing covers it yet.
    Snapshot leaving;
    RECT leavingFrame{};
    bool leavingTaken = false;
    if (replaced && IsWindow(replaced)) {
        genie::Settle(replaced);
        leavingTaken = GrabWindow(replaced, &leaving, &leavingFrame,
                                  genie::g_busy == 0);
    }

    if (arriving) {
        genie::Settle(target);
        arriving = genie::Conceal(target);
    }

    // Its picture at full size if there is one, else the card's.
    Snapshot picture;
    bool flying = false;
    if (arriving) {
        Snapshot& kept = g_snapshots[target];
        if (!kept.full.empty()) {
            picture.width = kept.fullWidth;
            picture.height = kept.fullHeight;
            picture.pixels = std::move(kept.full);
            kept.full.clear();
        } else {
            picture.width = kept.width;
            picture.height = kept.height;
            picture.pixels = kept.pixels;
        }

        RECT expected;
        if (PredictStageFrame(target, &expected)) {
            genie::Arrive(target, std::move(picture), card, expected);
            flying = true;
        }
    }

    if (replaced && IsWindow(replaced)) {
        // With Overlay, the leaving window stays put while the arriving one
        // covers it, and only then goes to the column.
        double delay =
            arriving && g_settings.animation == SwitchAnimation::Overlay
                ? g_settings.genieDuration * 0.3
                : 0;
        if (leavingTaken) {
            SendToStrip(replaced, g_settings.genieEnabled, delay, true, false,
                        &leaving, &leavingFrame);
        } else {
            SendToStrip(replaced, g_settings.genieEnabled, delay);
        }
        g_strip.insert(g_strip.begin(), replaced);
    }

    RebuildCards(false);

    LayoutStage();

    // The frame the arriving window ended up in, once it's known: it's then
    // photographed again, and only afterwards told it can show.
    RECT readyFrame{};
    bool readyPending = false;

    if (arriving) {
        RECT rect;
        RECT frame{};
        auto insets = g_frameInsets.find(target);
        if (!IsIconic(target) && insets != g_frameInsets.end() &&
            GetWindowRect(target, &rect)) {
            frame = {rect.left + insets->second.left,
                     rect.top + insets->second.top,
                     rect.right - insets->second.right,
                     rect.bottom - insets->second.bottom};
        }

        if (frame.right > frame.left && frame.bottom > frame.top) {
            if (!flying) {
                genie::Arrive(target, std::move(picture), card, frame);
            }
            readyFrame = frame;
            readyPending = true;
        } else if (flying) {
            genie::Settle(target);
        } else {
            genie::Reveal(target);
        }
    }

    if (activate) {
        BringToFront(target);
    }
    RaiseStage(target);
    if (replaced) {
        genie::Behind(replaced, target);
    }

    // Last, because it can take a moment (the animation meanwhile goes on by
    // itself, and waits at the end for this): the picture the animation ends
    // on is a new one of the window, not the one from when it went to the
    // column, so that nothing changes when the real window takes its place.
    if (readyPending) {
        Snapshot fresh;
        RECT freshFrame{};
        if (GrabSettled(target, &fresh, &freshFrame)) {
            genie::Update(target, std::move(fresh), freshFrame);
            readyFrame = freshFrame;
        }
        genie::Ready(target, readyFrame);
    }
}

// Sends one of two stage windows back to the column; the other one goes back
// to its usual size and place.
void RemoveFromStage(HWND hWnd) {
    if (!g_active || g_stage.size() < 2 || !OnStage(hWnd)) {
        return;
    }

    g_stage.erase(std::remove(g_stage.begin(), g_stage.end(), hWnd),
                  g_stage.end());
    g_focused = g_stage.front();

    SendToStrip(hWnd, g_settings.genieEnabled);
    g_strip.insert(g_strip.begin(), hWnd);
    RebuildCards(false);

    LayoutStage();
    BringToFront(g_focused);
    RaiseStage(g_focused);
    genie::Behind(hWnd, g_focused);
}

////////////////////////////////////////////////////////////////////////////////
// Virtual desktops
//
// Each virtual desktop has its own Stage Manager state. Windows only expose
// which desktop they're on (IVirtualDesktopManager), not which one is current,
// so the current one is read from where Explorer records it in the registry,
// which also gives a change notification when the user switches.

struct GuidLess {
    bool operator()(const GUID& a, const GUID& b) const {
        return memcmp(&a, &b, sizeof(GUID)) < 0;
    }
};

// {AA509086-5CA9-4C25-8F95-589D3C07B48A}
constexpr GUID kClsidVirtualDesktopManager = {
    0xaa509086, 0x5ca9, 0x4c25, {0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a}};
// {A5CD92FF-29BE-454C-8D04-D82879FB3F1B}
constexpr GUID kIidVirtualDesktopManager = {
    0xa5cd92ff, 0x29be, 0x454c, {0x8d, 0x04, 0xd8, 0x28, 0x79, 0xfb, 0x3f, 0x1b}};

IVirtualDesktopManager* g_desktopManager;
HKEY g_desktopKey;
HANDLE g_desktopChangedEvent;
GUID g_currentDesktop;

void OpenDesktopKey() {
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                      L"VirtualDesktops",
                      0, KEY_READ | KEY_NOTIFY,
                      &g_desktopKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(GUID);
        GUID id;
        if (RegQueryValueExW(g_desktopKey, L"CurrentVirtualDesktop", nullptr,
                             nullptr, (BYTE*)&id, &size) == ERROR_SUCCESS) {
            return;
        }
        RegCloseKey(g_desktopKey);
        g_desktopKey = nullptr;
    }

    // Windows 10 keeps it per session.
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    WCHAR path[200];
    swprintf_s(path,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
               L"SessionInfo\\%u\\VirtualDesktops",
               sessionId);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ | KEY_NOTIFY,
                      &g_desktopKey) != ERROR_SUCCESS) {
        g_desktopKey = nullptr;
    }
}

GUID ReadCurrentDesktop() {
    GUID id = {};
    DWORD size = sizeof(id);
    if (g_desktopKey) {
        RegQueryValueExW(g_desktopKey, L"CurrentVirtualDesktop", nullptr,
                         nullptr, (BYTE*)&id, &size);
    }
    return id;
}

void WatchDesktopKey() {
    if (g_desktopKey && g_desktopChangedEvent) {
        RegNotifyChangeKeyValue(
            g_desktopKey, FALSE,
            REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC,
            g_desktopChangedEvent, TRUE);
    }
}

// Whether the window belongs to the desktop Stage Manager is working on. When
// that can't be told, it's assumed to.
bool IsOnStageDesktop(HWND hWnd) {
    if (!g_desktopManager || !g_desktopKey) {
        return true;
    }
    GUID id = {};
    if (FAILED(g_desktopManager->GetWindowDesktopId(hWnd, &id))) {
        return true;
    }
    // Windows shown on all desktops, or not assigned to any yet.
    static const GUID kNull = {};
    if (memcmp(&id, &kNull, sizeof(GUID)) == 0) {
        return true;
    }
    return memcmp(&id, &g_currentDesktop, sizeof(GUID)) == 0;
}

BOOL CALLBACK CollectStageWindowsProc(HWND hWnd, LPARAM lParam) {
    if (IsCandidate(hWnd) && IsOnStageDesktop(hWnd) &&
        MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST) == g_monitor) {
        ((std::vector<HWND>*)lParam)->push_back(hWnd);
    }
    return TRUE;
}

void Enter() {
    if (g_active) {
        return;
    }

    HWND foreground =
        GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
    if (!IsCandidate(foreground) || IsIconic(foreground)) {
        foreground = nullptr;
    }

    if (foreground) {
        g_monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT pt;
        GetCursorPos(&pt);
        g_monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(g_monitor, &mi)) {
        return;
    }
    g_workArea = mi.rcWork;
    g_dpi = MonitorDpi(g_monitor);

    // The windows on this monitor, minimized ones included, top of the Z-order
    // first.
    std::vector<HWND> windows;
    EnumWindows(CollectStageWindowsProc, (LPARAM)&windows);

    if (!foreground) {
        for (HWND hWnd : windows) {
            if (!IsIconic(hWnd)) {
                foreground = hWnd;
                break;
            }
        }
    }
    if (!foreground && !windows.empty()) {
        foreground = windows.front();
    }

    if (!foreground) {
        Wh_Log(L"No window to put on stage");
        return;
    }

    Wh_Log(L"Turning on, %zu windows", windows.size());

    g_active = true;
    g_stage = {foreground};
    g_focused = foreground;
    g_strip.clear();

    for (HWND hWnd : windows) {
        SavePlacement(hWnd);
    }
    SavePlacement(foreground);

    // Which windows can be seen, before anything moves: those are animated
    // into their cards. The ones hidden behind others just go.
    std::vector<HWND> seen;
    if (g_settings.genieEnabled) {
        HRGN covered = CreateRectRgn(0, 0, 0, 0);
        HRGN showing = CreateRectRgn(0, 0, 0, 0);
        for (HWND hWnd : windows) {
            RECT frame;
            if (IsIconic(hWnd) ||
                FAILED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                             &frame, sizeof(frame)))) {
                continue;
            }
            HRGN own = CreateRectRgnIndirect(&frame);
            if (CombineRgn(showing, own, covered, RGN_DIFF) > NULLREGION &&
                hWnd != foreground) {
                seen.push_back(hWnd);
            }
            CombineRgn(covered, covered, own, RGN_OR);
            DeleteObject(own);
        }
        DeleteObject(covered);
        DeleteObject(showing);
    }

    LayoutStage();
    BringToFront(foreground);

    // One after the other, a little apart, and no more than a few at once:
    // each is a full window being redrawn at every frame.
    constexpr int kMaxAnimated = 4;
    constexpr double kStaggerMs = 50;
    int animated = 0;
    for (HWND hWnd : windows) {
        if (hWnd == foreground) {
            continue;
        }
        bool animate =
            animated < kMaxAnimated &&
            (int)g_strip.size() < g_settings.maxCards &&
            std::find(seen.begin(), seen.end(), hWnd) != seen.end();
        SendToStrip(hWnd, animate, animated * kStaggerMs, false, true);
        if (animate) {
            animated++;
        }
        g_strip.push_back(hWnd);
    }

    InstallWinEventHooks();
    RebuildCards(true);
}

// `activate`: bring the stage window to the front at the end. Not done for a
// desktop that isn't the current one, as that would switch to it.
void Exit(bool activate = true) {
    if (!g_active) {
        return;
    }

    Wh_Log(L"Turning off");

    genie::CancelAll();
    g_active = false;
    RemoveWinEventHooks();
    KillTimer(g_msgWnd, kRefreshTimer);
    DestroyCards();
    g_freshUntil.clear();

    // Oldest first, so the most recent windows end up on top, and the stage
    // window last.
    std::vector<HWND> order(g_strip.rbegin(), g_strip.rend());
    for (HWND hWnd : g_stage) {
        if (hWnd != g_focused) {
            order.push_back(hWnd);
        }
    }
    if (OnStage(g_focused)) {
        order.push_back(g_focused);
    }

    for (HWND hWnd : order) {
        auto it = g_saved.find(hWnd);
        if (!IsWindow(hWnd) || it == g_saved.end()) {
            continue;
        }
        WINDOWPLACEMENT wp = it->second;
        bool wasMinimized = wp.showCmd == SW_SHOWMINIMIZED ||
                            wp.showCmd == SW_MINIMIZE ||
                            wp.showCmd == SW_SHOWMINNOACTIVE;
        if (wasMinimized && !OnStage(hWnd)) {
            // Was minimized before, and still is: nothing to put back.
            if (IsIconic(hWnd)) {
                continue;
            }
            wp.showCmd = SW_SHOWMINNOACTIVE;
        } else if (hWnd != g_focused && wp.showCmd == SW_SHOWNORMAL) {
            wp.showCmd = SW_SHOWNOACTIVATE;
        }
        SetWindowPlacement(hWnd, &wp);
    }

    if (activate && g_focused && IsWindow(g_focused)) {
        BringToFront(g_focused);
    }

    g_stage.clear();
    g_focused = nullptr;
    g_strip.clear();
    g_saved.clear();
    g_frameInsets.clear();
    g_snapshots.clear();
}

void Toggle() {
    if (g_active) {
        Exit();
    } else {
        Enter();
    }
}

// Drops windows that were closed or hidden, and fills the stage if its window
// went away.
void Refresh() {
    if (!g_active) {
        return;
    }

    auto gone = [](HWND hWnd) {
        return !IsWindow(hWnd) || !IsWindowVisible(hWnd) ||
               !IsOnStageDesktop(hWnd);
    };

    g_strip.erase(std::remove_if(g_strip.begin(), g_strip.end(), gone),
                  g_strip.end());

    for (auto it = g_saved.begin(); it != g_saved.end();) {
        it = IsWindow(it->first) ? std::next(it) : g_saved.erase(it);
    }

    // The cards point into the pictures: gone before any of those is dropped.
    bool snapshotGone = false;
    for (const auto& [hWnd, snapshot] : g_snapshots) {
        snapshotGone = snapshotGone || !IsWindow(hWnd);
    }
    if (snapshotGone) {
        DestroyCards();
        for (auto it = g_snapshots.begin(); it != g_snapshots.end();) {
            it = IsWindow(it->first) ? std::next(it) : g_snapshots.erase(it);
        }
    }

    g_stage.erase(std::remove_if(g_stage.begin(), g_stage.end(), gone),
                  g_stage.end());
    if (!OnStage(g_focused)) {
        g_focused = g_stage.empty() ? nullptr : g_stage.back();
    }

    if (g_stage.empty()) {
        if (!g_strip.empty()) {
            SwitchTo(g_strip.front(), true);
            return;
        }
    } else if (g_stage.size() != g_laidOutCount) {
        // One of two stage windows left: center the other.
        LayoutStage();
    }

    RebuildCards(false);
}

struct DesktopState {
    HMONITOR monitor = nullptr;
    RECT workArea{};
    UINT dpi = 96;
    std::vector<HWND> stage;
    HWND focused = nullptr;
    double splitRatio = 0.5;
    std::vector<HWND> strip;
    std::unordered_map<HWND, WINDOWPLACEMENT> saved;
    std::unordered_map<HWND, RECT> frameInsets;
    std::unordered_map<HWND, Snapshot> snapshots;
};

// Desktops where Stage Manager is on, other than the current one.
std::map<GUID, DesktopState, GuidLess> g_suspendedDesktops;

void LoadDesktopState(DesktopState&& state) {
    g_active = true;
    g_monitor = state.monitor;
    g_workArea = state.workArea;
    g_dpi = state.dpi;
    g_stage = std::move(state.stage);
    g_focused = state.focused;
    g_splitRatio = state.splitRatio;
    g_laidOutCount = g_stage.size();
    g_strip = std::move(state.strip);
    g_saved = std::move(state.saved);
    g_frameInsets = std::move(state.frameInsets);
    g_snapshots = std::move(state.snapshots);
}

// Leaving the current desktop: its windows stay where they are, its column is
// put away until the user comes back.
void SuspendCurrentDesktop() {
    if (!g_active) {
        return;
    }

    genie::CancelAll();
    RemoveWinEventHooks();
    KillTimer(g_msgWnd, kRefreshTimer);
    DestroyCards();

    DesktopState state;
    state.monitor = g_monitor;
    state.workArea = g_workArea;
    state.dpi = g_dpi;
    state.stage = std::move(g_stage);
    state.focused = g_focused;
    state.splitRatio = g_splitRatio;
    state.strip = std::move(g_strip);
    state.saved = std::move(g_saved);
    state.frameInsets = std::move(g_frameInsets);
    state.snapshots = std::move(g_snapshots);
    g_suspendedDesktops[g_currentDesktop] = std::move(state);

    g_active = false;
    g_stage.clear();
    g_focused = nullptr;
    g_strip.clear();
    g_saved.clear();
    g_frameInsets.clear();
    g_snapshots.clear();
}

void ResumeCurrentDesktop() {
    auto it = g_suspendedDesktops.find(g_currentDesktop);
    if (it == g_suspendedDesktops.end()) {
        return;
    }

    LoadDesktopState(std::move(it->second));
    g_suspendedDesktops.erase(it);

    InstallWinEventHooks();
    Refresh();
    if (g_active) {
        LayoutStage();
        RebuildCards(true);
    }
}

void CheckDesktopSwitch() {
    if (!g_desktopKey) {
        return;
    }

    GUID current = ReadCurrentDesktop();
    if (memcmp(&current, &g_currentDesktop, sizeof(GUID)) == 0) {
        return;
    }

    Wh_Log(L"Virtual desktop switched");
    SuspendCurrentDesktop();
    g_currentDesktop = current;
    ResumeCurrentDesktop();
}

bool CoversMonitor(HWND hWnd) {
    RECT rect;
    MONITORINFO mi{sizeof(mi)};
    if (!GetWindowRect(hWnd, &rect) || !GetMonitorInfoW(g_monitor, &mi)) {
        return false;
    }
    return rect.left <= mi.rcMonitor.left && rect.top <= mi.rcMonitor.top &&
           rect.right >= mi.rcMonitor.right &&
           rect.bottom >= mi.rcMonitor.bottom;
}

// A stage window went maximized or full screen. If it just appeared, that's
// the app opening maximized: it's put back to the usual size. Otherwise the
// user asked for it, and Stage Manager turns off, leaving that window as it is.
void CheckStageZoom() {
    if (!g_active) {
        return;
    }

    for (HWND hWnd : std::vector<HWND>(g_stage)) {
        if (!IsWindow(hWnd) || !(IsZoomed(hWnd) || CoversMonitor(hWnd))) {
            continue;
        }

        auto fresh = g_freshUntil.find(hWnd);
        if (fresh != g_freshUntil.end() && GetTickCount64() < fresh->second) {
            Wh_Log(L"New window opened maximized, resizing it");
            LayoutStage();
            return;
        }

        Wh_Log(L"Stage window went full screen, turning off");
        g_saved.erase(hWnd);
        g_focused = hWnd;
        Exit();
        return;
    }
}

// Turns Stage Manager off everywhere, putting the windows of every desktop
// back.
void ExitAll() {
    Exit();
    for (auto& [id, state] : g_suspendedDesktops) {
        LoadDesktopState(std::move(state));
        Exit(false);
    }
    g_suspendedDesktops.clear();
}

void CALLBACK WinEventProc(HWINEVENTHOOK,
                           DWORD event,
                           HWND hWnd,
                           LONG idObject,
                           LONG idChild,
                           DWORD,
                           DWORD) {
    if (!g_active || !hWnd || idObject != OBJID_WINDOW ||
        idChild != CHILDID_SELF) {
        return;
    }

    switch (event) {
        case EVENT_SYSTEM_FOREGROUND: {
            HWND root = GetAncestor(hWnd, GA_ROOTOWNER);
            if (OnStage(root)) {
                // Moving between the stage windows.
                g_focused = root;
                break;
            }
            // Checked further when handled: a window being opened may not be
            // ready yet.
            if (root &&
                MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST) ==
                    g_monitor) {
                PostMessageW(g_msgWnd, WM_APP_ADOPT, (WPARAM)root, 0);
            }
            break;
        }

        case EVENT_SYSTEM_MOVESIZESTART:
            if (g_stage.size() == 2 && OnStage(hWnd)) {
                g_resizing = hWnd;
            }
            break;

        case EVENT_SYSTEM_MOVESIZEEND:
            if (hWnd == g_resizing) {
                g_resizing = nullptr;
                // Snap both back into place.
                LayoutStage();
            }
            break;

        case EVENT_OBJECT_LOCATIONCHANGE:
            if (hWnd == g_resizing) {
                FollowPairResize(hWnd);
            } else if (OnStage(hWnd)) {
                SetTimer(g_msgWnd, kZoomCheckTimer, 120, nullptr);
            }
            break;

        case EVENT_SYSTEM_MINIMIZESTART:
            // Minimized by the user: it leaves the stage for the column.
            if (OnStage(hWnd)) {
                g_stage.erase(
                    std::remove(g_stage.begin(), g_stage.end(), hWnd),
                    g_stage.end());
                if (g_focused == hWnd) {
                    g_focused = g_stage.empty() ? nullptr : g_stage.back();
                }
                g_strip.insert(g_strip.begin(), hWnd);
                SetTimer(g_msgWnd, kRefreshTimer, 150, nullptr);
            }
            break;

        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
            if (OnStage(hWnd) ||
                std::find(g_strip.begin(), g_strip.end(), hWnd) !=
                    g_strip.end()) {
                SetTimer(g_msgWnd, kRefreshTimer, 150, nullptr);
            }
            break;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Four-finger touchpad gesture, read through Raw Input.
//
// hidpi.h is not used: the few HidP_* functions needed are resolved from
// hid.dll at runtime, with the structures they fill declared below.

namespace touchpad {

constexpr USHORT kPageGenericDesktop = 0x01;
constexpr USHORT kUsageX = 0x30;
constexpr USHORT kUsageY = 0x31;
constexpr USHORT kPageDigitizer = 0x0D;
constexpr USHORT kUsageTouchPad = 0x05;
constexpr USHORT kUsageTipSwitch = 0x42;
constexpr USHORT kUsageContactCount = 0x54;

constexpr LONG kHidpStatusSuccess = 0x00110000;
constexpr int kHidpInput = 0;

// Same layout as HIDP_CAPS.
struct HidpCaps {
    USHORT Usage;
    USHORT UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps;
    USHORT NumberInputValueCaps;
    USHORT NumberInputDataIndices;
    USHORT NumberOutputButtonCaps;
    USHORT NumberOutputValueCaps;
    USHORT NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps;
    USHORT NumberFeatureValueCaps;
    USHORT NumberFeatureDataIndices;
};
static_assert(sizeof(HidpCaps) == 64);

// Same layout as HIDP_VALUE_CAPS.
struct HidpValueCaps {
    USHORT UsagePage;
    UCHAR ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USHORT LinkUsage;
    USHORT LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    BOOLEAN HasNull;
    UCHAR Reserved;
    USHORT BitSize;
    USHORT ReportCount;
    USHORT Reserved2[5];
    ULONG UnitsExp;
    ULONG Units;
    LONG LogicalMin;
    LONG LogicalMax;
    LONG PhysicalMin;
    LONG PhysicalMax;
    union {
        struct {
            USHORT UsageMin;
            USHORT UsageMax;
            USHORT StringMin;
            USHORT StringMax;
            USHORT DesignatorMin;
            USHORT DesignatorMax;
            USHORT DataIndexMin;
            USHORT DataIndexMax;
        } Range;
        struct {
            USHORT Usage;
            USHORT Reserved1;
            USHORT StringIndex;
            USHORT Reserved2;
            USHORT DesignatorIndex;
            USHORT Reserved3;
            USHORT DataIndex;
            USHORT Reserved4;
        } NotRange;
    };
};
static_assert(sizeof(HidpValueCaps) == 72);

using HidP_GetCaps_t = LONG(WINAPI*)(PVOID preparsedData, HidpCaps* caps);
using HidP_GetValueCaps_t = LONG(WINAPI*)(int reportType,
                                          HidpValueCaps* valueCaps,
                                          PUSHORT valueCapsLength,
                                          PVOID preparsedData);
using HidP_GetUsageValue_t = LONG(WINAPI*)(int reportType,
                                           USHORT usagePage,
                                           USHORT linkCollection,
                                           USHORT usage,
                                           PULONG usageValue,
                                           PVOID preparsedData,
                                           PCHAR report,
                                           ULONG reportLength);
using HidP_GetUsages_t = LONG(WINAPI*)(int reportType,
                                       USHORT usagePage,
                                       USHORT linkCollection,
                                       PUSHORT usageList,
                                       PULONG usageLength,
                                       PVOID preparsedData,
                                       PCHAR report,
                                       ULONG reportLength);

HidP_GetCaps_t pHidP_GetCaps;
HidP_GetValueCaps_t pHidP_GetValueCaps;
HidP_GetUsageValue_t pHidP_GetUsageValue;
HidP_GetUsages_t pHidP_GetUsages;

bool LoadHidFunctions() {
    HMODULE hid = LoadLibraryW(L"hid.dll");
    if (!hid) {
        return false;
    }

    pHidP_GetCaps = (HidP_GetCaps_t)GetProcAddress(hid, "HidP_GetCaps");
    pHidP_GetValueCaps =
        (HidP_GetValueCaps_t)GetProcAddress(hid, "HidP_GetValueCaps");
    pHidP_GetUsageValue =
        (HidP_GetUsageValue_t)GetProcAddress(hid, "HidP_GetUsageValue");
    pHidP_GetUsages = (HidP_GetUsages_t)GetProcAddress(hid, "HidP_GetUsages");

    return pHidP_GetCaps && pHidP_GetValueCaps && pHidP_GetUsageValue &&
           pHidP_GetUsages;
}

// A precision touchpad reports each finger in its own link collection holding
// X, Y and a tip switch. Several reports may be needed to carry one frame
// ("hybrid mode"); the first report of a frame holds the total contact count
// and the following ones hold zero.
struct Device {
    bool valid = false;
    std::vector<BYTE> preparsed;
    std::vector<USHORT> contactCollections;
    bool hasContactCount = false;
    USHORT contactCountCollection = 0;
    LONG xMin = 0;
    LONG xMax = 0;

    ULONG frameExpected = 0;
    ULONG frameProcessed = 0;
    int frameTips = 0;
    double frameSumX = 0;
    double frameSumY = 0;
};

struct Gesture {
    bool tracking = false;
    bool fired = false;
    double startX = 0;
    double startY = 0;
    ULONGLONG lastReportTick = 0;
};

std::unordered_map<HANDLE, Device> g_devices;
Gesture g_gesture;
bool g_registered;

bool LoadDevice(HANDLE hDevice, Device* device) {
    UINT size = 0;
    if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, nullptr, &size) !=
            0 ||
        size == 0) {
        return false;
    }

    device->preparsed.resize(size);
    if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA,
                               device->preparsed.data(), &size) == (UINT)-1) {
        return false;
    }

    PVOID preparsed = device->preparsed.data();

    HidpCaps caps{};
    if (pHidP_GetCaps(preparsed, &caps) != kHidpStatusSuccess ||
        caps.UsagePage != kPageDigitizer || caps.Usage != kUsageTouchPad) {
        return false;
    }

    USHORT count = caps.NumberInputValueCaps;
    std::vector<HidpValueCaps> valueCaps(count);
    if (count == 0 ||
        pHidP_GetValueCaps(kHidpInput, valueCaps.data(), &count, preparsed) !=
            kHidpStatusSuccess) {
        return false;
    }

    for (USHORT i = 0; i < count; i++) {
        const HidpValueCaps& vc = valueCaps[i];
        auto covers = [&vc](USHORT usage) {
            return vc.IsRange ? (vc.Range.UsageMin <= usage &&
                                 usage <= vc.Range.UsageMax)
                              : vc.NotRange.Usage == usage;
        };

        if (vc.UsagePage == kPageGenericDesktop && covers(kUsageX)) {
            if (std::find(device->contactCollections.begin(),
                          device->contactCollections.end(),
                          vc.LinkCollection) ==
                device->contactCollections.end()) {
                device->contactCollections.push_back(vc.LinkCollection);
            }
            device->xMin = vc.LogicalMin;
            device->xMax = vc.LogicalMax;
        } else if (vc.UsagePage == kPageDigitizer &&
                   covers(kUsageContactCount)) {
            device->hasContactCount = true;
            device->contactCountCollection = vc.LinkCollection;
        }
    }

    Wh_Log(L"Touchpad found: %u contact slots per report",
           (unsigned)device->contactCollections.size());

    return !device->contactCollections.empty() &&
           device->xMax > device->xMin;
}

// A four-finger sideways swipe, left or right, toggles Stage Manager. Only an
// exact four-finger contact counts; anything else ends the gesture, and a
// swipe fires at most once until the fingers are lifted.
void OnFrame(const Device& device, int tips, double x, double y) {
    if (tips != 4) {
        if (tips == 0) {
            g_gesture.fired = false;
        }
        g_gesture.tracking = false;
        return;
    }

    if (!g_gesture.tracking) {
        g_gesture.tracking = true;
        g_gesture.startX = x;
        g_gesture.startY = y;
        return;
    }

    if (g_gesture.fired) {
        return;
    }

    double threshold =
        (device.xMax - device.xMin) * g_settings.gestureDistance / 100.0;
    double dx = x - g_gesture.startX;
    double dy = y - g_gesture.startY;

    if (fabs(dx) >= threshold && fabs(dx) > fabs(dy) * 1.5) {
        g_gesture.fired = true;
        Wh_Log(L"Four-finger swipe %s", dx < 0 ? L"left" : L"right");
        Toggle();
    }
}

void OnReport(Device& device, PCHAR report, ULONG reportLength) {
    PVOID preparsed = device.preparsed.data();

    auto startFrame = [&device](ULONG expected) {
        device.frameExpected = expected;
        device.frameProcessed = 0;
        device.frameTips = 0;
        device.frameSumX = 0;
        device.frameSumY = 0;
    };

    if (device.hasContactCount) {
        ULONG contactCount = 0;
        if (pHidP_GetUsageValue(kHidpInput, kPageDigitizer,
                                device.contactCountCollection,
                                kUsageContactCount, &contactCount, preparsed,
                                report, reportLength) == kHidpStatusSuccess &&
            contactCount > 0) {
            startFrame(contactCount);
        }
    } else {
        startFrame((ULONG)device.contactCollections.size());
    }

    if (device.frameExpected == 0) {
        return;
    }

    for (USHORT linkCollection : device.contactCollections) {
        if (device.frameProcessed >= device.frameExpected) {
            break;
        }
        device.frameProcessed++;

        USHORT usages[32];
        ULONG usageCount = ARRAYSIZE(usages);
        bool tip = false;
        if (pHidP_GetUsages(kHidpInput, kPageDigitizer, linkCollection, usages,
                            &usageCount, preparsed, report,
                            reportLength) == kHidpStatusSuccess) {
            for (ULONG i = 0; i < usageCount; i++) {
                tip = tip || usages[i] == kUsageTipSwitch;
            }
        }
        if (!tip) {
            continue;
        }

        ULONG x = 0;
        ULONG y = 0;
        pHidP_GetUsageValue(kHidpInput, kPageGenericDesktop, linkCollection,
                            kUsageX, &x, preparsed, report, reportLength);
        pHidP_GetUsageValue(kHidpInput, kPageGenericDesktop, linkCollection,
                            kUsageY, &y, preparsed, report, reportLength);

        device.frameTips++;
        device.frameSumX += x;
        device.frameSumY += y;
    }

    if (device.frameProcessed >= device.frameExpected) {
        int tips = device.frameTips;
        OnFrame(device, tips, tips ? device.frameSumX / tips : 0,
                tips ? device.frameSumY / tips : 0);
        device.frameExpected = 0;
    }
}

void OnRawInput(HRAWINPUT hRawInput) {
    UINT size = 0;
    if (GetRawInputData(hRawInput, RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0) {
        return;
    }

    static std::vector<BYTE> buffer;
    buffer.resize(size);
    if (GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size,
                        sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        return;
    }

    const RAWINPUT* raw = (const RAWINPUT*)buffer.data();
    if (raw->header.dwType != RIM_TYPEHID) {
        return;
    }

    // A pause in the stream means the fingers were lifted without a final
    // report reaching us; don't continue a stale gesture.
    ULONGLONG now = GetTickCount64();
    if (now - g_gesture.lastReportTick > 300) {
        g_gesture.tracking = false;
        g_gesture.fired = false;
    }
    g_gesture.lastReportTick = now;

    auto it = g_devices.find(raw->header.hDevice);
    if (it == g_devices.end()) {
        Device device;
        device.valid = LoadDevice(raw->header.hDevice, &device);
        it = g_devices.emplace(raw->header.hDevice, std::move(device)).first;
    }

    Device& device = it->second;
    if (!device.valid) {
        return;
    }

    const RAWHID& hid = raw->data.hid;
    for (DWORD i = 0; i < hid.dwCount; i++) {
        OnReport(device, (PCHAR)hid.bRawData + i * hid.dwSizeHid,
                 hid.dwSizeHid);
    }
}

void UpdateRegistration(HWND hWnd) {
    bool want = g_settings.gesture != GestureMode::Off;
    if (want == g_registered) {
        return;
    }

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = kPageDigitizer;
    rid.usUsage = kUsageTouchPad;
    rid.dwFlags = want ? RIDEV_INPUTSINK : RIDEV_REMOVE;
    rid.hwndTarget = want ? hWnd : nullptr;
    if (RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        g_registered = want;
    } else {
        Wh_Log(L"RegisterRawInputDevices failed (%u)", GetLastError());
    }
}

}  // namespace touchpad

////////////////////////////////////////////////////////////////////////////////
// Message window and thread

constexpr WCHAR kMsgClass[] = L"Windhawk_StageManager_Msg";

HANDLE g_thread;
HANDLE g_readyEvent;

// Shortcuts made only of modifiers, such as Ctrl+Win, can't be registered with
// RegisterHotKey, so they're watched with a low-level keyboard hook: the
// shortcut fires when the modifiers are released, provided that exactly those
// modifiers were held and no other key was pressed meanwhile. That keeps
// Ctrl+Win+Left and the like working.
HHOOK g_keyboardHook;
UINT g_heldMods;
bool g_chordArmed;
bool g_chordSpoiled;
// Left and right variants tracked separately, so that releasing one while the
// other is still down doesn't count as releasing the modifier.
std::unordered_map<DWORD, bool> g_heldModifierKeys;

UINT ModifierOf(DWORD vk) {
    switch (vk) {
        case VK_LWIN:
        case VK_RWIN:
            return MOD_WIN;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return MOD_CONTROL;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return MOD_ALT;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return MOD_SHIFT;
    }
    return 0;
}

UINT CurrentHeldMods() {
    UINT mods = 0;
    for (const auto& [vk, held] : g_heldModifierKeys) {
        if (held) {
            mods |= ModifierOf(vk);
        }
    }
    return mods;
}

// A key nobody uses, pressed while Win is held so that releasing Win doesn't
// open the Start menu (and releasing Alt doesn't focus a menu bar).
void SendMaskKey() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0xE8;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
        bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        if (!(kb->flags & LLKHF_INJECTED)) {
            UINT mod = ModifierOf(kb->vkCode);

            if (mod && down) {
                g_heldModifierKeys[kb->vkCode] = true;
                g_heldMods = CurrentHeldMods();
                if (g_heldMods & ~g_settings.hotkeyMods) {
                    // An extra modifier: some other shortcut.
                    g_chordArmed = false;
                    g_chordSpoiled = true;
                } else if (g_heldMods == g_settings.hotkeyMods &&
                           !g_chordSpoiled && !g_chordArmed) {
                    g_chordArmed = true;
                    SendMaskKey();
                }
            } else if (mod && up) {
                g_heldModifierKeys[kb->vkCode] = false;
                g_heldMods = CurrentHeldMods();
                // Fire on the first modifier released, once per press.
                if (g_chordArmed && !g_chordSpoiled &&
                    (g_heldMods & g_settings.hotkeyMods) !=
                        g_settings.hotkeyMods) {
                    PostMessageW(g_msgWnd, WM_APP_TOGGLE, 0, 0);
                    g_chordArmed = false;
                    g_chordSpoiled = true;
                }
                if (!g_heldMods) {
                    g_chordArmed = false;
                    g_chordSpoiled = false;
                }
            } else if (down && g_heldMods) {
                // Another key with the modifiers: some other shortcut.
                g_chordArmed = false;
                g_chordSpoiled = true;
            }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void RemoveKeyboardHook() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_heldModifierKeys.clear();
    g_heldMods = 0;
    g_chordArmed = false;
    g_chordSpoiled = false;
}

void RegisterHotkey(HWND hWnd) {
    UnregisterHotKey(hWnd, kHotkeyId);
    RemoveKeyboardHook();

    if (!g_settings.hotkeyVk) {
        if (g_settings.hotkeyMods) {
            g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc,
                                               GetModuleHandleW(nullptr), 0);
            if (!g_keyboardHook) {
                Wh_Log(L"SetWindowsHookEx failed (%u)", GetLastError());
            }
        }
        return;
    }
    if (!RegisterHotKey(hWnd, kHotkeyId, g_settings.hotkeyMods | MOD_NOREPEAT,
                        g_settings.hotkeyVk)) {
        Wh_Log(L"RegisterHotKey failed (%u), the hotkey may be in use",
               GetLastError());
    }
}

LRESULT CALLBACK MsgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            if (wParam == kHotkeyId) {
                CheckDesktopSwitch();
                Toggle();
            }
            return 0;

        case WM_INPUT:
            if (g_settings.gesture != GestureMode::Off &&
                touchpad::pHidP_GetCaps) {
                touchpad::OnRawInput((HRAWINPUT)lParam);
            }
            // DefWindowProc must still see WM_INPUT so the system can clean up.
            break;

        case WM_APP_TOGGLE:
            CheckDesktopSwitch();
            Toggle();
            return 0;

        case WM_APP_SWITCH:
            CheckDesktopSwitch();
            SwitchTo((HWND)wParam, true, lParam != 0);
            return 0;

        case WM_APP_ADOPT: {
            // Activated in some other way: put it on stage, unless something
            // else changed in the meantime.
            // Switching desktops activates a window on the new one, possibly
            // before the switch is recorded: such a window must not be taken.
            CheckDesktopSwitch();
            HWND target = (HWND)wParam;
            if (lParam == 0) {
                g_adoptTries = 0;
            }
            if (!g_active || OnStage(target) ||
                GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != target) {
                return 0;
            }

            // A window being opened may not have its title yet: look again
            // a little later.
            if (!IsCandidate(target) || !IsOnStageDesktop(target)) {
                if (g_adoptTries++ < 8) {
                    g_pendingAdopt = target;
                    SetTimer(hWnd, kAdoptRetryTimer, 250, nullptr);
                }
                return 0;
            }

            bool known = g_saved.count(target) ||
                         std::find(g_strip.begin(), g_strip.end(), target) !=
                             g_strip.end();
            if (!known) {
                g_freshUntil[target] = GetTickCount64() + 2500;
            }

            SwitchTo(target, false);

            // An app may maximize its new window only after showing it.
            if (!known) {
                SetTimer(hWnd, kZoomCheckTimer, 400, nullptr);
            }
            return 0;
        }

        case WM_APP_REMOVE:
            CheckDesktopSwitch();
            RemoveFromStage((HWND)wParam);
            return 0;

        case WM_APP_GENIE_CARD:
            genie::CardReached((HWND)wParam);
            return 0;

        case WM_APP_MOUSEHOOK:
            UpdateMouseHook();
            return 0;

        case WM_APP_SETTINGS:
            ExitAll();
            LoadSettings();
            RegisterHotkey(hWnd);
            touchpad::UpdateRegistration(hWnd);
            return 0;

        case WM_TIMER:
            if (wParam == kRefreshTimer) {
                KillTimer(hWnd, kRefreshTimer);
                CheckDesktopSwitch();
                Refresh();
            } else if (wParam == kZoomCheckTimer) {
                KillTimer(hWnd, kZoomCheckTimer);
                CheckStageZoom();
            } else if (wParam == kAdoptRetryTimer) {
                KillTimer(hWnd, kAdoptRetryTimer);
                if (g_pendingAdopt) {
                    PostMessageW(hWnd, WM_APP_ADOPT, (WPARAM)g_pendingAdopt, 1);
                    g_pendingAdopt = nullptr;
                }
            }
            return 0;

        case WM_DESTROY:
            ExitAll();
            UnregisterHotKey(hWnd, kHotkeyId);
            RemoveKeyboardHook();
            g_settings.gesture = GestureMode::Off;
            touchpad::UpdateRegistration(hWnd);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

DWORD WINAPI ThreadProc(LPVOID) {
    // Window and monitor coordinates in physical pixels, matching DWM.
    using SetThreadDpiAwarenessContext_t =
        DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto pSetThreadDpiAwarenessContext =
            (SetThreadDpiAwarenessContext_t)GetProcAddress(
                GetModuleHandleW(L"user32.dll"),
                "SetThreadDpiAwarenessContext")) {
        pSetThreadDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-4);
    }

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(kClsidVirtualDesktopManager, nullptr,
                                CLSCTX_ALL, kIidVirtualDesktopManager,
                                (void**)&g_desktopManager))) {
        Wh_Log(L"No virtual desktop manager");
        g_desktopManager = nullptr;
    }
    OpenDesktopKey();
    g_currentDesktop = ReadCurrentDesktop();
    g_desktopChangedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WatchDesktopKey();

    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMsgClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW cardClass{sizeof(cardClass)};
    cardClass.lpfnWndProc = CardWndProc;
    cardClass.hInstance = instance;
    cardClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    cardClass.lpszClassName = kCardClass;
    RegisterClassExW(&cardClass);

    WNDCLASSEXW dividerClass{sizeof(dividerClass)};
    dividerClass.style = CS_DBLCLKS;
    dividerClass.lpfnWndProc = DividerWndProc;
    dividerClass.hInstance = instance;
    dividerClass.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    dividerClass.lpszClassName = kDividerClass;
    RegisterClassExW(&dividerClass);

    WNDCLASSEXW iconClass{sizeof(iconClass)};
    iconClass.lpfnWndProc = IconWndProc;
    iconClass.hInstance = instance;
    iconClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    iconClass.lpszClassName = kIconClass;
    RegisterClassExW(&iconClass);

    WNDCLASSEXW ghostClass{sizeof(ghostClass)};
    ghostClass.lpfnWndProc = DefWindowProcW;
    ghostClass.hInstance = instance;
    ghostClass.lpszClassName = genie::kGhostClass;
    RegisterClassExW(&ghostClass);

    g_markerWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  genie::kGhostClass, L"", WS_POPUP, 0, 0, 0, 0,
                                  nullptr, nullptr, instance, nullptr);
    genie::Start();

    if (!touchpad::LoadHidFunctions()) {
        Wh_Log(L"Couldn't load hid.dll functions, gesture unavailable");
        touchpad::pHidP_GetCaps = nullptr;
    }

    g_msgWnd = CreateWindowExW(0, kMsgClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                               nullptr, instance, nullptr);
    if (g_msgWnd) {
        RegisterHotkey(g_msgWnd);
        if (touchpad::pHidP_GetCaps) {
            touchpad::UpdateRegistration(g_msgWnd);
        }
    }

    SetEvent(g_readyEvent);

    // Messages, plus the registry notification of a desktop switch.
    bool running = g_msgWnd != nullptr;
    while (running) {
        DWORD handleCount = g_desktopChangedEvent ? 1 : 0;
        DWORD result = MsgWaitForMultipleObjectsEx(
            handleCount, &g_desktopChangedEvent, INFINITE, QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);

        if (handleCount && result == WAIT_OBJECT_0) {
            WatchDesktopKey();
            CheckDesktopSwitch();
            continue;
        }

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    genie::CancelAll();
    genie::Stop();
    if (g_markerWnd) {
        DestroyWindow(g_markerWnd);
        g_markerWnd = nullptr;
    }
    g_msgWnd = nullptr;

    if (g_desktopKey) {
        RegCloseKey(g_desktopKey);
        g_desktopKey = nullptr;
    }
    if (g_desktopChangedEvent) {
        CloseHandle(g_desktopChangedEvent);
        g_desktopChangedEvent = nullptr;
    }
    if (g_desktopManager) {
        g_desktopManager->Release();
        g_desktopManager = nullptr;
    }
    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    UnregisterClassW(genie::kGhostClass, instance);
    UnregisterClassW(kDividerClass, instance);
    UnregisterClassW(kIconClass, instance);
    UnregisterClassW(kCardClass, instance);
    UnregisterClassW(kMsgClass, instance);
    return 0;
}

}  // namespace

BOOL WhTool_ModInit() {
    LoadSettings();

    g_readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_readyEvent) {
        return FALSE;
    }

    g_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (!g_thread) {
        return FALSE;
    }

    WaitForSingleObject(g_readyEvent, 5000);
    return TRUE;
}

void WhTool_ModSettingsChanged() {
    if (g_msgWnd) {
        PostMessageW(g_msgWnd, WM_APP_SETTINGS, 0, 0);
    }
}

void WhTool_ModUninit() {
    if (g_msgWnd) {
        PostMessageW(g_msgWnd, WM_CLOSE, 0, 0);
    }

    if (g_thread) {
        WaitForSingleObject(g_thread, 10000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }

    if (g_readyEvent) {
        CloseHandle(g_readyEvent);
        g_readyEvent = nullptr;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Windhawk tool mod implementation for mods which don't need to inject to other
// processes or hook other functions. Context:
// https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process
//
// The mod will load and run in a dedicated windhawk.exe process.

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) &&
        sessionId == 0) {
        return FALSE;
    }

    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }

    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 ||
            wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
            break;
        }
    }

    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
                isCurrentToolModProcess = true;
            }
            break;
        }
    }

    LocalFree(argv);

    if (isExcluded) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex =
            CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running (%s)", WH_MOD_ID);
            ExitProcess(1);
        }

        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }

        IMAGE_DOS_HEADER* dosHeader =
            (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders =
            (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);

        DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* entryPoint = (BYTE*)dosHeader + entryPointRVA;

        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) {
        return FALSE;
    }

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }

    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath,
                              ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR
    commandLine[MAX_PATH + 2 +
                (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath,
               WH_MOD_ID);

    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            Wh_Log(L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t = BOOL(WINAPI*)(
        HANDLE hUserToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
        LPSECURITY_ATTRIBUTES lpProcessAttributes,
        LPSECURITY_ATTRIBUTES lpThreadAttributes, WINBOOL bInheritHandles,
        DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
        LPSTARTUPINFOW lpStartupInfo,
        LPPROCESS_INFORMATION lpProcessInformation,
        PHANDLE hRestrictedUserToken);
    CreateProcessInternalW_t pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule,
                                                 "CreateProcessInternalW");
    if (!pCreateProcessInternalW) {
        Wh_Log(L"No CreateProcessInternalW");
        return;
    }

    STARTUPINFO si{
        .cb = sizeof(STARTUPINFO),
        .dwFlags = STARTF_FORCEOFFFEEDBACK,
    };
    PROCESS_INFORMATION pi;
    if (!pCreateProcessInternalW(nullptr, currentProcessPath, commandLine,
                                 nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS,
                                 nullptr, nullptr, &si, &pi, nullptr)) {
        Wh_Log(L"CreateProcess failed");
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModUninit();
    ExitProcess(0);
}
