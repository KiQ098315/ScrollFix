#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")
#endif

struct ScrollFixDpiBootstrap {
    ScrollFixDpiBootstrap() {
        using SetCtxFn = BOOL(WINAPI*)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return;
        auto setCtx = reinterpret_cast<SetCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx) {
            setCtx(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4))); 
            return;
        }
        using SetAwareFn = BOOL(WINAPI*)();
        auto setAware = reinterpret_cast<SetAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
        if (setAware) setAware();
    }
};
static ScrollFixDpiBootstrap g_dpiBootstrap;

namespace {

void WFormat(wchar_t* dst, size_t cch, const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
#if defined(_MSC_VER)
    _vsnwprintf_s(dst, cch, _TRUNCATE, fmt, args);
#else
    vswprintf(dst, cch, fmt, args);
#endif
    va_end(args);
}

constexpr wchar_t kAppName[] = L"ScrollFix";
constexpr wchar_t kAppVersion[] = L"1.2.0";
constexpr wchar_t kWndClass[] = L"ScrollFixCoreWnd";
constexpr wchar_t kUiClass[] = L"ScrollFixSettingsUI";
constexpr wchar_t kDiagClass[] = L"ScrollFixDiagUI";
constexpr wchar_t kMutexName[] = L"Local\\ScrollFixSingleInstance";
constexpr wchar_t kRegPath[] = L"Software\\ScrollFix";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_UI_STATS = WM_APP + 2;
constexpr UINT WM_DIAG_EVENT = WM_APP + 3;
constexpr UINT WM_EMIT_WHEEL = WM_APP + 4;

constexpr UINT ID_TRAY = 1;
constexpr UINT ID_TOGGLE = 1001;
constexpr UINT ID_SETTINGS = 1002;
constexpr UINT ID_DIAG = 1003;
constexpr UINT ID_EXIT = 1004;
constexpr UINT ID_LANG_RU = 1005;
constexpr UINT ID_LANG_EN = 1006;
constexpr UINT ID_VERSION = 1007;

enum Lang : int { LangRu = 0, LangEn = 1 };

struct UiText {
    const wchar_t* alreadyRunning;
    const wchar_t* hookFail;
    const wchar_t* subtitle;
    const wchar_t* filter;
    const wchar_t* autostart;
    const wchar_t* clickFix;
    const wchar_t* sensitivity;
    const wchar_t* sensAuto;
    const wchar_t* sensSoft;
    const wchar_t* sensMed;
    const wchar_t* sensAgg;
    const wchar_t* save;
    const wchar_t* diagnostics;
    const wchar_t* language;
    const wchar_t* trayOff;
    const wchar_t* trayOn;
    const wchar_t* traySettings;
    const wchar_t* trayDiag;
    const wchar_t* trayExit;
    const wchar_t* trayLang;
    const wchar_t* diagTitle;
    const wchar_t* diagHint;
};

constexpr UiText kRu{
    L"ScrollFix уже запущен.",
    L"Не удалось установить хук мыши.",
    L"Фильтр ложных скроллов и даблкликов",
    L"Фильтр скролла",
    L"Автозапуск",
    L"Фикс даблклика",
    L"Чувствительность",
    L"Авто",
    L"Мягкая",
    L"Средняя",
    L"Жёсткая",
    L"Сохранить",
    L"Диагностика",
    L"Язык",
    L"Выключить фильтр",
    L"Включить фильтр",
    L"Настройки",
    L"Диагностика",
    L"Выход",
    L"Язык",
    L"Диагностика",
    L"зелёный=ok  красный=удалён  жёлтый=hold  бирюзовый=реверс",
};

constexpr UiText kEn{
    L"ScrollFix is already running.",
    L"Failed to install mouse hook.",
    L"Filters false scrolls and double-clicks",
    L"Scroll filter",
    L"Start with Windows",
    L"Double-click fix",
    L"Sensitivity",
    L"Auto",
    L"Soft",
    L"Medium",
    L"Strict",
    L"Save",
    L"Diagnostics",
    L"Language",
    L"Disable filter",
    L"Enable filter",
    L"Settings",
    L"Diagnostics",
    L"Exit",
    L"Language",
    L"Diagnostics",
    L"green=ok  red=removed  yellow=hold  teal=reverse",
};

constexpr UINT TIMER_UI = 61;
constexpr UINT TIMER_DIAG = 62;

constexpr COLORREF C_BG = RGB(10, 12, 16);
constexpr COLORREF C_PANEL = RGB(18, 22, 30);
constexpr COLORREF C_PANEL2 = RGB(24, 30, 40);
constexpr COLORREF C_TEXT = RGB(232, 238, 247);
constexpr COLORREF C_MUTED = RGB(176, 186, 202);
constexpr COLORREF C_ACCENT = RGB(46, 230, 166);
constexpr COLORREF C_ACCENT_DIM = RGB(24, 120, 90);
constexpr COLORREF C_DANGER = RGB(255, 92, 110);
constexpr COLORREF C_WARN = RGB(255, 184, 77);
constexpr COLORREF C_SUP = RGB(255, 110, 120);
constexpr COLORREF C_OK = RGB(80, 220, 160);

enum SensMode : int { SensAuto = 0, SensSoft = 1, SensMedium = 2, SensAggressive = 3 };

struct Settings {
    bool enabled = true;
    bool autostart = false;
    bool clickFix = true;
    bool logging = false;
    int sens = SensAuto;
    int lang = LangRu;
    int windowMs = 20;
    int minStreak = 2;
    int reverseConfirm = 4;
};

Settings g_settings;
const UiText& T() { return (g_settings.lang == LangEn) ? kEn : kRu; }
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_clickFix{true};
std::atomic<int> g_sens{SensAuto};
std::atomic<int> g_windowMs{20};
std::atomic<int> g_minStreak{2};
std::atomic<int> g_reverseConfirm{4};
std::atomic<bool> g_logging{false};

HINSTANCE g_inst = nullptr;
HWND g_hwnd = nullptr;
HWND g_settingsWnd = nullptr;
HWND g_diagWnd = nullptr;
HHOOK g_hook = nullptr;
HANDLE g_mutex = nullptr;
NOTIFYICONDATAW g_nid{};
bool g_trayAdded = false;
ULONG_PTR g_gdiplusToken = 0;

HFONT g_fontTitle = nullptr;
HFONT g_fontBody = nullptr;
HFONT g_fontSmall = nullptr;
HFONT g_fontMono = nullptr;
HBRUSH g_brBg = nullptr;
int g_dpi = 96;
int g_fontsDpi = 0;

int SX(int px) { return MulDiv(px, g_dpi, 96); }

void EnableDpiAwareness() {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    auto setCtx = reinterpret_cast<SetCtxFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setCtx) {
        
        setCtx(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
        return;
    }
    using SetAwareFn = BOOL(WINAPI*)();
    auto setAware = reinterpret_cast<SetAwareFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"));
    if (setAware) setAware();
}

void RefreshDpi(HWND hwnd) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    auto getDpi = reinterpret_cast<GetDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpi && hwnd) {
        const UINT d = getDpi(hwnd);
        if (d >= 96) g_dpi = static_cast<int>(d);
        return;
    }
    HDC hdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    if (g_dpi < 96) g_dpi = 96;
}

LARGE_INTEGER g_qpf{};

inline DWORD NowMs() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<DWORD>((c.QuadPart * 1000ULL) / static_cast<unsigned long long>(g_qpf.QuadPart));
}

inline int Sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
inline int Abs(int v) { return v < 0 ? -v : v; }
inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

DWORD g_avgInterval = 70;     
DWORD g_maxInterval = 120;
unsigned g_falseBurstLens = 1; 
unsigned g_adaptSamples = 0;

int g_dir = 0;
int g_streak = 0;
DWORD g_lastAcceptMs = 0;

int g_holdDir = 0;
int g_holdSum = 0;
int g_holdCount = 0;
DWORD g_holdStartMs = 0;
DWORD g_holdSinceAccept = 0;
DWORD g_stickyUntil = 0;
bool g_injecting = false;
constexpr UINT TIMER_HOLD = 70;

struct BtnFixState {
    DWORD lastDownMs = 0;
    DWORD lastUpMs = 0;
    bool suppressUp = false;
};
BtnFixState g_lBtn{};
BtnFixState g_rBtn{};

std::atomic<unsigned long long> g_raw{0};
std::atomic<unsigned long long> g_passed{0};
std::atomic<unsigned long long> g_suppressed{0};
std::atomic<unsigned long long> g_reversed{0};
std::atomic<unsigned long long> g_clickSuppressed{0};
DWORD g_lastUiPostMs = 0;

enum DiagKind : int { DK_Pass = 1, DK_Suppress = 2, DK_Suspect = 3, DK_Reverse = 4 };
struct DiagItem {
    int dir;      
    int kind;
    DWORD ms;
};
constexpr int kDiagCap = 48;
DiagItem g_diag[kDiagCap];
int g_diagW = 0;
int g_diagN = 0;

FILE* g_logFile = nullptr;

bool GetExePath(wchar_t* out, DWORD cch) {
    const DWORD n = GetModuleFileNameW(nullptr, out, cch);
    return n > 0 && n < cch;
}

void LogLine(const wchar_t* line) {
    if (!g_logging.load(std::memory_order_relaxed) || !g_logFile) return;
    fwprintf(g_logFile, L"%ls\n", line);
}

void OpenLogIfNeeded() {
    if (!g_settings.logging) {
        if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
        return;
    }
    if (g_logFile) return;
    wchar_t path[MAX_PATH]{};
    if (!GetExePath(path, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    const wchar_t name[] = L"ScrollFix.log";
    const size_t remain = (size_t)(MAX_PATH - (slash + 1 - path));
    if (remain < sizeof(name) / sizeof(wchar_t)) return;
    for (size_t i = 0; i < sizeof(name) / sizeof(wchar_t); ++i) slash[1 + i] = name[i];
    g_logFile = _wfopen(path, L"a, ccs=UTF-8");
}

void CloseLog() {
    if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
}

void LoadSettings() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    auto rd = [&](const wchar_t* n, DWORD def) -> DWORD {
        DWORD t = 0, d = 0, sz = sizeof(d);
        if (RegQueryValueExW(key, n, nullptr, &t, reinterpret_cast<LPBYTE>(&d), &sz) == ERROR_SUCCESS && t == REG_DWORD)
            return d;
        return def;
    };
    g_settings.enabled = rd(L"Enabled", 1) != 0;
    g_settings.autostart = rd(L"Autostart", 0) != 0;
    g_settings.clickFix = rd(L"ClickFix", 1) != 0;
    g_settings.logging = rd(L"Logging", 0) != 0;
    g_settings.sens = Clamp((int)rd(L"Sens", SensAuto), 0, 3);
    g_settings.lang = Clamp((int)rd(L"Lang", LangRu), 0, 1);
    g_settings.windowMs = Clamp((int)rd(L"WindowMs", 20), 5, 30);
    g_settings.minStreak = Clamp((int)rd(L"MinStreak", 2), 2, 5);
    g_settings.reverseConfirm = Clamp((int)rd(L"ReverseConfirm", 4), 2, 6);
    RegCloseKey(key);
}

void SaveSettings() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    auto wr = [&](const wchar_t* n, DWORD v) {
        RegSetValueExW(key, n, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    };
    wr(L"Enabled", g_settings.enabled ? 1 : 0);
    wr(L"Autostart", g_settings.autostart ? 1 : 0);
    wr(L"ClickFix", g_settings.clickFix ? 1 : 0);
    wr(L"Logging", g_settings.logging ? 1 : 0);
    wr(L"Sens", (DWORD)g_settings.sens);
    wr(L"Lang", (DWORD)g_settings.lang);
    wr(L"WindowMs", (DWORD)g_settings.windowMs);
    wr(L"MinStreak", (DWORD)g_settings.minStreak);
    wr(L"ReverseConfirm", (DWORD)g_settings.reverseConfirm);
    RegCloseKey(key);
}

void ApplyRuntime() {
    g_enabled.store(g_settings.enabled, std::memory_order_relaxed);
    g_clickFix.store(g_settings.clickFix, std::memory_order_relaxed);
    g_sens.store(g_settings.sens, std::memory_order_relaxed);
    g_windowMs.store(g_settings.windowMs, std::memory_order_relaxed);
    g_minStreak.store(g_settings.minStreak, std::memory_order_relaxed);
    g_reverseConfirm.store(g_settings.reverseConfirm, std::memory_order_relaxed);
    g_logging.store(g_settings.logging, std::memory_order_relaxed);
    if (g_settings.logging) OpenLogIfNeeded();
    else CloseLog();
}

void SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t path[MAX_PATH]{};
        if (GetExePath(path, MAX_PATH)) {
            wchar_t quoted[MAX_PATH + 4]{};
            WFormat(quoted, MAX_PATH + 4, L"\"%ls\"", path);
            RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted),
                           (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD t = 0, sz = 0;
    const LONG rc = RegQueryValueExW(key, kAppName, nullptr, &t, nullptr, &sz);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && t == REG_SZ;
}

void MaybePostUi() {
    const DWORD now = NowMs();
    if (now - g_lastUiPostMs < 100) return;
    g_lastUiPostMs = now;
    if (g_settingsWnd && IsWindow(g_settingsWnd)) PostMessageW(g_settingsWnd, WM_UI_STATS, 0, 0);
    if (g_diagWnd && IsWindow(g_diagWnd)) PostMessageW(g_diagWnd, WM_UI_STATS, 0, 0);
}

void PushDiag(int dir, int kind) {
    if (!g_diagWnd || !IsWindow(g_diagWnd)) return;
    g_diag[g_diagW] = DiagItem{dir, kind, NowMs()};
    g_diagW = (g_diagW + 1) % kDiagCap;
    if (g_diagN < kDiagCap) ++g_diagN;
}

void BounceParams(int& bounceMs, DWORD& idleMs, DWORD& sessionMs) {
    const int sens = g_sens.load(std::memory_order_relaxed);
    if (sens == SensAuto) {
        bounceMs = Clamp((int)(g_avgInterval / 3 + 30), 34, 56);
        idleMs = Clamp(g_avgInterval * 8 + 260, 560, 980);
        sessionMs = Clamp(g_avgInterval * 5 + 200, 360, 720);
    } else if (sens == SensSoft) {
        bounceMs = 28;
        idleMs = 420;
        sessionMs = 280;
    } else if (sens == SensAggressive) {
        bounceMs = 52;
        idleMs = 820;
        sessionMs = 560;
    } else {
        bounceMs = Clamp(g_windowMs.load(std::memory_order_relaxed) + 18, 34, 56);
        idleMs = 640;
        sessionMs = 420;
    }
}

void ClickParams(DWORD& bounceMs, DWORD& minGapMs) {
    const int sens = g_sens.load(std::memory_order_relaxed);
    if (sens == SensSoft) {
        bounceMs = 35;
        minGapMs = 70;
    } else if (sens == SensAggressive) {
        bounceMs = 65;
        minGapMs = 120;
    } else if (sens == SensMedium) {
        bounceMs = 50;
        minGapMs = 95;
    } else {
        bounceMs = 45;
        minGapMs = 90;
    }
}

void ClearHold() {
    g_holdDir = 0;
    g_holdSum = 0;
    g_holdCount = 0;
    g_holdStartMs = 0;
    g_holdSinceAccept = 0;
    if (g_hwnd) KillTimer(g_hwnd, TIMER_HOLD);
}

void NoteSuppress(int n) {
    g_falseBurstLens = (g_falseBurstLens * 3 + (unsigned)Clamp(n, 1, 8)) / 4;
    if (g_falseBurstLens < 1) g_falseBurstLens = 1;
    g_suppressed.fetch_add((unsigned long long)Clamp(n, 1, 8), std::memory_order_relaxed);
    g_stickyUntil = NowMs() + 320;
}

void AcceptDir(int dir, DWORD now) {
    if (dir == g_dir && g_lastAcceptMs != 0) {
        const DWORD dt = now - g_lastAcceptMs;
        if (dt > 0 && dt < 400) {
            g_avgInterval = (g_avgInterval * 7 + dt) / 8;
            if (dt > g_maxInterval) g_maxInterval = (g_maxInterval * 3 + dt) / 4;
            else g_maxInterval = (g_maxInterval * 15 + dt) / 16;
            if (g_adaptSamples < 100000) ++g_adaptSamples;
        }
        if (g_streak < 100000) ++g_streak;
    } else {
        g_dir = dir;
        g_streak = 1;
    }
    g_lastAcceptMs = now;
}

void EmitWheelOnce(int delta) {
    if (delta == 0 || !g_hwnd) return;
    PostMessageW(g_hwnd, WM_EMIT_WHEEL, (WPARAM)(int)delta, 0);
}

void EmitWheelNow(int delta) {
    if (delta == 0) return;
    g_injecting = true;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)delta;
    SendInput(1, &in, sizeof(INPUT));
    g_injecting = false;
}

bool ConfirmRealReverse(DWORD now, DWORD idleMs, DWORD sessionMs) {
    if (g_holdCount <= 0) return false;
    const DWORD span = now - g_holdStartMs;
    const bool sticky = now < g_stickyUntil;
    const bool inSession = g_holdSinceAccept < sessionMs || g_streak >= 2;
    const bool afterIdle = g_holdSinceAccept >= idleMs;

    if (sticky || inSession) {
        if (g_holdCount >= 4 && span >= 55) return true;
        if (g_holdCount >= 5 && span >= 35) return true;
        if (g_holdCount >= 6) return true;
        return false;
    }

    if (afterIdle) {
        if (g_holdCount >= 2 && span >= 45) return true;
        if (g_holdCount >= 3 && span >= 28) return true;
        return false;
    }

    if (g_holdCount >= 3 && span >= 50) return true;
    if (g_holdCount >= 4 && span >= 35) return true;
    return false;
}

void ReleaseHoldAsReverse() {
    if (g_holdCount <= 0) { ClearHold(); return; }
    const int sum = g_holdSum;
    const int dir = Sign(sum);
    ClearHold();
    if (sum == 0 || dir == 0) return;
    AcceptDir(dir, NowMs());
    EmitWheelOnce(sum);
    PushDiag(dir, DK_Reverse);
    g_reversed.fetch_add(1, std::memory_order_relaxed);
    MaybePostUi();
}

void OnHoldTimeout() {
    if (g_holdCount <= 0) { ClearHold(); return; }
    int bounceMs = 34;
    DWORD idleMs = 640, sessionMs = 420;
    BounceParams(bounceMs, idleMs, sessionMs);
    (void)bounceMs;
    const DWORD now = NowMs();
    const DWORD span = now - g_holdStartMs;
    const bool sticky = now < g_stickyUntil;
    const bool inSession = g_holdSinceAccept < sessionMs || g_streak >= 2;

    if (g_holdCount <= 1) {
        NoteSuppress(g_holdCount);
        PushDiag(g_holdDir, DK_Suppress);
        ClearHold();
        MaybePostUi();
        return;
    }

    if (!sticky && !inSession && g_holdSinceAccept >= idleMs && g_holdCount >= 2 && span >= 40) {
        ReleaseHoldAsReverse();
        return;
    }
    if (!sticky && !inSession && g_holdCount >= 3 && span >= 45) {
        ReleaseHoldAsReverse();
        return;
    }
    NoteSuppress(g_holdCount);
    for (int i = 0; i < g_holdCount && i < 6; ++i) PushDiag(g_holdDir, DK_Suppress);
    ClearHold();
    MaybePostUi();
}

bool FilterButton(BtnFixState& st, bool isDown) {
    if (!g_clickFix.load(std::memory_order_relaxed)) return true;
    const DWORD now = NowMs();
    DWORD bounceMs = 45, minGapMs = 90;
    ClickParams(bounceMs, minGapMs);

    if (isDown) {
        if (st.lastUpMs != 0 && (now - st.lastUpMs) < bounceMs) {
            st.suppressUp = true;
            g_clickSuppressed.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (st.lastDownMs != 0 && (now - st.lastDownMs) < minGapMs) {
            st.suppressUp = true;
            g_clickSuppressed.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        st.lastDownMs = now;
        st.suppressUp = false;
        return true;
    }

    if (st.suppressUp) {
        st.suppressUp = false;
        return false;
    }
    st.lastUpMs = now;
    return true;
}

bool FilterWheel(int delta) {
    const int dir = Sign(delta);
    if (dir == 0) return true;

    const DWORD now = NowMs();
    g_raw.fetch_add(1, std::memory_order_relaxed);

    if (!g_enabled.load(std::memory_order_relaxed)) {
        ClearHold();
        AcceptDir(dir, now);
        PushDiag(dir, DK_Pass);
        g_passed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    int bounceMs = 34;
    DWORD idleMs = 640, sessionMs = 420;
    BounceParams(bounceMs, idleMs, sessionMs);
    const DWORD since = (g_lastAcceptMs == 0) ? 100000u : (now - g_lastAcceptMs);

    if (g_dir == 0) {
        AcceptDir(dir, now);
        PushDiag(dir, DK_Pass);
        g_passed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (dir == g_dir) {
        if (g_holdCount > 0) {
            NoteSuppress(g_holdCount);
            for (int i = 0; i < g_holdCount && i < 6; ++i) PushDiag(g_holdDir, DK_Suppress);
            ClearHold();
        }
        AcceptDir(dir, now);
        PushDiag(dir, DK_Pass);
        g_passed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (g_holdCount > 0 && dir != g_holdDir) {
        NoteSuppress(g_holdCount);
        ClearHold();
    }

    if (g_holdCount == 0) {
        g_holdDir = dir;
        g_holdSum = delta;
        g_holdCount = 1;
        g_holdStartMs = now;
        g_holdSinceAccept = since;
        PushDiag(dir, DK_Suspect);
        const UINT holdMs = (UINT)Clamp(bounceMs + 12, 36, 70);
        if (g_hwnd) SetTimer(g_hwnd, TIMER_HOLD, holdMs, nullptr);
        return false;
    }

    g_holdSum += delta;
    ++g_holdCount;
    PushDiag(dir, DK_Suspect);
    if (g_hwnd) SetTimer(g_hwnd, TIMER_HOLD, (UINT)Clamp(bounceMs + 12, 36, 70), nullptr);

    if (ConfirmRealReverse(now, idleMs, sessionMs)) {
        ReleaseHoldAsReverse();
        return false;
    }
    return false;
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (g_injecting || (info->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED))) {
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP) {
        if (!FilterButton(g_lBtn, wParam == WM_LBUTTONDOWN)) return 1;
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }
    if (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP) {
        if (!FilterButton(g_rBtn, wParam == WM_RBUTTONDOWN)) return 1;
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    if (wParam != WM_MOUSEWHEEL) {
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    const short delta = (short)HIWORD(info->mouseData);
    if (delta == 0) return CallNextHookEx(g_hook, code, wParam, lParam);

    if (!FilterWheel(delta)) {
        return 1;
    }
    MaybePostUi();
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

void DestroyFonts() {
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontBody) DeleteObject(g_fontBody);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    if (g_fontMono) DeleteObject(g_fontMono);
    if (g_brBg) DeleteObject(g_brBg);
    g_fontTitle = g_fontBody = g_fontSmall = g_fontMono = nullptr;
    g_brBg = nullptr;
    g_fontsDpi = 0;
}

void EnsureFonts() {
    if (g_fontTitle && g_fontsDpi == g_dpi) return;
    DestroyFonts();
    
    auto mk = [](int px, int weight, const wchar_t* face) -> HFONT {
        return CreateFontW(-SX(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_SWISS, face);
    };
    g_fontTitle = mk(20, FW_SEMIBOLD, L"Segoe UI Semibold");
    g_fontBody = mk(13, FW_SEMIBOLD, L"Segoe UI");
    g_fontSmall = mk(12, FW_MEDIUM, L"Segoe UI");
    g_fontMono = mk(12, FW_SEMIBOLD, L"Consolas");
    g_brBg = CreateSolidBrush(C_BG);
    g_fontsDpi = g_dpi;
}

void FillRound(HDC hdc, RECT rc, int r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(hdc, br), op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

void DrawTxt(HDC hdc, HFONT f, COLORREF c, RECT rc, const wchar_t* t, UINT fmt) {
    SelectObject(hdc, f);
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    DrawTextW(hdc, t, -1, &rc, fmt | DT_NOPREFIX);
}

RECT RR(int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; }
bool PtIn(RECT rc, int x, int y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

HICON CreateTrayIcon() {
    const int size = 16;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) return LoadIconW(nullptr, IDI_APPLICATION);
    auto* px = (DWORD*)bits;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            
            const int dx = x - 7, dy = y - 7;
            const bool plate = (dx * dx + dy * dy) <= 64;
            DWORD c = 0x00000000u;
            if (plate) c = 0xFF161C26u;
            
            if (x >= 6 && x <= 9 && y >= 3 && y <= 12) c = 0xFF2EE6A6u;
            
            if ((y == 4 && (x == 6 || x == 9)) || (y == 3 && x >= 7 && x <= 8)) c = 0xFF2EE6A6u;
            if ((y == 11 && (x == 6 || x == 9)) || (y == 12 && x >= 7 && x <= 8)) c = 0xFF2EE6A6u;
            px[y * size + x] = c;
        }
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask); DeleteObject(color);
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void UpdateTrayTip() {
    if (!g_trayAdded) return;
    WFormat(g_nid.szTip, 128, L"ScrollFix [%ls]", g_settings.enabled ? L"ON" : L"OFF");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void AddTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = CreateTrayIcon();
    WFormat(g_nid.szTip, 128, L"ScrollFix [ON]");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) { DestroyIcon(g_nid.hIcon); g_nid.hIcon = nullptr; }
    g_trayAdded = false;
}

constexpr int kUiW = 380;
constexpr int kUiH = 396;

enum SHit {
    SH_None, SH_Close, SH_Enable, SH_Auto, SH_Click,
    SH_Sens0, SH_Sens1, SH_Sens2, SH_Sens3,
    SH_LangRu, SH_LangEn, SH_Save, SH_Diag
};
int g_sHover = SH_None;
bool g_sDrag = false;
POINT g_sDragPt{};

RECT SClose() { return RR(SX(kUiW - 34), SX(10), SX(22), SX(22)); }
RECT SEnable() { return RR(SX(20), SX(82), SX(38), SX(20)); }
RECT SAuto() { return RR(SX(20), SX(116), SX(38), SX(20)); }
RECT SClick() { return RR(SX(20), SX(150), SX(38), SX(20)); }
RECT SSens0() { return RR(SX(20), SX(220), SX(78), SX(28)); }
RECT SSens1() { return RR(SX(104), SX(220), SX(78), SX(28)); }
RECT SSens2() { return RR(SX(188), SX(220), SX(78), SX(28)); }
RECT SSens3() { return RR(SX(272), SX(220), SX(88), SX(28)); }
RECT SLangRu() { return RR(SX(20), SX(276), SX(52), SX(24)); }
RECT SLangEn() { return RR(SX(78), SX(276), SX(52), SX(24)); }
RECT SSave() { return RR(SX(20), SX(330), SX(162), SX(34)); }
RECT SDiag() { return RR(SX(198), SX(330), SX(162), SX(34)); }

int SHitTest(int x, int y) {
    if (PtIn(SClose(), x, y)) return SH_Close;
    if (PtIn(SEnable(), x, y)) return SH_Enable;
    if (PtIn(SAuto(), x, y)) return SH_Auto;
    if (PtIn(SClick(), x, y)) return SH_Click;
    if (PtIn(SSens0(), x, y)) return SH_Sens0;
    if (PtIn(SSens1(), x, y)) return SH_Sens1;
    if (PtIn(SSens2(), x, y)) return SH_Sens2;
    if (PtIn(SSens3(), x, y)) return SH_Sens3;
    if (PtIn(SLangRu(), x, y)) return SH_LangRu;
    if (PtIn(SLangEn(), x, y)) return SH_LangEn;
    if (PtIn(SSave(), x, y)) return SH_Save;
    if (PtIn(SDiag(), x, y)) return SH_Diag;
    return SH_None;
}

Gdiplus::Color GpColor(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
    const float d = radius * 2.f;
    if (d <= 0.f) { path.AddRectangle(r); return; }
    path.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.f, 90.f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.f, 90.f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.f, 90.f);
    path.CloseFigure();
}

void FillRoundAA(HDC hdc, RECT rc, float radius, COLORREF fill) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    Gdiplus::RectF r((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                     (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    
    r.X += 0.5f; r.Y += 0.5f; r.Width -= 1.f; r.Height -= 1.f;
    if (r.Width < 1.f || r.Height < 1.f) return;
    float rad = radius;
    if (rad * 2.f > r.Width) rad = r.Width / 2.f;
    if (rad * 2.f > r.Height) rad = r.Height / 2.f;
    AddRoundRect(path, r, rad);
    Gdiplus::SolidBrush br(GpColor(fill));
    g.FillPath(&br, &path);
}

void FillEllipseAA(HDC hdc, RECT rc, COLORREF fill) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::SolidBrush br(GpColor(fill));
    g.FillEllipse(&br,
                  (Gdiplus::REAL)rc.left + 0.5f,
                  (Gdiplus::REAL)rc.top + 0.5f,
                  (Gdiplus::REAL)(rc.right - rc.left) - 1.f,
                  (Gdiplus::REAL)(rc.bottom - rc.top) - 1.f);
}

void DrawToggle(HDC hdc, RECT rc, bool on, bool hover) {
    COLORREF track = on ? C_ACCENT_DIM : C_PANEL2;
    if (hover) track = on ? RGB(30, 150, 110) : RGB(40, 48, 62);
    const float rad = (float)(rc.bottom - rc.top) * 0.5f;
    FillRoundAA(hdc, rc, rad, track);
    const int pad = SX(2);
    const int kn = rc.bottom - rc.top - pad * 2;
    RECT knob = on ? RR(rc.right - kn - pad, rc.top + pad, kn, kn)
                   : RR(rc.left + pad, rc.top + pad, kn, kn);
    FillEllipseAA(hdc, knob, on ? C_ACCENT : RGB(220, 226, 235));
}

void DrawChip(HDC hdc, RECT rc, const wchar_t* t, bool on, bool hover) {
    const COLORREF bg = on ? C_ACCENT : (hover ? RGB(40, 48, 62) : C_PANEL2);
    FillRoundAA(hdc, rc, (float)SX(7), bg);
    DrawTxt(hdc, g_fontSmall, on ? RGB(8, 16, 14) : C_TEXT, rc, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawCloseBtn(HDC hdc, RECT rc, bool hover) {
    FillRoundAA(hdc, rc, (float)SX(5), hover ? RGB(196, 43, 28) : C_PANEL2);
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(GpColor(hover ? RGB(255, 255, 255) : C_MUTED), (Gdiplus::REAL)(SX(2) > 1 ? SX(2) : 1));
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    const float m = (float)SX(7);
    g.DrawLine(&pen, rc.left + m, rc.top + m, rc.right - m, rc.bottom - m);
    g.DrawLine(&pen, rc.right - m, rc.top + m, rc.left + m, rc.bottom - m);
}

void DrawAppLogo(HDC hdc, int x, int y, int size) {
    FillRoundAA(hdc, RR(x, y, size, size), (float)size / 4.f, RGB(22, 28, 38));
    const int cx = x + size / 2;
    const int cy = y + size / 2;
    FillEllipseAA(hdc, RR(cx - size / 5, cy - size / 3, size * 2 / 5, size * 2 / 3), C_ACCENT);
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(GpColor(RGB(22, 28, 38)), 1.2f);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    const float a = (float)((size / 8 > 2) ? (size / 8) : 2);
    g.DrawLine(&pen, (float)cx - a, (float)cy - size / 6.f, (float)cx, (float)cy - size / 6.f - a);
    g.DrawLine(&pen, (float)cx, (float)cy - size / 6.f - a, (float)cx + a, (float)cy - size / 6.f);
    g.DrawLine(&pen, (float)cx - a, (float)cy + size / 6.f, (float)cx, (float)cy + size / 6.f + a);
    g.DrawLine(&pen, (float)cx, (float)cy + size / 6.f + a, (float)cx + a, (float)cy + size / 6.f);
}

void ShowDiag();

void PaintSettings(HWND hwnd) {
    RefreshDpi(hwnd);
    EnsureFonts();
    PAINTSTRUCT ps{};
    HDC scr = BeginPaint(hwnd, &ps);
    RECT cr{}; GetClientRect(hwnd, &cr);
    HDC hdc = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(hdc, bm);

    
    SetBkMode(hdc, TRANSPARENT);
#if defined(CLEARTYPE_QUALITY)
    
#endif

    FillRect(hdc, &cr, g_brBg);
    RECT accentLine = RR(0, 0, cr.right, SX(2));
    HBRUSH accentBr = CreateSolidBrush(C_ACCENT);
    FillRect(hdc, &accentLine, accentBr);
    DeleteObject(accentBr);

    const UiText& tr = T();
    DrawAppLogo(hdc, SX(14), SX(12), SX(28));
    DrawTxt(hdc, g_fontTitle, C_TEXT, RR(SX(50), SX(10), SX(250), SX(22)), L"ScrollFix",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTxt(hdc, g_fontSmall, C_MUTED, RR(SX(50), SX(32), SX(290), SX(18)), tr.subtitle,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawCloseBtn(hdc, SClose(), g_sHover == SH_Close);

    FillRoundAA(hdc, RR(SX(12), SX(56), SX(kUiW - 24), SX(132)), (float)SX(12), C_PANEL);
    DrawTxt(hdc, g_fontBody, C_TEXT, RR(SX(68), SX(80), SX(260), SX(22)), tr.filter,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawToggle(hdc, SEnable(), g_settings.enabled, g_sHover == SH_Enable);
    DrawTxt(hdc, g_fontBody, C_TEXT, RR(SX(68), SX(114), SX(260), SX(22)), tr.autostart,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawToggle(hdc, SAuto(), g_settings.autostart, g_sHover == SH_Auto);
    DrawTxt(hdc, g_fontBody, C_TEXT, RR(SX(68), SX(148), SX(260), SX(22)), tr.clickFix,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawToggle(hdc, SClick(), g_settings.clickFix, g_sHover == SH_Click);

    DrawTxt(hdc, g_fontSmall, C_MUTED, RR(SX(20), SX(200), SX(180), SX(16)), tr.sensitivity,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTxt(hdc, g_fontSmall, C_ACCENT, RR(SX(200), SX(200), SX(160), SX(16)),
            g_settings.lang == LangEn ? L"Auto recommended" : L"Авто — рекомендуется",
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawChip(hdc, SSens0(), tr.sensAuto, g_settings.sens == SensAuto, g_sHover == SH_Sens0);
    DrawChip(hdc, SSens1(), tr.sensSoft, g_settings.sens == SensSoft, g_sHover == SH_Sens1);
    DrawChip(hdc, SSens2(), tr.sensMed, g_settings.sens == SensMedium, g_sHover == SH_Sens2);
    DrawChip(hdc, SSens3(), tr.sensAgg, g_settings.sens == SensAggressive, g_sHover == SH_Sens3);

    DrawTxt(hdc, g_fontSmall, C_MUTED, RR(SX(20), SX(258), SX(100), SX(16)), tr.language,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawChip(hdc, SLangRu(), L"RU", g_settings.lang == LangRu, g_sHover == SH_LangRu);
    DrawChip(hdc, SLangEn(), L"EN", g_settings.lang == LangEn, g_sHover == SH_LangEn);

    FillRoundAA(hdc, SSave(), (float)SX(8), g_sHover == SH_Save ? RGB(70, 245, 185) : C_ACCENT);
    DrawTxt(hdc, g_fontBody, RGB(8, 16, 14), SSave(), tr.save, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    FillRoundAA(hdc, SDiag(), (float)SX(8), g_sHover == SH_Diag ? RGB(40, 50, 66) : C_PANEL2);
    DrawTxt(hdc, g_fontBody, C_TEXT, SDiag(), tr.diagnostics, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    
    HPEN frame = CreatePen(PS_SOLID, 1, RGB(40, 48, 62));
    HGDIOBJ oldPen = SelectObject(hdc, frame);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, cr.right - 1, cr.bottom - 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(frame);

    BitBlt(scr, 0, 0, cr.right, cr.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old); DeleteObject(bm); DeleteDC(hdc);
    EndPaint(hwnd, &ps);
}

void PersistSettings() {
    ApplyRuntime();
    SaveSettings();
    SetAutostart(g_settings.autostart);
    UpdateTrayTip();
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: EnsureFonts(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintSettings(hwnd); return 0;
    case WM_UI_STATS: return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (g_sDrag) {
            POINT p{x, y}; ClientToScreen(hwnd, &p);
            SetWindowPos(hwnd, nullptr, p.x - g_sDragPt.x, p.y - g_sDragPt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return 0;
        }
        int h = SHitTest(x, y);
        if (h != g_sHover) { g_sHover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: g_sHover = SH_None; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int h = SHitTest(x, y); g_sHover = h;
        if (h == SH_None && y < SX(54)) { g_sDrag = true; g_sDragPt = {x, y}; SetCapture(hwnd); return 0; }
        if (h == SH_Close) { DestroyWindow(hwnd); return 0; }
        if (h == SH_Enable) {
            g_settings.enabled = !g_settings.enabled;
            ApplyRuntime();
            UpdateTrayTip();
            if (!g_settings.enabled) ClearHold();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (h == SH_Auto) { g_settings.autostart = !g_settings.autostart; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_Click) {
            g_settings.clickFix = !g_settings.clickFix;
            ApplyRuntime();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (h == SH_Sens0) { g_settings.sens = SensAuto; ApplyRuntime(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_Sens1) { g_settings.sens = SensSoft; ApplyRuntime(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_Sens2) { g_settings.sens = SensMedium; ApplyRuntime(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_Sens3) { g_settings.sens = SensAggressive; ApplyRuntime(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_LangRu) { g_settings.lang = LangRu; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_LangEn) { g_settings.lang = LangEn; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_Save) { PersistSettings(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (h == SH_Diag) { ShowDiag(); return 0; }
        return 0;
    }
    case WM_LBUTTONUP: if (g_sDrag || GetCapture() == hwnd) { g_sDrag = false; ReleaseCapture(); } return 0;
    case WM_KEYDOWN: if (wParam == VK_ESCAPE) DestroyWindow(hwnd); return 0;
    case 0x02E0: { 
        const RECT* r = reinterpret_cast<RECT*>(lParam);
        g_dpi = HIWORD(wParam);
        if (g_dpi < 96) g_dpi = 96;
        DestroyFonts();
        EnsureFonts();
        if (r) SetWindowPos(hwnd, nullptr, r->left, r->top, SX(kUiW), SX(kUiH), SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY: g_settingsWnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettings() {
    if (g_settingsWnd && IsWindow(g_settingsWnd)) { ShowWindow(g_settingsWnd, SW_SHOW); SetForegroundWindow(g_settingsWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = SettingsProc; wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(C_BG);
        wc.lpszClassName = kUiClass; RegisterClassExW(&wc); reg = true;
    }
    RefreshDpi(nullptr);
    EnsureFonts();
    const int w = SX(kUiW), h = SX(kUiH);
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    
    g_settingsWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kUiClass, kAppName, WS_POPUP,
                                    wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2,
                                    w, h, nullptr, nullptr, g_inst, nullptr);
    if (g_settingsWnd) {
        RefreshDpi(g_settingsWnd);
        EnsureFonts();
        if (SX(kUiW) != w || SX(kUiH) != h) {
            SetWindowPos(g_settingsWnd, nullptr, 0, 0, SX(kUiW), SX(kUiH), SWP_NOMOVE | SWP_NOZORDER);
        }
        ShowWindow(g_settingsWnd, SW_SHOW);
    }
}

int g_dHover = 0;
bool g_dDrag = false;
POINT g_dDragPt{};

void PaintDiag(HWND hwnd) {
    EnsureFonts();
    PAINTSTRUCT ps{};
    HDC scr = BeginPaint(hwnd, &ps);
    RECT cr{}; GetClientRect(hwnd, &cr);
    HDC hdc = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(hdc, bm);
    FillRect(hdc, &cr, g_brBg);
    FillRound(hdc, RR(0, 0, cr.right, 3), 0, C_ACCENT);

    DrawTxt(hdc, g_fontTitle, C_TEXT, RR(SX(24), SX(16), SX(400), SX(30)), T().diagTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTxt(hdc, g_fontSmall, C_MUTED, RR(SX(24), SX(46), SX(500), SX(18)), T().diagHint,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT xc = RR(560, 12, 28, 28);
    FillRound(hdc, xc, 8, g_dHover == 1 ? RGB(50, 24, 30) : C_PANEL);
    DrawTxt(hdc, g_fontBody, g_dHover == 1 ? C_DANGER : C_MUTED, xc, L"X", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    FillRound(hdc, RR(20, 78, 560, 120), 14, C_PANEL);

    
    const int n = g_diagN;
    const int start = (g_diagW - n + kDiagCap) % kDiagCap;
    int x = 36;
    for (int i = 0; i < n && x < 560; ++i) {
        const DiagItem& it = g_diag[(start + i) % kDiagCap];
        COLORREF col = C_OK;
        if (it.kind == DK_Suppress) col = C_SUP;
        else if (it.kind == DK_Suspect) col = C_WARN;
        else if (it.kind == DK_Reverse) col = C_ACCENT;
        const wchar_t* arrow = it.dir > 0 ? L"↑" : L"↓";
        RECT r = RR(x, 110, 22, 48);
        DrawTxt(hdc, g_fontTitle, col, r, arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += 24;
    }

    FillRound(hdc, RR(20, 220, 560, 100), 14, C_PANEL);
    wchar_t a[160], b[160];
    const unsigned long long raw = g_raw.load();
    const unsigned long long sup = g_suppressed.load();
    const double pct = raw ? (100.0 * (double)sup / (double)raw) : 0.0;
    WFormat(a, 160, L"wheel raw %llu  drop %llu (%.1f%%)  pass %llu  rev %llu  click-drop %llu",
            raw, sup, pct, g_passed.load(), g_reversed.load(), g_clickSuppressed.load());
    WFormat(b, 160, L"avg interval %u ms    max~ %u ms    adapt samples %u    false-burst~ %u",
            g_avgInterval, g_maxInterval, g_adaptSamples, g_falseBurstLens);
    DrawTxt(hdc, g_fontMono, C_TEXT, RR(36, 236, 530, 22), a, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTxt(hdc, g_fontMono, C_MUTED, RR(36, 268, 530, 22), b, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(scr, 0, 0, cr.right, cr.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old); DeleteObject(bm); DeleteDC(hdc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK DiagProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: SetTimer(hwnd, TIMER_DIAG, 50, nullptr); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintDiag(hwnd); return 0;
    case WM_TIMER: if (wParam == TIMER_DIAG) InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_UI_STATS: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (g_dDrag) {
            POINT p{x, y}; ClientToScreen(hwnd, &p);
            SetWindowPos(hwnd, nullptr, p.x - g_dDragPt.x, p.y - g_dDragPt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return 0;
        }
        int h = PtIn(RR(560, 12, 28, 28), x, y) ? 1 : 0;
        if (h != g_dHover) { g_dHover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (PtIn(RR(560, 12, 28, 28), x, y)) { DestroyWindow(hwnd); return 0; }
        if (y < 70) { g_dDrag = true; g_dDragPt = {x, y}; SetCapture(hwnd); }
        return 0;
    }
    case WM_LBUTTONUP: if (g_dDrag || GetCapture() == hwnd) { g_dDrag = false; ReleaseCapture(); } return 0;
    case WM_KEYDOWN: if (wParam == VK_ESCAPE) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: KillTimer(hwnd, TIMER_DIAG); g_diagWnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowDiag() {
    if (g_diagWnd && IsWindow(g_diagWnd)) { ShowWindow(g_diagWnd, SW_SHOW); SetForegroundWindow(g_diagWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = DiagProc; wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kDiagClass; RegisterClassExW(&wc); reg = true;
    }
    const int w = 600, h = 350;
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_diagWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kDiagClass, T().diagTitle, WS_POPUP | WS_BORDER,
                                wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2,
                                w, h, nullptr, nullptr, g_inst, nullptr);
    if (g_diagWnd) {
        ShowWindow(g_diagWnd, SW_SHOW);
    }
}

void ToggleEnabled() {
    g_settings.enabled = !g_settings.enabled;
    ApplyRuntime();
    SaveSettings();
    UpdateTrayTip();
    if (!g_settings.enabled) ClearHold();
    if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, FALSE);
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt{}; GetCursorPos(&pt);
    const UiText& tr = T();
    HMENU menu = CreatePopupMenu();
    wchar_t ver[64]{};
    WFormat(ver, 64, L"ScrollFix v%ls", kAppVersion);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_VERSION, ver);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TOGGLE, g_settings.enabled ? tr.trayOff : tr.trayOn);
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, tr.traySettings);
    AppendMenuW(menu, MF_STRING, ID_DIAG, tr.trayDiag);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU lang = CreatePopupMenu();
    AppendMenuW(lang, MF_STRING | (g_settings.lang == LangRu ? MF_CHECKED : 0), ID_LANG_RU, L"Русский");
    AppendMenuW(lang, MF_STRING | (g_settings.lang == LangEn ? MF_CHECKED : 0), ID_LANG_EN, L"English");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)lang, tr.trayLang);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, tr.trayExit);
    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (cmd == ID_TOGGLE) ToggleEnabled();
    else if (cmd == ID_SETTINGS) ShowSettings();
    else if (cmd == ID_DIAG) ShowDiag();
    else if (cmd == ID_LANG_RU) { g_settings.lang = LangRu; SaveSettings(); if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, FALSE); }
    else if (cmd == ID_LANG_EN) { g_settings.lang = LangEn; SaveSettings(); if (g_settingsWnd) InvalidateRect(g_settingsWnd, nullptr, FALSE); }
    else if (cmd == ID_EXIT) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        return 0;
    case WM_EMIT_WHEEL:
        EmitWheelNow((int)wParam);
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_HOLD) {
            KillTimer(hwnd, TIMER_HOLD);
            OnHoldTimeout();
        }
        return 0;
    case WM_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
        else if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) ShowSettings();
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        ClearHold();
        if (g_settingsWnd) DestroyWindow(g_settingsWnd);
        if (g_diagWnd) DestroyWindow(g_diagWnd);
        RemoveTrayIcon();
        if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        CloseLog();
        DestroyFonts();
        timeEndPeriod(1);
        if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} 

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_inst = hInstance;
    EnableDpiAwareness();
    RefreshDpi(nullptr);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);
    QueryPerformanceFrequency(&g_qpf);
    if (g_qpf.QuadPart <= 0) g_qpf.QuadPart = 1000;

    g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!g_mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, T().alreadyRunning, kAppName, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    timeBeginPeriod(1);
    LoadSettings();
    g_settings.autostart = IsAutostartEnabled();
    ApplyRuntime();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) return 1;

    g_hwnd = CreateWindowExW(0, kWndClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    if (!g_hook) {
        MessageBoxW(nullptr, T().hookFail, kAppName, MB_OK | MB_ICONERROR);
        DestroyWindow(g_hwnd);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); }
    return (int)msg.wParam;
}
