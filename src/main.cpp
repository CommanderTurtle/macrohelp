// CursorOverlay v2.0 - Cursor crosshairs with live key display
// PromptFont-powered key visualization with 5-second fade history
// Outputs Tasket-compatible JSON to clicksession.txt
//
// Build: Visual Studio 2022/2025+ x64 Native Tools Command Prompt
//   build.bat          - MSVC build
//   build_mingw.bat    - MinGW-w64 build
//   CMakeLists.txt     - CMake build

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define GDIPVER 0x0110
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <mutex>
#include <cmath>

#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// =============================================================================
// CONFIGURATION
// =============================================================================

static constexpr COLORREF CROSSHAIR_COLOR   = RGB(220, 20, 20);
static constexpr int      CROSSHAIR_WIDTH   = 1;
static constexpr int      CENTER_DOT_R      = 4;
static constexpr COLORREF CENTER_DOT_COLOR  = RGB(255, 200, 0);
static constexpr COLORREF COLORKEY          = RGB(0, 0, 0);
static constexpr int      UPDATE_MS         = 16;
static constexpr int      COORD_PANEL_W     = 230;
static constexpr int      COORD_PANEL_H     = 85;

// Key display
static constexpr int      KEY_DISPLAY_W     = 420;
static constexpr int      KEY_DISPLAY_H     = 480;
static constexpr int      KEY_FADE_MS       = 5000;
static constexpr int      KEY_MAX_HISTORY   = 15;
static constexpr int      KEY_MAX_VISIBLE   = 11;       // current + -1..-10
static constexpr int      KEY_BOX_H         = 28;
static constexpr int      KEY_BOX_RADIUS    = 6;
static constexpr int      KEY_BOX_PAD_X     = 10;
static constexpr int      KEY_FONT_SIZE     = 14;
static constexpr int      KEY_GAP_X         = 4;
static constexpr int      KEY_GAP_Y         = 4;
static constexpr int      KEY_OFFSET_Y      = 30;
static constexpr int      KEY_PREFIX_W      = 28;

// Colors (ARGB premultiplied-ready)
static constexpr BYTE     KEY_BG_A          = 180;
static constexpr BYTE     KEY_BG_R          = 45;
static constexpr BYTE     KEY_BG_G          = 45;
static constexpr BYTE     KEY_BG_B          = 50;
static constexpr BYTE     KEY_BORDER_A      = 200;
static constexpr BYTE     KEY_BORDER_R      = 80;
static constexpr BYTE     KEY_BORDER_G      = 80;
static constexpr BYTE     KEY_BORDER_B      = 90;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

LRESULT CALLBACK CrosshairWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK CoordPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyDisplayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SaveCursorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK RecordKeysDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ManualKeysDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Core
bool InitApp();
void ShutdownApp();
void CreateCrosshairWindow();
void CreateCoordPanel();
void CreateKeyDisplayWindow();
void RenderKeyDisplay();
void DrawKeyDisplayEntry(Graphics& gfx, const std::vector<std::string>& keys,
                         int x, int y, int boxH, int index, float opacity);

// Font & glyphs
bool LoadPromptFont();
std::wstring KeyToGlyph(const std::string& keyName);

// Key tracking
void TrackKeyDown(UINT vkCode, bool extended);
void TrackKeyUp(UINT vkCode);
void FinalizeKeyCombo();

// VK mapping
std::string VkCodeToTasketName(UINT vkCode, bool extended);

// JSON output
bool AppendActionAndWait(const std::string& actionJson);
std::string EscapeJsonString(const std::string& s);
std::string WStringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWString(const std::string& str);
std::wstring GetDownloadsFolder();

// =============================================================================
// KEY HISTORY ENTRY
// =============================================================================

struct KeyHistoryEntry {
    std::vector<std::string> keys;
    DWORD tickMs;           // GetTickCount() when finalized
};

// =============================================================================
// GLOBAL STATE
// =============================================================================

// Instance
HINSTANCE   g_hInst = nullptr;

// Windows
HWND        g_hwndCrosshair = nullptr;
HWND        g_hwndCoordPanel = nullptr;
HWND        g_hwndKeyDisplay = nullptr;
HWND        g_hwndSaveDlg = nullptr;
HWND        g_hwndRecordDlg = nullptr;

// Visibility
bool        g_crosshairVisible = true;
bool        g_coordPanelVisible = true;
bool        g_keyDisplayVisible = true;
bool        g_running = true;

// GDI / GDI+
HFONT       g_hFontCoord = nullptr;
HFONT       g_hFontBold = nullptr;
HBRUSH      g_hbrColorkey = nullptr;
GdiplusStartupInput  g_gdiInput;
GdiplusStartupOutput g_gdiOutput;
ULONG_PTR   g_gdiToken = 0;

// Key display GDI+ resources
HANDLE      g_hFontResource = nullptr;   // AddFontMemResourceEx handle
HFONT       g_hPromptFont = nullptr;
HFONT       g_hPromptFontSmall = nullptr;

// Key display DIB
HBITMAP     g_hKeyDib = nullptr;
HDC         g_hKeyDibDC = nullptr;
void*       g_pKeyDibBits = nullptr;
int         g_keyDibW = 0;
int         g_keyDibH = 0;

// Key tracking
std::vector<std::string> g_activeCombo;
std::mutex  g_comboMutex;
KeyHistoryEntry g_keyHistory[KEY_MAX_HISTORY];
int         g_keyHistoryCount = 0;
int         g_keyHistoryHead = 0;
std::mutex  g_historyMutex;
bool        g_keyComboFinalized = true;

// Recording (existing)
bool        g_recordingKeys = false;
bool        g_waitingForKey = false;
std::vector<std::vector<std::string>> g_recordedSteps;
std::vector<std::string> g_currentStepKeys;
std::vector<std::vector<std::string>> g_manualSteps;
HHOOK       g_hKeyboardHook = nullptr;
HWND        g_hwndRecordKeysDlg = nullptr;
std::mutex  g_keyMutex;

// Screen
int         g_screenX = 0, g_screenY = 0;
int         g_screenW = 0, g_screenH = 0;

// Shared dialog data
POINT       g_savedCursorPos = {0, 0};

// Tray
#define TRAY_ICON_ID 1001

// =============================================================================
// PROMPTFONT GLYPH MAPPING
// =============================================================================

// Maps Tasket key names to PromptFont Unicode glyph characters
static std::wstring KeyToGlyph(const std::string& key) {
    static const struct { const char* name; const wchar_t* glyph; } map[] = {
        {"CONTROL_LEFT",   L"\u2427"},   // PF_KEYBOARD_CONTROL
        {"CONTROL_RIGHT",  L"\u2427"},
        {"SHIFT_LEFT",     L"\u2429"},   // PF_KEYBOARD_SHIFT
        {"SHIFT_RIGHT",    L"\u2429"},
        {"ALT_LEFT",       L"\u2428"},   // PF_KEYBOARD_ALT
        {"ALT_GR",         L"\u244A"},   // PF_KEYBOARD_ALT_GR
        {"WINDOWS",        L"\u242A"},   // PF_KEYBOARD_SUPER
        {"FN",             L"\u2426"},   // PF_KEYBOARD_FN
        {"LEFT_MOUSE",     L"\u278A"},   // PF_MOUSE_1
        {"RIGHT_MOUSE",    L"\u278B"},   // PF_MOUSE_2
        {"MIDDLE_MOUSE",   L"\u278C"},   // PF_MOUSE_3
        {"XBUTTON_1",      L"\u278D"},   // PF_MOUSE_4
        {"XBUTTON_2",      L"\u278E"},   // PF_MOUSE_5
        {"UP_ARROW",       L"\u23F6"},   // PF_KEYBOARD_UP
        {"DOWN_ARROW",     L"\u23F7"},   // PF_KEYBOARD_DOWN
        {"LEFT_ARROW",     L"\u23F4"},   // PF_KEYBOARD_LEFT
        {"RIGHT_ARROW",    L"\u23F5"},   // PF_KEYBOARD_RIGHT
        {"TAB",            L"\u242B"},   // PF_KEYBOARD_TAB
        {"CAPS_LOCK",      L"\u242C"},   // PF_KEYBOARD_CAPS
        {"BACKSPACE",      L"\u242D"},   // PF_KEYBOARD_BACKSPACE
        {"ENTER",          L"\u242E"},   // PF_KEYBOARD_ENTER
        {"ESCAPE",         L"\u242F"},   // PF_KEYBOARD_ESCAPE
        {"SCROLL_LOCK",    L"\u2431"},   // PF_KEYBOARD_SCROLL_LOCK
        {"NUM_LOCK",       L"\u2433"},   // PF_KEYBOARD_NUM_LOCK
        {"INSERT",         L"\u2434"},   // PF_KEYBOARD_INSERT
        {"HOME",           L"\u2435"},   // PF_KEYBOARD_HOME
        {"PAGE_UP",        L"\u2436"},   // PF_KEYBOARD_PAGE_UP
        {"DELETE",         L"\u2437"},   // PF_KEYBOARD_DELETE
        {"END",            L"\u2438"},   // PF_KEYBOARD_END
        {"PAGE_DOWN",      L"\u2439"},   // PF_KEYBOARD_PAGE_DOWN
        {"SPACEBAR",       L"\u243A"},   // PF_KEYBOARD_SPACE
        {nullptr, nullptr}
    };
    for (int i = 0; map[i].name; i++) {
        if (key == map[i].name) return map[i].glyph;
    }
    // Fallback: return the key name as text
    return Utf8ToWString(key);
}

// =============================================================================
// KEY TRACKING
// =============================================================================

static bool IsModifierKey(UINT vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static void TrackKeyDown(UINT vkCode, bool extended) {
    std::lock_guard<std::mutex> lock(g_comboMutex);

    // Don't track if it's our own hotkey being processed
    if (vkCode == '1' || vkCode == '2') {
        SHORT shift = GetAsyncKeyState(VK_SHIFT);
        SHORT alt = GetAsyncKeyState(VK_MENU);
        if ((shift & 0x8000) && (alt & 0x8000)) return;
    }

    std::string name = VkCodeToTasketName(vkCode, extended);
    if (name.empty()) return;

    // Check if already in combo (auto-repeat)
    for (const auto& k : g_activeCombo) {
        if (k == name) return;
    }

    g_activeCombo.push_back(name);
    g_keyComboFinalized = false;
}

static void TrackKeyUp(UINT vkCode) {
    std::lock_guard<std::mutex> lock(g_comboMutex);

    std::string name = VkCodeToTasketName(vkCode, false);
    if (name.empty()) return;

    // Remove from active combo
    auto it = g_activeCombo.begin();
    while (it != g_activeCombo.end()) {
        if (*it == name) {
            it = g_activeCombo.erase(it);
        } else {
            ++it;
        }
    }

    // If combo had non-modifier keys and is now empty, it was finalized by the down
    // If only modifiers were held and released, finalize now
    if (g_activeCombo.empty() && !g_keyComboFinalized) {
        // Combo already finalized when non-modifier was pressed
    }
}

static void FinalizeKeyCombo() {
    std::lock_guard<std::mutex> lock(g_historyMutex);

    if (g_activeCombo.empty()) return;

    KeyHistoryEntry entry;
    entry.keys = g_activeCombo;
    entry.tickMs = GetTickCount();

    g_keyHistory[g_keyHistoryHead] = entry;
    g_keyHistoryHead = (g_keyHistoryHead + 1) % KEY_MAX_HISTORY;
    if (g_keyHistoryCount < KEY_MAX_HISTORY) g_keyHistoryCount++;

    g_keyComboFinalized = true;
}

// =============================================================================
// LOW-LEVEL KEYBOARD HOOK
// =============================================================================

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        UINT vk = (UINT)pKbd->vkCode;
        bool ext = (pKbd->flags & LLKHF_EXTENDED) != 0;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // Always track for display (unless our dialog has focus)
            bool dialogHasFocus = (g_hwndSaveDlg && GetForegroundWindow() == g_hwndSaveDlg) ||
                                  (g_hwndRecordDlg && GetForegroundWindow() == g_hwndRecordDlg);

            if (!dialogHasFocus) {
                if (IsModifierKey(vk)) {
                    TrackKeyDown(vk, ext);
                } else {
                    TrackKeyDown(vk, ext);
                    // Non-modifier: finalize the combo
                    if (!g_keyComboFinalized) {
                        FinalizeKeyCombo();
                        g_activeCombo.clear();
                    }
                }
            }

            // Recording mode
            if (g_recordingKeys && g_waitingForKey) {
                if (!IsModifierKey(vk)) {
                    std::lock_guard<std::mutex> lock(g_keyMutex);
                    std::string keyName = VkCodeToTasketName(vk, ext);
                    if (!keyName.empty()) {
                        bool exists = false;
                        for (const auto& k : g_currentStepKeys) {
                            if (k == keyName) { exists = true; break; }
                        }
                        if (!exists) g_currentStepKeys.push_back(keyName);
                        g_waitingForKey = false;
                        if (g_hwndRecordKeysDlg && IsWindow(g_hwndRecordKeysDlg)) {
                            PostMessage(g_hwndRecordKeysDlg, WM_USER + 100, 0, 0);
                        }
                    }
                } else {
                    std::lock_guard<std::mutex> lock(g_keyMutex);
                    std::string keyName = VkCodeToTasketName(vk, ext);
                    if (!keyName.empty()) {
                        bool exists = false;
                        for (const auto& k : g_currentStepKeys) {
                            if (k == keyName) { exists = true; break; }
                        }
                        if (!exists) g_currentStepKeys.push_back(keyName);
                    }
                }
            }
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            TrackKeyUp(vk);
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

std::wstring GetDownloadsFolder() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path))) {
        return std::wstring(path) + L"\\Downloads\\clicksession.txt";
    }
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(buf) + L"\\Downloads\\clicksession.txt";
    }
    return L"clicksession.txt";
}

std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int need = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                    nullptr, 0, nullptr, nullptr);
    std::string s(need, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &s[0], need, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) return L"";
    int need = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring w(need, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &w[0], need);
    return w;
}

std::string EscapeJsonString(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': r += "\\\\"; break;
            case '"':  r += "\\\""; break;
            default: r += c; break;
        }
    }
    return r;
}

bool AppendActionAndWait(const std::string& actionJson) {
    std::wstring fp = GetDownloadsFolder();
    std::ofstream f(fp, std::ios::app);
    if (!f.is_open()) {
        std::ofstream cf(fp);
        if (!cf.is_open()) return false;
        cf.close();
        f.open(fp, std::ios::app);
        if (!f.is_open()) return false;
    }
    f << actionJson << "\n";
    f << "        {\n"
         "            \"duration\": 0.2,\n"
         "            \"type\": \"wait\"\n"
         "        },\n";
    f.close();
    return true;
}

// =============================================================================
// VK CODE -> TASKET NAME
// =============================================================================

std::string VkCodeToTasketName(UINT vkCode, bool extended) {
    switch (vkCode) {
        case 'A': case 'a': return "A";
        case 'B': case 'b': return "B";
        case 'C': case 'c': return "C";
        case 'D': case 'd': return "D";
        case 'E': case 'e': return "E";
        case 'F': case 'f': return "F";
        case 'G': case 'g': return "G";
        case 'H': case 'h': return "H";
        case 'I': case 'i': return "I";
        case 'J': case 'j': return "J";
        case 'K': case 'k': return "K";
        case 'L': case 'l': return "L";
        case 'M': case 'm': return "M";
        case 'N': case 'n': return "N";
        case 'O': case 'o': return "O";
        case 'P': case 'p': return "P";
        case 'Q': case 'q': return "Q";
        case 'R': case 'r': return "R";
        case 'S': case 's': return "S";
        case 'T': case 't': return "T";
        case 'U': case 'u': return "U";
        case 'V': case 'v': return "V";
        case 'W': case 'w': return "W";
        case 'X': case 'x': return "X";
        case 'Y': case 'y': return "Y";
        case 'Z': case 'z': return "Z";
        case '0': return "0"; case '1': return "1"; case '2': return "2";
        case '3': return "3"; case '4': return "4"; case '5': return "5";
        case '6': return "6"; case '7': return "7"; case '8': return "8"; case '9': return "9";
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
        case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
        case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        case VK_LCONTROL: return "CONTROL_LEFT";
        case VK_RCONTROL: return "CONTROL_RIGHT";
        case VK_LSHIFT: return "SHIFT_LEFT";
        case VK_RSHIFT: return "SHIFT_RIGHT";
        case VK_LMENU: return "ALT_LEFT";
        case VK_RMENU: return "ALT_GR";
        case VK_LWIN: return "WINDOWS";
        case VK_RWIN: return "WINDOWS";
        case VK_LBUTTON: return "LEFT_MOUSE";
        case VK_RBUTTON: return "RIGHT_MOUSE";
        case VK_MBUTTON: return "MIDDLE_MOUSE";
        case VK_XBUTTON1: return "XBUTTON_1";
        case VK_XBUTTON2: return "XBUTTON_2";
        case VK_NUMPAD0: return "NUMPAD_0"; case VK_NUMPAD1: return "NUMPAD_1";
        case VK_NUMPAD2: return "NUMPAD_2"; case VK_NUMPAD3: return "NUMPAD_3";
        case VK_NUMPAD4: return "NUMPAD_4"; case VK_NUMPAD5: return "NUMPAD_5";
        case VK_NUMPAD6: return "NUMPAD_6"; case VK_NUMPAD7: return "NUMPAD_7";
        case VK_NUMPAD8: return "NUMPAD_8"; case VK_NUMPAD9: return "NUMPAD_9";
        case VK_ADD: return "+"; case VK_SUBTRACT: return "-";
        case VK_MULTIPLY: return "*"; case VK_DIVIDE: return "/";
        case VK_DECIMAL: return "."; case VK_SEPARATOR: return ",";
        case VK_NUMLOCK: return "NUM_LOCK";
        case VK_SPACE: return "SPACEBAR";
        case VK_BACK: return "BACKSPACE";
        case VK_DELETE: return "DELETE";
        case VK_INSERT: return "INSERT";
        case VK_ESCAPE: return "ESCAPE";
        case VK_UP: return "UP_ARROW"; case VK_DOWN: return "DOWN_ARROW";
        case VK_LEFT: return "LEFT_ARROW"; case VK_RIGHT: return "RIGHT_ARROW";
        case VK_HOME: return "HOME"; case VK_END: return "END";
        case VK_PRIOR: return "PAGE_UP"; case VK_NEXT: return "PAGE_DOWN";
        case VK_TAB: return "TAB";
        case VK_CAPITAL: return "CAPS_LOCK";
        case VK_SCROLL: return "SCROLL_LOCK";
        case VK_SNAPSHOT: return "PRINT_SCREEN";
        case VK_PRINT: return "PRINT";
        case VK_MEDIA_PLAY_PAUSE: return "PLAYPAUSE_MEDIA";
        case VK_MEDIA_STOP: return "STOP_MEDIA";
        case VK_VOLUME_UP: return "VOLUME_UP";
        case VK_VOLUME_DOWN: return "VOLUME_DOWN";
        case VK_VOLUME_MUTE: return "VOLUME_MUTE";
        case VK_MEDIA_NEXT_TRACK: return "NEXT_TRACK";
        case VK_MEDIA_PREV_TRACK: return "PREVIOUS_TRACK";
        case VK_RETURN: return extended ? "NUMPAD ENTER" : "ENTER";
        default: {
            UINT sc = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
            if (sc) {
                wchar_t kn[32] = {};
                LONG lp = (sc << 16);
                if (extended) lp |= (1L << 24);
                if (GetKeyNameTextW(lp, kn, 32) > 0) {
                    return WStringToUtf8(kn);
                }
            }
            return "";
        }
    }
}


// =============================================================================
// PROMPTFONT LOADING
// =============================================================================

bool LoadPromptFont() {
    // Load font from embedded resource
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCE(FONT_PROMPT), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(g_hInst, hRes);
    if (!hData) return false;

    DWORD size = SizeofResource(g_hInst, hRes);
    void* pData = LockResource(hData);
    if (!pData || size == 0) return false;

    DWORD numFonts = 0;
    g_hFontResource = AddFontMemResourceEx(pData, size, nullptr, &numFonts);
    if (!g_hFontResource || numFonts == 0) return false;

    // Create HFONT handles for PromptFont
    g_hPromptFont = CreateFontW(KEY_FONT_SIZE + 2, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"PromptFont");

    g_hPromptFontSmall = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"PromptFont");

    return g_hPromptFont != nullptr;
}

// =============================================================================
// KEY DISPLAY RENDERING  (GDI+ with per-pixel alpha)
// =============================================================================

static void DrawRoundedRect(Graphics& gfx, int x, int y, int w, int h, int radius,
                            Color fillColor, Color borderColor) {
    GraphicsPath path;
    int d = radius * 2;
    // Top-left arc
    path.AddArc(x, y, d, d, 180, 90);
    // Top-right arc
    path.AddArc(x + w - d, y, d, d, 270, 90);
    // Bottom-right arc
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    // Bottom-left arc
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();

    SolidBrush fillBrush(fillColor);
    Pen borderPen(borderColor, 1.0f);
    gfx.FillPath(&fillBrush, &path);
    gfx.DrawPath(&borderPen, &path);
}

static int MeasureKeyTextWidth(Graphics& gfx, const std::wstring& text, const Font& font) {
    RectF layoutRect(0, 0, 1000, 100);
    RectF boundRect;
    gfx.MeasureString(text.c_str(), (INT)text.length(), &font, layoutRect, &boundRect);
    return (int)(boundRect.Width + 0.5f);
}

static void DrawKeyDisplayEntry(Graphics& gfx, const std::vector<std::string>& keys,
                                 int startX, int y, int boxH, int historyIndex,
                                 float opacity) {
    if (keys.empty() || opacity <= 0.02f) return;

    BYTE a = (BYTE)(opacity * 255);

    // Draw prefix label (-10 to current)
    if (historyIndex < 0) {
        std::wstring prefix = Utf8ToWString("" + std::to_string(historyIndex));
        // Actually show as "-1", "-2", etc.
        prefix = L"-";
        prefix += Utf8ToWString(std::to_string(-historyIndex));

        Font font(L"Segoe UI", 10, FontStyleRegular);
        SolidBrush brush(Color(a, 160, 160, 170));
        PointF pt((REAL)startX, (REAL)(y + 4));
        gfx.DrawString(prefix.c_str(), (INT)prefix.length(), &font, pt, &brush);
    }

    int x = startX + KEY_PREFIX_W;

    // Draw each key in the combo
    for (size_t i = 0; i < keys.size(); i++) {
        // "+" separator between keys
        if (i > 0) {
            Font sepFont(L"Segoe UI", 9, FontStyleRegular);
            SolidBrush sepBrush(Color(a, 180, 180, 190));
            PointF sepPt((REAL)x, (REAL)(y + 4));
            gfx.DrawString(L"+", 1, &sepFont, sepPt, &sepBrush);
            x += 16;
        }

        // Get glyph or text for this key
        std::wstring display = KeyToGlyph(keys[i]);

        // Measure text to size the box
        Font measureFont(L"PromptFont", (REAL)KEY_FONT_SIZE);
        int textW = MeasureKeyTextWidth(gfx, display, measureFont);
        if (textW < 12) textW = 12;
        int boxW = textW + KEY_BOX_PAD_X * 2;

        // Draw rounded rect background
        Color bgColor(a, KEY_BG_R, KEY_BG_G, KEY_BG_B);
        Color borderColor(a, KEY_BORDER_R, KEY_BORDER_G, KEY_BORDER_B);
        DrawRoundedRect(gfx, x, y, boxW, boxH, KEY_BOX_RADIUS, bgColor, borderColor);

        // Draw text
        Font font(L"PromptFont", (REAL)KEY_FONT_SIZE);
        SolidBrush textBrush(Color(a, 255, 255, 255));

        // Center text in box
        RectF layoutRect((REAL)(x + 2), (REAL)(y - 1), (REAL)(boxW - 4), (REAL)(boxH + 2));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        gfx.DrawString(display.c_str(), (INT)display.length(), &font, layoutRect, &sf, &textBrush);

        x += boxW + KEY_GAP_X;
    }
}

void RenderKeyDisplay() {
    if (!g_hwndKeyDisplay || !g_hKeyDibDC) return;

    // Get cursor position to position the window
    POINT cursorPt;
    GetCursorPos(&cursorPt);

    // Calculate window position (centered below cursor)
    int winX = cursorPt.x - KEY_DISPLAY_W / 2;
    int winY = cursorPt.y + KEY_OFFSET_Y;

    // Clamp to screen bounds
    if (winX < g_screenX) winX = g_screenX;
    if (winX + KEY_DISPLAY_W > g_screenX + g_screenW) winX = g_screenX + g_screenW - KEY_DISPLAY_W;
    if (winY + KEY_DISPLAY_H > g_screenY + g_screenH) {
        winY = cursorPt.y - KEY_DISPLAY_H - KEY_OFFSET_Y;
    }
    if (winY < g_screenY) winY = g_screenY;

    // Move window if needed
    RECT rc;
    GetWindowRect(g_hwndKeyDisplay, &rc);
    if (rc.left != winX || rc.top != winY) {
        SetWindowPos(g_hwndKeyDisplay, nullptr, winX, winY, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Create GDI+ bitmap from DIB
    Bitmap bmp(g_hKeyDib, nullptr);
    Graphics gfx(&bmp);
    gfx.SetSmoothingMode(SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gfx.SetCompositingMode(CompositingModeSourceOver);
    gfx.SetCompositingQuality(CompositingQualityHighQuality);

    // Clear to fully transparent
    gfx.Clear(Color(0, 0, 0, 0));

    // Read key history
    KeyHistoryEntry entries[KEY_MAX_HISTORY];
    int entryCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        if (g_keyHistoryCount > 0) {
            // Copy in reverse order (most recent first)
            for (int i = 0; i < g_keyHistoryCount && i < KEY_MAX_VISIBLE; i++) {
                int idx = (g_keyHistoryHead - 1 - i + KEY_MAX_HISTORY) % KEY_MAX_HISTORY;
                entries[i] = g_keyHistory[idx];
            }
            entryCount = min(g_keyHistoryCount, KEY_MAX_VISIBLE);
        }
    }

    // Also show currently active combo (not yet finalized)
    std::vector<std::string> activeKeys;
    {
        std::lock_guard<std::mutex> lock(g_comboMutex);
        if (!g_activeCombo.empty()) {
            activeKeys = g_activeCombo;
        }
    }

    DWORD now = GetTickCount();
    int y = 8;

    // Draw active (unfinalized) combo at top with pulsing opacity
    if (!activeKeys.empty()) {
        // Pulse between 60% and 100% opacity
        float pulse = 0.6f + 0.4f * sinf((now % 1000) / 1000.0f * 3.14159f * 2);
        DrawKeyDisplayEntry(gfx, activeKeys, 8, y, KEY_BOX_H, 0, pulse);
        y += KEY_BOX_H + KEY_GAP_Y + 4;
    }

    // Draw history entries
    for (int i = 0; i < entryCount; i++) {
        DWORD age = now - entries[i].tickMs;
        if (age > (DWORD)KEY_FADE_MS) continue;

        float opacity = 1.0f - (float)age / (float)KEY_FADE_MS;
        if (opacity < 0.02f) continue;

        // History index: 0 = current (most recent finalized), -1 = next, etc.
        int histIdx = -i;

        DrawKeyDisplayEntry(gfx, entries[i].keys, 8, y, KEY_BOX_H, histIdx, opacity);
        y += KEY_BOX_H + KEY_GAP_Y;

        if (y > KEY_DISPLAY_H - KEY_BOX_H) break;
    }

    // Update layered window with alpha
    POINT srcPt = {0, 0};
    SIZE size = {g_keyDibW, g_keyDibH};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC hdcScreen = GetDC(nullptr);
    UpdateLayeredWindow(g_hwndKeyDisplay, hdcScreen, nullptr, &size,
                        g_hKeyDibDC, &srcPt, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

// =============================================================================
// CROSSHAIR DRAWING
// =============================================================================

static void DrawCrosshair(HDC hdc) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwndCrosshair, &pt);
    RECT rc;
    GetClientRect(g_hwndCrosshair, &rc);

    // Fill with colorkey (transparent)
    FillRect(hdc, &rc, g_hbrColorkey);

    // Horizontal line
    HPEN hPen = CreatePen(PS_SOLID, CROSSHAIR_WIDTH, CROSSHAIR_COLOR);
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, rc.left, pt.y, nullptr);
    LineTo(hdc, rc.right, pt.y);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    // Vertical line
    hPen = CreatePen(PS_SOLID, CROSSHAIR_WIDTH, CROSSHAIR_COLOR);
    hOld = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, pt.x, rc.top, nullptr);
    LineTo(hdc, pt.x, rc.bottom);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    // Center dot
    HBRUSH hBr = CreateSolidBrush(CENTER_DOT_COLOR);
    HPEN hDotPen = CreatePen(PS_SOLID, 1, CENTER_DOT_COLOR);
    HPEN hOldDP = (HPEN)SelectObject(hdc, hDotPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    int r = CENTER_DOT_R;
    Ellipse(hdc, pt.x - r, pt.y - r, pt.x + r, pt.y + r);
    SelectObject(hdc, hOldDP);
    SelectObject(hdc, hOldBr);
    DeleteObject(hDotPen);
    DeleteObject(hBr);
}

// =============================================================================
// WINDOW PROCEDURE: CROSSHAIR
// =============================================================================

LRESULT CALLBACK CrosshairWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            if (wParam == IDT_UPDATE_CURSOR) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_HOTKEY: {
            if (wParam == IDH_SAVE_CURSOR) {
                if (!g_hwndSaveDlg || !IsWindow(g_hwndSaveDlg)) {
                    GetCursorPos(&g_savedCursorPos);
                    g_hwndSaveDlg = CreateDialogParamW(g_hInst,
                        MAKEINTRESOURCE(IDD_SAVE_CURSOR), hwnd,
                        (DLGPROC)SaveCursorDlgProc, 0);
                    if (g_hwndSaveDlg) {
                        ShowWindow(g_hwndSaveDlg, SW_SHOW);
                        SetForegroundWindow(g_hwndSaveDlg);
                    }
                } else {
                    SetForegroundWindow(g_hwndSaveDlg);
                }
            }
            else if (wParam == IDH_RECORD_KEYS) {
                if (!g_hwndRecordDlg || !IsWindow(g_hwndRecordDlg)) {
                    g_recordedSteps.clear();
                    g_currentStepKeys.clear();
                    g_hwndRecordDlg = CreateDialogParamW(g_hInst,
                        MAKEINTRESOURCE(IDD_RECORD_KEYS), hwnd,
                        (DLGPROC)RecordKeysDlgProc, 0);
                    if (g_hwndRecordDlg) {
                        ShowWindow(g_hwndRecordDlg, SW_SHOW);
                        SetForegroundWindow(g_hwndRecordDlg);
                    }
                } else {
                    SetForegroundWindow(g_hwndRecordDlg);
                }
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawCrosshair(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// =============================================================================
// WINDOW PROCEDURE: COORDINATE PANEL
// =============================================================================

LRESULT CALLBACK CoordPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, IDT_UPDATE_COORDS, UPDATE_MS, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == IDT_UPDATE_COORDS) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            // Background
            HBRUSH hBg = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);

            // Accent bar
            RECT rcAcc = {rc.left, rc.top, rc.right, rc.top + 3};
            HBRUSH hAcc = CreateSolidBrush(RGB(0, 150, 255));
            FillRect(hdc, &rcAcc, hAcc);
            DeleteObject(hAcc);

            // Coordinates
            POINT pt;
            GetCursorPos(&pt);
            std::wstringstream ss;
            ss << L"X: " << std::setw(5) << pt.x << L"  Y: " << std::setw(5) << pt.y;

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(240, 240, 240));
            SelectObject(hdc, g_hFontBold);
            RECT rt = {rc.left + 15, rc.top + 16, rc.right - 15, rc.top + 50};
            DrawTextW(hdc, ss.str().c_str(), -1, &rt,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Status
            SetTextColor(hdc, RGB(150, 150, 150));
            SelectObject(hdc, g_hFontCoord);
            std::wstring st = g_crosshairVisible ? L"[Active] Shift+Alt+1/2" : L"[Hidden]";
            RECT rs = {rc.left + 15, rc.top + 50, rc.right - 15, rc.bottom - 5};
            DrawTextW(hdc, st.c_str(), -1, &rs,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_USER + 1: {
            if (lParam == WM_LBUTTONUP) {
                ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW);
            }
            else if (lParam == WM_RBUTTONUP) {
                HMENU hm = CreatePopupMenu();
                AppendMenuW(hm, MF_STRING, IDM_TOGGLE_CROSSHAIR,
                    g_crosshairVisible ? L"Hide Crosshair" : L"Show Crosshair");
                AppendMenuW(hm, MF_STRING, IDM_TOGGLE_COORDS,
                    g_coordPanelVisible ? L"Hide Coordinates" : L"Show Coordinates");
                AppendMenuW(hm, MF_STRING, IDM_TOGGLE_KEYS,
                    g_keyDisplayVisible ? L"Hide Key Display" : L"Show Key Display");
                AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hm, MF_STRING, IDM_SAVE_CURSOR, L"Save Cursor (Shift+Alt+1)");
                AppendMenuW(hm, MF_STRING, IDM_RECORD_KEYS, L"Record Keys (Shift+Alt+2)");
                AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hm, MF_STRING, IDM_EXIT, L"Exit");
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_NONOTIFY,
                                         pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(hm);
                switch (cmd) {
                    case IDM_TOGGLE_CROSSHAIR:
                        g_crosshairVisible = !g_crosshairVisible;
                        ShowWindow(g_hwndCrosshair, g_crosshairVisible ? SW_SHOW : SW_HIDE);
                        break;
                    case IDM_TOGGLE_COORDS:
                        g_coordPanelVisible = !g_coordPanelVisible;
                        ShowWindow(g_hwndCoordPanel, g_coordPanelVisible ? SW_SHOW : SW_HIDE);
                        break;
                    case IDM_TOGGLE_KEYS:
                        g_keyDisplayVisible = !g_keyDisplayVisible;
                        ShowWindow(g_hwndKeyDisplay, g_keyDisplayVisible ? SW_SHOW : SW_HIDE);
                        break;
                    case IDM_SAVE_CURSOR:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_SAVE_CURSOR, 0);
                        break;
                    case IDM_RECORD_KEYS:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_RECORD_KEYS, 0);
                        break;
                    case IDM_EXIT:
                        g_running = false;
                        PostQuitMessage(0);
                        break;
                }
            }
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDM_EXIT) {
                g_running = false;
                PostQuitMessage(0);
            }
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, IDT_UPDATE_COORDS);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// =============================================================================
// WINDOW PROCEDURE: KEY DISPLAY
// =============================================================================

LRESULT CALLBACK KeyDisplayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    switch (msg) {
        case WM_TIMER:
            if (wParam == IDT_UPDATE_KEYS) {
                RenderKeyDisplay();
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            // We use UpdateLayeredWindow, not WM_PAINT
            ValidateRect(hwnd, nullptr);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, IDT_UPDATE_KEYS);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}


// =============================================================================
// DIALOG: SAVE CURSOR (modeless)
// =============================================================================

LRESULT CALLBACK SaveCursorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::vector<std::vector<int>> s_coords;
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            s_coords.clear();
            s_coords.push_back({0, 1000, g_savedCursorPos.x, g_savedCursorPos.y});
            std::wstringstream ss;
            ss << L"X: " << g_savedCursorPos.x << L"   Y: " << g_savedCursorPos.y;
            SetDlgItemTextW(hwnd, IDC_STATIC_COORDS, ss.str().c_str());
            SetDlgItemTextW(hwnd, IDC_EDIT_NAME, L"");
            SetFocus(GetDlgItem(hwnd, IDC_EDIT_NAME));
            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_ANOTHER_CURSOR: {
                    POINT pt;
                    GetCursorPos(&pt);
                    s_coords.push_back({0, 1000, pt.x, pt.y});
                    std::wstringstream ss;
                    ss << L"Added point " << s_coords.size() << L": X=" << pt.x << L" Y=" << pt.y;
                    SetDlgItemTextW(hwnd, IDC_STATIC_COORDS, ss.str().c_str());
                    return TRUE;
                }

                case IDOK: {
                    wchar_t nameBuf[256] = {};
                    GetDlgItemTextW(hwnd, IDC_EDIT_NAME, nameBuf, 256);
                    if (wcslen(nameBuf) == 0) {
                        MessageBoxW(hwnd, L"Please enter a name for this cursor position.",
                                   L"Name Required", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    std::string name = EscapeJsonString(WStringToUtf8(nameBuf));

                    std::string json;
                    json += "        {\n";
                    json += "            \"cursorMovsId\": \"" + name + "\",\n";
                    json += "            \"cursormovsmap\": [\n";
                    for (size_t i = 0; i < s_coords.size(); i++) {
                        json += "                [\n";
                        json += "                    " + std::to_string(s_coords[i][0]) + ",\n";
                        json += "                    " + std::to_string(s_coords[i][1]) + ",\n";
                        json += "                    " + std::to_string(s_coords[i][2]) + ",\n";
                        json += "                    " + std::to_string(s_coords[i][3]) + "\n";
                        json += "                ]";
                        if (i + 1 < s_coords.size()) json += ",";
                        json += "\n";
                    }
                    json += "            ],\n";
                    json += "            \"loop\": 1,\n";
                    json += "            \"optionalkeysstroke\": [\n";
                    json += "            ],\n";
                    json += "            \"type\": \"cursormovements\"\n";
                    json += "        },";

                    if (!AppendActionAndWait(json)) {
                        MessageBoxW(hwnd, L"Failed to save to clicksession.txt.",
                                   L"Error", MB_OK | MB_ICONERROR);
                    }
                    DestroyWindow(hwnd);
                    g_hwndSaveDlg = nullptr;
                    return TRUE;
                }

                case IDCANCEL:
                    DestroyWindow(hwnd);
                    g_hwndSaveDlg = nullptr;
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_hwndSaveDlg = nullptr;
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: RECORD KEYS (modeless)
// =============================================================================

LRESULT CALLBACK RecordKeysDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            g_hwndRecordKeysDlg = hwnd;
            g_recordingKeys = true;
            g_waitingForKey = false;
            g_currentStepKeys.clear();
            SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, L"Click 'Capture Key' then press your key(s).");
            SetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, L"");
            SetDlgItemTextW(hwnd, IDC_EDIT_DELAY, L"200");
            HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
            if (hList) SendMessage(hList, LB_RESETCONTENT, 0, 0);
            SetFocus(GetDlgItem(hwnd, IDC_EDIT_SEQ_NAME));
            return FALSE;
        }

        case WM_USER + 100: {
            if (!g_currentStepKeys.empty()) {
                bool isNew = true;
                if (!g_recordedSteps.empty()) {
                    if (g_recordedSteps.back() == g_currentStepKeys) isNew = false;
                }
                if (isNew) {
                    g_recordedSteps.push_back(g_currentStepKeys);
                    HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
                    if (hList) {
                        std::wstringstream ss;
                        ss << L"Step " << g_recordedSteps.size() << L": ";
                        for (size_t i = 0; i < g_currentStepKeys.size(); i++) {
                            if (i > 0) ss << L" + ";
                            ss << Utf8ToWString(g_currentStepKeys[i]);
                        }
                        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
                        int cnt = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
                        if (cnt > 0) SendMessage(hList, LB_SETTOPINDEX, cnt - 1, 0);
                    }
                }
                std::wstringstream ss;
                ss << L"Captured " << g_currentStepKeys.size() << L" key(s)."
                   << L" Click 'Capture Key' for next, or 'Add Another Key' for combo.";
                SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, ss.str().c_str());
            }
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_CAPTURE: {
                    g_currentStepKeys.clear();
                    g_waitingForKey = true;
                    SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, L"Press any key... (hold modifiers first)");
                    return TRUE;
                }

                case IDC_BTN_ADD_STEP: {
                    if (!g_currentStepKeys.empty()) {
                        g_recordedSteps.push_back(g_currentStepKeys);
                        HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
                        if (hList) {
                            std::wstringstream ss;
                            ss << L"Step " << g_recordedSteps.size() << L": ";
                            for (size_t i = 0; i < g_currentStepKeys.size(); i++) {
                                if (i > 0) ss << L" + ";
                                ss << Utf8ToWString(g_currentStepKeys[i]);
                            }
                            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
                        }
                        g_currentStepKeys.clear();
                    }
                    g_waitingForKey = true;
                    SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, L"New step - press key...");
                    return TRUE;
                }

                case IDC_BTN_ADD_ANOTHER_KEY: {
                    g_waitingForKey = true;
                    SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, L"Press additional key for combo...");
                    return TRUE;
                }

                case IDC_BTN_MANUAL: {
                    g_manualSteps.clear();
                    if (!g_currentStepKeys.empty()) {
                        g_recordedSteps.push_back(g_currentStepKeys);
                        g_currentStepKeys.clear();
                    }
                    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCE(IDD_MANUAL_KEYS),
                                       hwnd, (DLGPROC)ManualKeysDlgProc, 0) == IDOK) {
                        for (const auto& step : g_manualSteps) {
                            if (!step.empty()) {
                                g_recordedSteps.push_back(step);
                                HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
                                if (hList) {
                                    std::wstringstream ss;
                                    ss << L"Step " << g_recordedSteps.size() << L": ";
                                    for (size_t i = 0; i < step.size(); i++) {
                                        if (i > 0) ss << L" + ";
                                        ss << Utf8ToWString(step[i]);
                                    }
                                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
                                }
                            }
                        }
                    }
                    g_manualSteps.clear();
                    return TRUE;
                }

                case IDOK: {
                    if (!g_currentStepKeys.empty()) {
                        g_recordedSteps.push_back(g_currentStepKeys);
                        g_currentStepKeys.clear();
                    }
                    if (g_recordedSteps.empty()) {
                        MessageBoxW(hwnd, L"No keys recorded. Capture at least one key.",
                                   L"No Keys", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    wchar_t nameBuf[256] = {};
                    GetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, nameBuf, 256);
                    if (wcslen(nameBuf) == 0) {
                        MessageBoxW(hwnd, L"Please enter a name for this key sequence.",
                                   L"Name Required", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    wchar_t delayBuf[32] = {};
                    GetDlgItemTextW(hwnd, IDC_EDIT_DELAY, delayBuf, 32);
                    int stepDelay = 200;
                    if (wcslen(delayBuf) > 0) stepDelay = _wtoi(delayBuf);
                    if (stepDelay < 10) stepDelay = 10;

                    std::string name = EscapeJsonString(WStringToUtf8(nameBuf));

                    std::string json;
                    json += "        {\n";
                    json += "            \"keysSeqId\": \"" + name + "\",\n";
                    json += "            \"keysmap\": {\n";
                    for (size_t i = 0; i < g_recordedSteps.size(); i++) {
                        int tOff = (int)(i * stepDelay);
                        json += "                \"" + std::to_string(tOff) + "\": [\n";
                        json += "                    100,\n";
                        json += "                    [\n";
                        for (size_t j = 0; j < g_recordedSteps[i].size(); j++) {
                            json += "                        \"" + g_recordedSteps[i][j] + "\"";
                            if (j + 1 < g_recordedSteps[i].size()) json += ",";
                            json += "\n";
                        }
                        json += "                    ]\n";
                        json += "                ]";
                        if (i + 1 < g_recordedSteps.size()) json += ",";
                        json += "\n";
                    }
                    json += "            },\n";
                    json += "            \"loop\": 1,\n";
                    json += "            \"type\": \"keyssequence\"\n";
                    json += "        },";

                    if (!AppendActionAndWait(json)) {
                        MessageBoxW(hwnd, L"Failed to save to clicksession.txt.",
                                   L"Error", MB_OK | MB_ICONERROR);
                    }
                    g_recordingKeys = false;
                    g_hwndRecordKeysDlg = nullptr;
                    DestroyWindow(hwnd);
                    g_hwndRecordDlg = nullptr;
                    return TRUE;
                }

                case IDCANCEL:
                    g_recordingKeys = false;
                    g_hwndRecordKeysDlg = nullptr;
                    DestroyWindow(hwnd);
                    g_hwndRecordDlg = nullptr;
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            g_recordingKeys = false;
            g_hwndRecordKeysDlg = nullptr;
            DestroyWindow(hwnd);
            g_hwndRecordDlg = nullptr;
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: MANUAL KEYS (modal child)
// =============================================================================

LRESULT CALLBACK ManualKeysDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::vector<std::string> s_curKeys;
    static int s_stepNum;
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            s_curKeys.clear();
            s_stepNum = 1;

            const wchar_t* keys[] = {
                L"", L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J",
                L"K", L"L", L"M", L"N", L"O", L"P", L"Q", L"R", L"S", L"T", L"U",
                L"V", L"W", L"X", L"Y", L"Z",
                L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9",
                L"CONTROL_LEFT", L"CONTROL_RIGHT", L"SHIFT_LEFT", L"SHIFT_RIGHT",
                L"ALT_LEFT", L"ALT_GR", L"WINDOWS", L"FN",
                L"LEFT_MOUSE", L"RIGHT_MOUSE", L"MIDDLE_MOUSE", L"XBUTTON_1", L"XBUTTON_2",
                L"NUMPAD_0", L"NUMPAD_1", L"NUMPAD_2", L"NUMPAD_3", L"NUMPAD_4",
                L"NUMPAD_5", L"NUMPAD_6", L"NUMPAD_7", L"NUMPAD_8", L"NUMPAD_9",
                L"+", L"-", L"*", L"/", L".", L",", L"NUMPAD ENTER", L"NUM_LOCK",
                L"SPACEBAR", L"ENTER", L"BACKSPACE", L"DELETE", L"INSERT", L"ESCAPE",
                L"UP_ARROW", L"DOWN_ARROW", L"LEFT_ARROW", L"RIGHT_ARROW",
                L"HOME", L"END", L"PAGE_UP", L"PAGE_DOWN", L"TAB",
                L"PRINT_SCREEN", L"PRINT",
                L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8", L"F9", L"F10",
                L"F11", L"F12",
                L"PLAYPAUSE_MEDIA", L"STOP_MEDIA", L"VOLUME_UP", L"VOLUME_DOWN",
                L"VOLUME_MUTE", L"NEXT_TRACK", L"PREVIOUS_TRACK",
                L"CAPS_LOCK", L"NUM_LOCK", L"SCROLL_LOCK",
                nullptr
            };

            HWND c1 = GetDlgItem(hwnd, IDC_COMBO_KEY1);
            HWND c2 = GetDlgItem(hwnd, IDC_COMBO_KEY2);
            HWND c3 = GetDlgItem(hwnd, IDC_COMBO_KEY3);
            for (int i = 0; keys[i]; i++) {
                if (c1) SendMessageW(c1, CB_ADDSTRING, 0, (LPARAM)keys[i]);
                if (c2) SendMessageW(c2, CB_ADDSTRING, 0, (LPARAM)keys[i]);
                if (c3) SendMessageW(c3, CB_ADDSTRING, 0, (LPARAM)keys[i]);
            }
            if (c1) SendMessage(c1, CB_SETCURSEL, 0, 0);
            if (c2) SendMessage(c2, CB_SETCURSEL, 0, 0);
            if (c3) SendMessage(c3, CB_SETCURSEL, 0, 0);

            std::wstringstream ss;
            ss << L"Step " << s_stepNum;
            SetDlgItemTextW(hwnd, IDC_STATIC_STEP_NUM, ss.str().c_str());
            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_KEY: {
                    wchar_t k1[64] = {}, k2[64] = {}, k3[64] = {};
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY1, k1, 64);
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY2, k2, 64);
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY3, k3, 64);
                    if (!*k1 && !*k2 && !*k3) {
                        MessageBoxW(hwnd, L"Select at least one key.",
                                   L"No Key Selected", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    s_curKeys.clear();
                    if (*k1) s_curKeys.push_back(WStringToUtf8(k1));
                    if (*k2) s_curKeys.push_back(WStringToUtf8(k2));
                    if (*k3) s_curKeys.push_back(WStringToUtf8(k3));
                    g_manualSteps.push_back(s_curKeys);

                    SendDlgItemMessageW(hwnd, IDC_COMBO_KEY1, CB_SETCURSEL, 0, 0);
                    SendDlgItemMessageW(hwnd, IDC_COMBO_KEY2, CB_SETCURSEL, 0, 0);
                    SendDlgItemMessageW(hwnd, IDC_COMBO_KEY3, CB_SETCURSEL, 0, 0);

                    s_stepNum++;
                    std::wstringstream ss;
                    ss << L"Step " << s_stepNum << L" (added " << s_curKeys.size() << L" keys)";
                    SetDlgItemTextW(hwnd, IDC_STATIC_STEP_NUM, ss.str().c_str());
                    s_curKeys.clear();
                    return TRUE;
                }

                case IDC_BTN_ADD_STEP_MANUAL: {
                    wchar_t k1[64] = {}, k2[64] = {}, k3[64] = {};
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY1, k1, 64);
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY2, k2, 64);
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY3, k3, 64);
                    if (*k1 || *k2 || *k3) {
                        s_curKeys.clear();
                        if (*k1) s_curKeys.push_back(WStringToUtf8(k1));
                        if (*k2) s_curKeys.push_back(WStringToUtf8(k2));
                        if (*k3) s_curKeys.push_back(WStringToUtf8(k3));
                        g_manualSteps.push_back(s_curKeys);
                        s_curKeys.clear();
                    }
                    SendDlgItemMessageW(hwnd, IDC_COMBO_KEY1, CB_SETCURSEL, 0, 0);
                    SendDlgItemMessageW(hwnd, IDC_COMBO_KEY2, CB_SETCURSEL, 0, 0);
                    SendDlgItemMessageW(hwnd, IDC_COMBO_KEY3, CB_SETCURSEL, 0, 0);
                    s_stepNum++;
                    std::wstringstream ss;
                    ss << L"Step " << s_stepNum;
                    SetDlgItemTextW(hwnd, IDC_STATIC_STEP_NUM, ss.str().c_str());
                    return TRUE;
                }

                case IDOK: {
                    wchar_t k1[64] = {}, k2[64] = {}, k3[64] = {};
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY1, k1, 64);
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY2, k2, 64);
                    GetDlgItemTextW(hwnd, IDC_COMBO_KEY3, k3, 64);
                    if (*k1 || *k2 || *k3) {
                        s_curKeys.clear();
                        if (*k1) s_curKeys.push_back(WStringToUtf8(k1));
                        if (*k2) s_curKeys.push_back(WStringToUtf8(k2));
                        if (*k3) s_curKeys.push_back(WStringToUtf8(k3));
                        g_manualSteps.push_back(s_curKeys);
                    }
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}


// =============================================================================
// WINDOW CREATION
// =============================================================================

void CreateCrosshairWindow() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = CrosshairWndProc;
    wcex.hInstance = g_hInst;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"CursorOverlayCrosshair";
    wcex.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_CURSOROVERLAY));
    wcex.hIconSm = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_SMALL));
    RegisterClassExW(&wcex);

    g_hwndCrosshair = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CursorOverlayCrosshair", L"CursorOverlay",
        WS_POPUP,
        g_screenX, g_screenY, g_screenW, g_screenH,
        nullptr, nullptr, g_hInst, nullptr);

    if (g_hwndCrosshair) {
        SetLayeredWindowAttributes(g_hwndCrosshair, COLORKEY, 0, LWA_COLORKEY);
    }
}

void CreateCoordPanel() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = CoordPanelWndProc;
    wcex.hInstance = g_hInst;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"CursorOverlayCoordPanel";
    RegisterClassExW(&wcex);

    int x = g_screenX + g_screenW - COORD_PANEL_W - 20;
    int y = g_screenY + 20;

    g_hwndCoordPanel = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CursorOverlayCoordPanel", L"CursorOverlay",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, COORD_PANEL_W, COORD_PANEL_H,
        nullptr, nullptr, g_hInst, nullptr);
}

void CreateKeyDisplayWindow() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = KeyDisplayWndProc;
    wcex.hInstance = g_hInst;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"CursorOverlayKeyDisplay";
    RegisterClassExW(&wcex);

    g_hwndKeyDisplay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CursorOverlayKeyDisplay", L"CursorOverlay Keys",
        WS_POPUP,
        g_screenX, g_screenY, KEY_DISPLAY_W, KEY_DISPLAY_H,
        nullptr, nullptr, g_hInst, nullptr);

    if (!g_hwndKeyDisplay) return;

    // Create 32bpp DIB section for alpha rendering
    g_keyDibW = KEY_DISPLAY_W;
    g_keyDibH = KEY_DISPLAY_H;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_keyDibW;
    bmi.bmiHeader.biHeight = -g_keyDibH;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(nullptr);
    g_hKeyDib = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &g_pKeyDibBits, nullptr, 0);
    g_hKeyDibDC = CreateCompatibleDC(hdcScreen);
    SelectObject(g_hKeyDibDC, g_hKeyDib);
    ReleaseDC(nullptr, hdcScreen);

    // Clear to fully transparent
    if (g_pKeyDibBits) {
        memset(g_pKeyDibBits, 0, g_keyDibW * g_keyDibH * 4);
    }
}

// =============================================================================
// INITIALIZATION & SHUTDOWN
// =============================================================================

bool InitApp() {
    // Screen dimensions
    g_screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Fonts
    g_hFontCoord = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontBold = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hbrColorkey = CreateSolidBrush(COLORKEY);

    // GDI+
    GdiplusStartup(&g_gdiToken, &g_gdiInput, &g_gdiOutput);

    // PromptFont
    LoadPromptFont();

    return g_hFontCoord && g_hFontBold && g_hbrColorkey;
}

void ShutdownApp() {
    // Cleanup dialogs
    if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) DestroyWindow(g_hwndSaveDlg);
    if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) DestroyWindow(g_hwndRecordDlg);

    // Cleanup key display DIB
    if (g_hKeyDibDC) DeleteDC(g_hKeyDibDC);
    if (g_hKeyDib) DeleteObject(g_hKeyDib);

    // Cleanup windows
    if (g_hwndKeyDisplay) DestroyWindow(g_hwndKeyDisplay);
    if (g_hwndCrosshair) DestroyWindow(g_hwndCrosshair);
    if (g_hwndCoordPanel) DestroyWindow(g_hwndCoordPanel);

    // Cleanup GDI objects
    if (g_hFontCoord) DeleteObject(g_hFontCoord);
    if (g_hFontBold) DeleteObject(g_hFontBold);
    if (g_hbrColorkey) DeleteObject(g_hbrColorkey);
    if (g_hPromptFont) DeleteObject(g_hPromptFont);
    if (g_hPromptFontSmall) DeleteObject(g_hPromptFontSmall);

    // Remove PromptFont from memory
    if (g_hFontResource) RemoveFontMemResourceEx(g_hFontResource);

    // Shutdown GDI+
    if (g_gdiToken) GdiplusShutdown(g_gdiToken);
}

// =============================================================================
// ENTRY POINT
// =============================================================================

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;

    // Single instance
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"CursorOverlay_SingleInstance_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND e = FindWindowW(L"CursorOverlayCrosshair", L"CursorOverlay");
        if (e) { SetForegroundWindow(e); FlashWindow(e, TRUE); }
        return 0;
    }

    if (!InitApp()) {
        MessageBoxW(nullptr, L"Failed to initialize application.", L"CursorOverlay Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create windows
    CreateCrosshairWindow();
    CreateCoordPanel();
    CreateKeyDisplayWindow();

    if (!g_hwndCrosshair || !g_hwndCoordPanel) {
        MessageBoxW(nullptr, L"Failed to create overlay windows.", L"CursorOverlay Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    // Hotkeys
    RegisterHotKey(g_hwndCrosshair, IDH_SAVE_CURSOR, MOD_SHIFT | MOD_ALT, '1');
    RegisterHotKey(g_hwndCrosshair, IDH_RECORD_KEYS, MOD_SHIFT | MOD_ALT, '2');

    // Keyboard hook (always active for key display)
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    // Show windows
    ShowWindow(g_hwndCrosshair, SW_SHOW);
    ShowWindow(g_hwndCoordPanel, nCmdShow);
    if (g_hwndKeyDisplay) ShowWindow(g_hwndKeyDisplay, SW_SHOW);
    UpdateWindow(g_hwndCrosshair);
    UpdateWindow(g_hwndCoordPanel);

    // Timers
    SetTimer(g_hwndCrosshair, IDT_UPDATE_CURSOR, UPDATE_MS, nullptr);
    if (g_hwndKeyDisplay) {
        SetTimer(g_hwndKeyDisplay, IDT_UPDATE_KEYS, UPDATE_MS, nullptr);
    }

    // Tray icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndCoordPanel;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_CURSOROVERLAY));
    wcscpy_s(nid.szTip, L"CursorOverlay - Shift+Alt+1/2");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Balloon notification
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"CursorOverlay");
    wcscpy_s(nid.szInfo, L"Running! Shift+Alt+1=Save  Shift+Alt+2=Record  Key display active");
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    // Main message loop
    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0)) {
        bool dlgMsg = false;
        if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg) && IsDialogMessage(g_hwndSaveDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg) && IsDialogMessage(g_hwndRecordDlg, &msg)) {
            dlgMsg = true;
        }

        if (!dlgMsg) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Cleanup
    Shell_NotifyIcon(NIM_DELETE, &nid);
    UnregisterHotKey(g_hwndCrosshair, IDH_SAVE_CURSOR);
    UnregisterHotKey(g_hwndCrosshair, IDH_RECORD_KEYS);
    if (g_hKeyboardHook) { UnhookWindowsHookEx(g_hKeyboardHook); g_hKeyboardHook = nullptr; }
    KillTimer(g_hwndCrosshair, IDT_UPDATE_CURSOR);
    if (g_hwndKeyDisplay) KillTimer(g_hwndKeyDisplay, IDT_UPDATE_KEYS);
    ShutdownApp();
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return (int)msg.wParam;
}
