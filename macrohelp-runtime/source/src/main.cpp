// CursorOverlay v2.0 - Cursor crosshairs with live key display
// PromptFont-powered key visualization with 5-second fade history
// Outputs Tasket-compatible JSON to clicksession.txt
//
// Build: Visual Studio 2022/2025+ x64 Native Tools Command Prompt
//   build.bat          - MSVC build
//   build_mingw.bat    - MinGW-w64 build
//   CMakeLists.txt     - CMake build

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#define GDIPVER 0x0110
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cwctype>
#include <algorithm>
#include <stdexcept>
#include <utility>

#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;

// =============================================================================
// CONFIGURATION
// =============================================================================

static constexpr COLORREF CROSSHAIR_COLOR   = RGB(220, 20, 20);
static constexpr int      CROSSHAIR_WIDTH   = 1;
static constexpr int      CENTER_DOT_R      = 4;
static constexpr COLORREF CENTER_DOT_COLOR  = RGB(255, 200, 0);
static constexpr COLORREF COLORKEY          = RGB(0, 0, 0);
static constexpr int      DEFAULT_UPDATE_MS = 16;
static constexpr int      MIN_CROSSHAIR_UPDATE_MS = 8;
static constexpr int      COORD_PANEL_CLIENT_W = 760;
static constexpr int      COORD_PANEL_CLIENT_H = 76;
static constexpr int      COORD_PANEL_VISIBLE_W = 760;
static constexpr int      COORD_PANEL_VISIBLE_H = 76;
static constexpr UINT_PTR SAVE_CURSOR_WATCH_TIMER = 4701;
static constexpr UINT_PTR RECORD_KEYS_WATCH_TIMER = 4702;
static constexpr UINT_PTR CLICK_ACTION_WATCH_TIMER = 4703;
static constexpr UINT_PTR VIEW_TOGGLE_WATCH_TIMER = 4704;
static constexpr UINT_PTR PASTE_BUFFER_WATCH_TIMER = 4705;
static constexpr UINT_PTR REGISTRY_HUB_WATCH_TIMER = 4706;
static constexpr int      GRID_SPACING_PX = 120;
static constexpr int      PASTE_BUFFER_MAX_VISIBLE_LINES = 50;
static constexpr int      REGISTRY_POINT_COUNT = 16;
static constexpr int      REGISTRY_ROUTER_POINT_COUNT = 6;

// Key display
static constexpr int      KEY_DISPLAY_W     = 420;
static constexpr int      KEY_DISPLAY_H     = 480;
static constexpr int      KEY_FADE_MS       = 5000;
static constexpr int      KEY_MAX_HISTORY   = 15;
static constexpr int      KEY_MAX_VISIBLE   = 11;       // current + -1..-10
static constexpr int      KEY_BOX_H         = 32;
static constexpr int      KEY_BOX_RADIUS    = 6;
static constexpr int      KEY_BOX_PAD_X     = 10;
static constexpr int      KEY_FONT_SIZE     = 16;
static constexpr int      KEY_GAP_X         = 4;
static constexpr int      KEY_GAP_Y         = 4;
static constexpr int      KEY_OFFSET_X      = 24;
static constexpr int      KEY_OFFSET_Y      = 18;
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
LRESULT CALLBACK CirclePlacerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ClickActionDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ViewTogglesDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK PasteBuffersDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK RegistryHubDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Core
bool InitApp();
void ShutdownApp();
void EnablePhysicalPixelDpiAwareness();
void RefreshScreenMetrics();
int DetectDisplayUpdateIntervalMs();
void CreateCrosshairWindow();
void CreateCoordPanel();
void CreateKeyDisplayWindow();
void RegisterKeyboardRawInput(HWND hwnd);
void RenderKeyDisplay();
void DrawKeyDisplayEntry(Graphics& gfx, const std::vector<std::string>& keys,
                         int x, int y, int boxH, int index, float opacity);

// Font & glyphs
bool LoadPromptFont();
std::unique_ptr<Font> CreatePromptFont(REAL size);
std::unique_ptr<Font> CreateKeyTextFont(const std::string& keyName, REAL size);
std::wstring KeyToGlyph(const std::string& keyName);
std::wstring GetKeyHudStatusText();
void PushKeyHistory(const std::vector<std::string>& keys);
void SeedKeyHudStartupPulse();

// Key tracking
void TrackKeyDown(UINT vkCode, bool extended);
void TrackKeyUp(UINT vkCode, bool extended);
void FinalizeKeyCombo(bool clearActive);
void TrackDisplayKeyDown(UINT vkCode, bool extended);
void PollKeyboardState();

// VK mapping
std::string VkCodeToTasketName(UINT vkCode, bool extended);

// JSON output
bool AppendActionAndWait(const std::string& actionJson);
static std::string BuildCursorMovementActionJson(
    const std::string& name,
    const std::vector<std::vector<int>>& coords,
    const std::vector<std::string>& optionalKeys = {});
static std::string BuildKeysSequenceActionJson(
    const std::string& name,
    const std::vector<std::vector<std::string>>& steps,
    int stepDelay,
    int releaseAfterMs = 100);
static std::string BuildMouseClickActionJson(const std::string& name, const std::string& mouseToken, int releaseAfterMs);
static std::string BuildLeftClickActionJson(const std::string& name);
static std::string BuildPasteActionJson(const std::string& contentId, const std::string& content);
static std::string BuildWaitActionJson(double durationSeconds);
static std::string StripTrailingActionComma(std::string actionJson);
static const char* ClickButtonToken(int buttonIndex);
static const char* ClickButtonSlug(int buttonIndex);
static bool ScheduleTasketTempTask(const std::string& taskName, const std::string& taskJson, int delaySeconds, std::wstring* status = nullptr, bool run = true, bool cleanup = true);
static bool ScheduleTasketCursorMove(const POINT& target, int moveMs, int delaySeconds, std::wstring* status = nullptr);
static bool ScheduleTasketClick(int buttonIndex, int releaseAfterMs, int delaySeconds, std::wstring* status = nullptr);
static bool ScheduleTasketPasteBuffer(int slotIndex, const std::wstring& text, std::wstring* status = nullptr);
static bool ScheduleTasketPasteSequence(const std::vector<int>& slots, std::wstring* status = nullptr);
static bool ScheduleTasketZoneFlow(int flowIndex, POINT start, POINT end, std::wstring* status = nullptr);
static bool ScheduleTasketZoneCaptureToPasteBuffer(int flowIndex, POINT start, POINT end, int targetSlot, std::wstring* status = nullptr);
static bool StopAllTasketTasks(std::wstring* status = nullptr);
static bool ScheduleTasketRegistryHubScript(std::wstring* status = nullptr);
std::string EscapeJsonString(const std::string& s);
std::string WStringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWString(const std::string& str);
std::wstring GetDownloadsFolder();
std::wstring GetMacrohelpDataDir();
std::wstring GetMacrohelpStatePath();
std::wstring GetMacrohelpCirclePreviewPath();
std::wstring GetMacrohelpCommandPath();
void WriteBackendState(bool force = false);
void RefreshCirclePreviewFromFile();
void HideCirclePreview();
void StartCirclePlacement();
void StartClickAction(int buttonIndex);
void StartViewToggles();
void StartPasteBuffers();
void StartPasteBuffersZoneStore(POINT start, POINT end);
void StartRegistryHub();
void DrawCirclePreview(HDC hdc);
void DrawCursorGrid(HDC hdc, const RECT& rc, POINT pt);
void ProcessBackendCommandFile();
static void SetCircleStatus(const std::wstring& value);
static std::wstring TrimWide(const std::wstring& value);
static std::string CompactLower(const std::wstring& value);
static void CloseSaveCursorDialog(HWND hwnd);
static void CloseRecordKeysDialog(HWND hwnd);
static void CloseClickActionDialog(HWND hwnd);
static void ClosePasteBuffersDialog(HWND hwnd);
static void CloseRegistryHubDialog(HWND hwnd);
static void LoadRegistryHubState();
static void SaveRegistryHubState();
static void UpdateRegistryHubDialogText(HWND hwnd);
static void ResetCirclePlacement(bool closeDialog);
static bool UpdatePreviewFromInput(HWND hwnd);

// =============================================================================
// KEY HISTORY ENTRY
// =============================================================================

struct KeyHistoryEntry {
    std::vector<std::string> keys;
    DWORD tickMs;           // GetTickCount() when finalized
};

struct CirclePreview {
    bool enabled = false;
    int originX = 0;
    int originY = 0;
    int targetX = 0;
    int targetY = 0;
    int radius = 0;
    bool showQuadrants = true;
    bool showTarget = true;
    bool zoneEnabled = false;
    POINT zoneStart = {0, 0};
    POINT zoneEnd = {0, 0};
    DWORD expiresTickMs = 0;
    FILETIME lastWriteTime = {};
    bool hasMouseAnchor = false;
    POINT mouseAnchor = {0, 0};
    std::wstring label;
    std::wstring angleLabel;
};

enum class CircleStage {
    Idle,
    Distance,
    Mode,
    Angle,
    Confirm,
    ZoneAction
};

enum class CircleAngleMode {
    None,
    Radians,
    Degrees
};

enum class SaveCursorStage {
    Name,
    Command
};

enum class RecordKeysStage {
    Name,
    Command,
    Paste
};

enum class ClickActionStage {
    Command,
    HoldMs
};

struct CirclePlacementState {
    bool active = false;
    CircleStage stage = CircleStage::Idle;
    CircleAngleMode angleMode = CircleAngleMode::None;
    POINT cancelOnMouseMoveFrom = {0, 0};
    POINT origin = {0, 0};
    POINT target = {0, 0};
    double radius = 0.0;
    double thetaRadians = 0.0;
    bool targetValid = false;
    bool zoneMode = false;
    bool zoneHasStart = false;
    POINT zoneStart = {0, 0};
    POINT zoneEnd = {0, 0};
    int selectedChoice = 1;
    DWORD openedTickMs = 0;
    std::wstring lastInput;
    std::wstring status;
};

struct SaveCursorState {
    SaveCursorStage stage = SaveCursorStage::Name;
    std::vector<std::vector<int>> coords;
    std::wstring name;
    POINT cancelOnMouseMoveFrom = {0, 0};
    DWORD openedTickMs = 0;
};

struct RecordKeysState {
    RecordKeysStage stage = RecordKeysStage::Name;
    std::wstring name;
    int stepDelay = 200;
    POINT cancelOnMouseMoveFrom = {0, 0};
    DWORD openedTickMs = 0;
};

struct ClickActionState {
    ClickActionStage stage = ClickActionStage::Command;
    int buttonIndex = 0;
    bool nativeHeld = false;
    POINT cancelOnMouseMoveFrom = {0, 0};
    DWORD openedTickMs = 0;
};

struct ViewToggleState {
    POINT cancelOnMouseMoveFrom = {0, 0};
    DWORD openedTickMs = 0;
};

enum class PasteBufferStage {
    Command,
    Edit,
    PasteSelect,
    PasteSequence,
    ZoneCommand,
    ZoneEdit,
    ZonePlaySelect,
    ZonePlayFlow,
    ZoneCaptureZoneSelect,
    ZoneCaptureFlow,
    ZoneCaptureTarget
};

struct PasteBufferState {
    PasteBufferStage stage = PasteBufferStage::Command;
    int activeSlot = -1;
    int activeZoneSlot = -1;
    int activeZoneFlow = -1;
    int visibleLines = 1;
    bool pendingZoneFromCircle = false;
    POINT pendingZoneStart = {0, 0};
    POINT pendingZoneEnd = {0, 0};
    POINT cancelOnMouseMoveFrom = {0, 0};
    DWORD openedTickMs = 0;
};

struct ZoneBuffer {
    bool set = false;
    POINT start = {0, 0};
    POINT end = {0, 0};
};

enum class RegistryHubStage {
    Command,
    Script,
    Help,
    RegistryPath,
    ExportPath,
    ImportPath,
    PowerShellPath,
    CmdPath,
    RegeditedPath,
    VarEdit,
    PasteBufferSelect,
    PasteBufferEdit,
    ZoneSelect,
    ZoneEdit,
    PointEdit
};

struct RegistryHubState {
    RegistryHubStage stage = RegistryHubStage::Command;
    int activeVar = -1;
    int activePasteSlot = -1;
    int activeZoneSlot = -1;
    int activePoint = -1;
    int visibleLines = 1;
    POINT cancelOnMouseMoveFrom = {0, 0};
    DWORD openedTickMs = 0;
    std::wstring status;
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
HWND        g_hwndViewTogglesDlg = nullptr;
HWND        g_hwndPasteBuffersDlg = nullptr;
HWND        g_hwndRegistryHubDlg = nullptr;

// Visibility
bool        g_crosshairVisible = true;
bool        g_coordPanelVisible = true;
bool        g_keyDisplayVisible = true;
bool        g_gridVisible = false;
bool        g_coordinatesOnlyMode = false;
bool        g_running = true;

// GDI / GDI+
HFONT       g_hFontCoord = nullptr;
HFONT       g_hFontBold = nullptr;
HFONT       g_hFontMono = nullptr;
HBRUSH      g_hbrColorkey = nullptr;
HBRUSH      g_hbrHudBg = nullptr;
HBRUSH      g_hbrHudEdit = nullptr;
std::wstring g_pendingCircleInputOverride;
GdiplusStartupInput  g_gdiInput;
GdiplusStartupOutput g_gdiOutput;
ULONG_PTR   g_gdiToken = 0;

// Key display GDI+ resources
HANDLE      g_hFontResource = nullptr;   // AddFontMemResourceEx handle
HFONT       g_hPromptFont = nullptr;
HFONT       g_hPromptFontSmall = nullptr;
PrivateFontCollection* g_promptFontCollection = nullptr;

// Key display DIB
HBITMAP     g_hKeyDib = nullptr;
HDC         g_hKeyDibDC = nullptr;
void*       g_pKeyDibBits = nullptr;
int         g_keyDibW = 0;
int         g_keyDibH = 0;

CirclePreview g_circlePreview;
CirclePlacementState g_circlePlacement;
SaveCursorState g_saveCursorState;
RecordKeysState g_recordKeysState;
ClickActionState g_clickActionState;
ViewToggleState g_viewToggleState;
PasteBufferState g_pasteBufferState;
std::wstring g_pasteBuffers[4];
ZoneBuffer g_zoneBuffers[4];
RegistryHubState g_registryHubState;
std::wstring g_registryScript;
std::wstring g_registryVars[4];
std::wstring g_registryPowerShellPath;
std::wstring g_registryCmdPath = L"cmd";
std::wstring g_regeditedExePath;
std::wstring g_registryFilePath;
std::wstring g_registryExportPath;
std::wstring g_registryImportPath;
int g_lastTasketTaskNumber = 0;
std::wstring g_lastTasketTaskState;
std::wstring g_lastTasketTaskMessage;
bool g_registryPointSet[REGISTRY_POINT_COUNT] = {};
POINT g_registryPoints[REGISTRY_POINT_COUNT] = {};

// Key tracking
std::vector<std::string> g_activeCombo;
std::mutex  g_comboMutex;
KeyHistoryEntry g_keyHistory[KEY_MAX_HISTORY];
int         g_keyHistoryCount = 0;
int         g_keyHistoryHead = 0;
std::string g_lastHistorySignature;
DWORD       g_lastHistoryTick = 0;
std::mutex  g_historyMutex;
bool        g_keyComboFinalized = true;
bool        g_polledKeyDown[256] = {};

// Recording (existing)
bool        g_recordingKeys = false;
bool        g_waitingForKey = false;
std::vector<std::vector<std::string>> g_recordedSteps;
std::vector<std::string> g_currentStepKeys;
std::vector<std::vector<std::string>> g_manualSteps;
std::vector<std::string> g_recordExtraActions;
HHOOK       g_hKeyboardHook = nullptr;
HWND        g_hwndRecordKeysDlg = nullptr;
HWND        g_hwndCircleDlg = nullptr;
HWND        g_hwndClickDlg = nullptr;
HWND        g_hwndRegistryHubEdit = nullptr;
std::mutex  g_keyMutex;

// Screen
int         g_screenX = 0, g_screenY = 0;
int         g_screenW = 0, g_screenH = 0;
int         g_updateMs = DEFAULT_UPDATE_MS;
int         g_displayRefreshHz = 60;
bool        g_timerResolutionActive = false;
double      g_uiScale = 1.0;

// Shared dialog data
POINT       g_savedCursorPos = {0, 0};
std::wstring g_saveCursorDefaultName;

// Tray
#define TRAY_ICON_ID 1001

// =============================================================================
// PROMPTFONT GLYPH MAPPING
// =============================================================================

static std::wstring CodepointToWString(int codepoint) {
    if (codepoint <= 0) return L"";
    if (codepoint <= 0xFFFF) return std::wstring(1, (wchar_t)codepoint);

    codepoint -= 0x10000;
    wchar_t hi = (wchar_t)(0xD800 + (codepoint >> 10));
    wchar_t lo = (wchar_t)(0xDC00 + (codepoint & 0x3FF));
    std::wstring out;
    out += hi;
    out += lo;
    return out;
}

// Maps Tasket key names to Shinmera PromptFont codepoints from res/promptfont.h.
static int PromptGlyphCodepointForKey(const std::string& key) {
    if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
        return 0x0FF21 + (key[0] - 'A'); // PF_KEYBOARD_A..Z
    }
    if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
        return 0x0FF10 + (key[0] - '0'); // PF_KEYBOARD_0..9
    }

    static const struct { const char* name; int codepoint; } map[] = {
        {"CONTROL_LEFT",   0x0244D}, // PF_KEYBOARD_CONTROL_L
        {"CONTROL_RIGHT",  0x0244E}, // PF_KEYBOARD_CONTROL_R
        {"SHIFT_LEFT",     0x0244F}, // PF_KEYBOARD_SHIFT_L
        {"SHIFT_RIGHT",    0x02450}, // PF_KEYBOARD_SHIFT_R
        {"ALT_LEFT",       0x0244B}, // PF_KEYBOARD_ALT_L
        {"ALT_GR",         0x0244A}, // PF_KEYBOARD_ALT_GR
        {"WINDOWS",        0x0242A}, // PF_KEYBOARD_SUPER
        {"FN",             0x02426}, // PF_KEYBOARD_FN
        {"LEFT_MOUSE",     0x0278A}, // PF_MOUSE_1
        {"RIGHT_MOUSE",    0x0278B}, // PF_MOUSE_2
        {"MIDDLE_MOUSE",   0x0278C}, // PF_MOUSE_3
        {"XBUTTON_1",      0x0278D}, // PF_MOUSE_4
        {"XBUTTON_2",      0x0278E}, // PF_MOUSE_5
        {"UP_ARROW",       0x023F6}, // PF_KEYBOARD_UP
        {"DOWN_ARROW",     0x023F7}, // PF_KEYBOARD_DOWN
        {"LEFT_ARROW",     0x023F4}, // PF_KEYBOARD_LEFT
        {"RIGHT_ARROW",    0x023F5}, // PF_KEYBOARD_RIGHT
        {"TAB",            0x0242B}, // PF_KEYBOARD_TAB
        {"CAPS_LOCK",      0x0242C}, // PF_KEYBOARD_CAPS
        {"BACKSPACE",      0x0242D}, // PF_KEYBOARD_BACKSPACE
        {"ENTER",          0x0242E}, // PF_KEYBOARD_ENTER
        {"ESCAPE",         0x0242F}, // PF_KEYBOARD_ESCAPE
        {"PRINT_SCREEN",   0x02430}, // PF_KEYBOARD_PRINT_SCREEN
        {"PRINT",          0x02430}, // PF_KEYBOARD_PRINT_SCREEN
        {"SCROLL_LOCK",    0x02431}, // PF_KEYBOARD_SCROLL_LOCK
        {"NUM_LOCK",       0x02433}, // PF_KEYBOARD_NUM_LOCK
        {"INSERT",         0x02434}, // PF_KEYBOARD_INSERT
        {"HOME",           0x02435}, // PF_KEYBOARD_HOME
        {"PAGE_UP",        0x02436}, // PF_KEYBOARD_PAGE_UP
        {"DELETE",         0x02437}, // PF_KEYBOARD_DELETE
        {"END",            0x02438}, // PF_KEYBOARD_END
        {"PAGE_DOWN",      0x02439}, // PF_KEYBOARD_PAGE_DOWN
        {"SPACEBAR",       0x0243A}, // PF_KEYBOARD_SPACE
        {"F1",             0x02460}, // PF_KEYBOARD_F1
        {"F2",             0x02461},
        {"F3",             0x02462},
        {"F4",             0x02463},
        {"F5",             0x02464},
        {"F6",             0x02465},
        {"F7",             0x02466},
        {"F8",             0x02467},
        {"F9",             0x02468},
        {"F10",            0x02469},
        {"F11",            0x0246A},
        {"F12",            0x0246B},
        {"NUMPAD_1",       0x02474}, // PF_KEYBOARD_NUMPAD_1
        {"NUMPAD_2",       0x02475},
        {"NUMPAD_3",       0x02476},
        {"NUMPAD_4",       0x02477},
        {"NUMPAD_5",       0x02478},
        {"NUMPAD_6",       0x02479},
        {"NUMPAD_7",       0x0247A},
        {"NUMPAD_8",       0x0247B},
        {"NUMPAD_9",       0x0247C},
        {"NUMPAD_0",       0x0247D},
        {".",              0x0247E}, // PF_KEYBOARD_NUMPAD_DOT
        {"NUMPAD_ENTER",   0x0247F},
        {"-",              0x02480}, // PF_KEYBOARD_NUMPAD_MINUS
        {"+",              0x02481}, // PF_KEYBOARD_NUMPAD_PLUS
        {"/",              0x02482}, // PF_KEYBOARD_NUMPAD_SLASH
        {"*",              0x02483}, // PF_KEYBOARD_NUMPAD_STAR
        {",",              0x0FF0C}, // PF_KEYBOARD_COMMA
        {nullptr, 0}
    };
    for (int i = 0; map[i].name; i++) {
        if (key == map[i].name) return map[i].codepoint;
    }
    return 0;
}

static std::wstring KeyToGlyph(const std::string& key) {
    int codepoint = PromptGlyphCodepointForKey(key);
    if (codepoint) return CodepointToWString(codepoint);
    // Fallback: return the key name as text
    return Utf8ToWString(key);
}

std::unique_ptr<Font> CreateKeyTextFont(const std::string& keyName, REAL size) {
    if (PromptGlyphCodepointForKey(keyName)) {
        return CreatePromptFont(size);
    }
    return std::make_unique<Font>(L"Segoe UI", size, FontStyleBold, UnitPixel);
}

void PushKeyHistory(const std::vector<std::string>& keys) {
    if (keys.empty()) return;

    std::lock_guard<std::mutex> lock(g_historyMutex);
    std::string signature;
    for (const auto& key : keys) {
        if (!signature.empty()) signature += "+";
        signature += key;
    }

    DWORD now = GetTickCount();
    if (signature == g_lastHistorySignature && now - g_lastHistoryTick < 80) {
        return;
    }
    g_lastHistorySignature = signature;
    g_lastHistoryTick = now;

    KeyHistoryEntry entry;
    entry.keys = keys;
    entry.tickMs = now;

    g_keyHistory[g_keyHistoryHead] = entry;
    g_keyHistoryHead = (g_keyHistoryHead + 1) % KEY_MAX_HISTORY;
    if (g_keyHistoryCount < KEY_MAX_HISTORY) g_keyHistoryCount++;
}

static std::wstring FormatKeyNames(const std::vector<std::string>& keys) {
    std::wstring out;
    for (size_t i = 0; i < keys.size(); i++) {
        if (i > 0) out += L"+";
        out += Utf8ToWString(keys[i]);
    }
    return out;
}

std::wstring GetKeyHudStatusText() {
    if (!g_keyDisplayVisible) return L"Key HUD: off";

    std::lock_guard<std::mutex> lock(g_historyMutex);
    if (g_keyHistoryCount == 0) return L"Key HUD: on, waiting";

    int idx = (g_keyHistoryHead - 1 + KEY_MAX_HISTORY) % KEY_MAX_HISTORY;
    DWORD age = GetTickCount() - g_keyHistory[idx].tickMs;
    if (age > (DWORD)KEY_FADE_MS) return L"Key HUD: on, waiting";

    return L"Key HUD: " + FormatKeyNames(g_keyHistory[idx].keys);
}

void SeedKeyHudStartupPulse() {
    PushKeyHistory({"HUD", "ON"});
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

void TrackDisplayKeyDown(UINT vkCode, bool extended) {
    if (IsModifierKey(vkCode)) {
        TrackKeyDown(vkCode, extended);
    } else {
        TrackKeyDown(vkCode, extended);
        FinalizeKeyCombo(true);
    }
}

void PollKeyboardState() {
    static const struct { UINT vk; bool extended; } keys[] = {
        {'A', false}, {'B', false}, {'C', false}, {'D', false}, {'E', false},
        {'F', false}, {'G', false}, {'H', false}, {'I', false}, {'J', false},
        {'K', false}, {'L', false}, {'M', false}, {'N', false}, {'O', false},
        {'P', false}, {'Q', false}, {'R', false}, {'S', false}, {'T', false},
        {'U', false}, {'V', false}, {'W', false}, {'X', false}, {'Y', false},
        {'Z', false},
        {'0', false}, {'1', false}, {'2', false}, {'3', false}, {'4', false},
        {'5', false}, {'6', false}, {'7', false}, {'8', false}, {'9', false},
        {VK_F1, false}, {VK_F2, false}, {VK_F3, false}, {VK_F4, false},
        {VK_F5, false}, {VK_F6, false}, {VK_F7, false}, {VK_F8, false},
        {VK_F9, false}, {VK_F10, false}, {VK_F11, false}, {VK_F12, false},
        {VK_LCONTROL, false}, {VK_RCONTROL, true},
        {VK_LSHIFT, false}, {VK_RSHIFT, true},
        {VK_LMENU, false}, {VK_RMENU, true},
        {VK_LWIN, false}, {VK_RWIN, true},
        {VK_SPACE, false}, {VK_RETURN, false}, {VK_BACK, false},
        {VK_DELETE, true}, {VK_INSERT, true}, {VK_ESCAPE, false},
        {VK_TAB, false}, {VK_CAPITAL, false}, {VK_SCROLL, false},
        {VK_NUMLOCK, false}, {VK_SNAPSHOT, false}, {VK_PRINT, false},
        {VK_UP, true}, {VK_DOWN, true}, {VK_LEFT, true}, {VK_RIGHT, true},
        {VK_HOME, true}, {VK_END, true}, {VK_PRIOR, true}, {VK_NEXT, true},
        {VK_NUMPAD0, false}, {VK_NUMPAD1, false}, {VK_NUMPAD2, false},
        {VK_NUMPAD3, false}, {VK_NUMPAD4, false}, {VK_NUMPAD5, false},
        {VK_NUMPAD6, false}, {VK_NUMPAD7, false}, {VK_NUMPAD8, false},
        {VK_NUMPAD9, false}, {VK_ADD, false}, {VK_SUBTRACT, false},
        {VK_MULTIPLY, false}, {VK_DIVIDE, true}, {VK_DECIMAL, false},
        {VK_VOLUME_UP, false}, {VK_VOLUME_DOWN, false}, {VK_VOLUME_MUTE, false},
        {VK_MEDIA_PLAY_PAUSE, false}, {VK_MEDIA_STOP, false},
        {VK_MEDIA_NEXT_TRACK, false}, {VK_MEDIA_PREV_TRACK, false},
        {0, false}
    };

    bool dialogHasFocus = (g_hwndSaveDlg && GetForegroundWindow() == g_hwndSaveDlg) ||
                          (g_hwndRecordDlg && GetForegroundWindow() == g_hwndRecordDlg) ||
                          (g_hwndCircleDlg && GetForegroundWindow() == g_hwndCircleDlg) ||
                          (g_hwndClickDlg && GetForegroundWindow() == g_hwndClickDlg) ||
                          (g_hwndViewTogglesDlg && GetForegroundWindow() == g_hwndViewTogglesDlg) ||
                          (g_hwndPasteBuffersDlg && GetForegroundWindow() == g_hwndPasteBuffersDlg);

    for (int i = 0; keys[i].vk; i++) {
        UINT vk = keys[i].vk;
        bool down = (GetAsyncKeyState((int)vk) & 0x8000) != 0;
        if (down && !g_polledKeyDown[vk]) {
            if (!dialogHasFocus) TrackDisplayKeyDown(vk, keys[i].extended);
        } else if (!down && g_polledKeyDown[vk]) {
            TrackKeyUp(vk, keys[i].extended);
        }
        g_polledKeyDown[vk] = down;
    }
}

static void TrackKeyDown(UINT vkCode, bool extended) {
    std::lock_guard<std::mutex> lock(g_comboMutex);

    // Don't track if it's our own hotkey being processed
    if ((vkCode >= '0' && vkCode <= '8')) {
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

static void TrackKeyUp(UINT vkCode, bool extended) {
    std::vector<std::string> modifierOnlyCombo;

    std::string name = VkCodeToTasketName(vkCode, extended);
    if (name.empty()) return;

    {
        std::lock_guard<std::mutex> lock(g_comboMutex);

        if (!g_keyComboFinalized && g_activeCombo.size() == 1 && g_activeCombo[0] == name) {
            modifierOnlyCombo = g_activeCombo;
            g_keyComboFinalized = true;
        }

        // Remove from active combo
        auto it = g_activeCombo.begin();
        while (it != g_activeCombo.end()) {
            if (*it == name) {
                it = g_activeCombo.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!modifierOnlyCombo.empty()) {
        PushKeyHistory(modifierOnlyCombo);
    }
}

static void FinalizeKeyCombo(bool clearActive) {
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(g_comboMutex);
        if (g_activeCombo.empty()) return;
        keys = g_activeCombo;
        g_keyComboFinalized = true;
        if (clearActive) g_activeCombo.clear();
    }

    PushKeyHistory(keys);
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
                                  (g_hwndRecordDlg && GetForegroundWindow() == g_hwndRecordDlg) ||
                                  (g_hwndCircleDlg && GetForegroundWindow() == g_hwndCircleDlg) ||
                                  (g_hwndClickDlg && GetForegroundWindow() == g_hwndClickDlg) ||
                                  (g_hwndViewTogglesDlg && GetForegroundWindow() == g_hwndViewTogglesDlg) ||
                                  (g_hwndPasteBuffersDlg && GetForegroundWindow() == g_hwndPasteBuffersDlg);

            if (!dialogHasFocus) {
                TrackDisplayKeyDown(vk, ext);
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
            TrackKeyUp(vk, ext);
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

std::wstring GetMacrohelpDataDir() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        std::wstring dir = std::wstring(path) + L"\\Macrohelp";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring dir = std::wstring(buf) + L"\\Macrohelp";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    CreateDirectoryW(L"Macrohelp", nullptr);
    return L"Macrohelp";
}

std::wstring GetMacrohelpStatePath() {
    return GetMacrohelpDataDir() + L"\\state.json";
}

std::wstring GetMacrohelpCirclePreviewPath() {
    return GetMacrohelpDataDir() + L"\\circle_preview.json";
}

std::wstring GetMacrohelpCommandPath() {
    return GetMacrohelpDataDir() + L"\\command.txt";
}

static std::wstring GetMacrohelpRegistryHubStatePath() {
    return GetMacrohelpDataDir() + L"\\registry_hub_state.txt";
}

static std::wstring GetMacrohelpRegistryHubExportPath() {
    return GetMacrohelpDataDir() + L"\\registry_hub_environment.txt";
}

static std::wstring DefaultRegistryHubEnvironmentPath() {
    return L"%USERPROFILE%\\Desktop\\temps\\macrohelp-registry-hub-environment.txt";
}

static std::wstring DefaultDesktopTempsDir() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) return std::wstring(path) + L"\\Desktop\\temps";
    return L"temps";
}

static std::string LocalTimestampForFileName() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char stamp[64] = {};
    sprintf_s(stamp, "%04u%02u%02u-%02u%02u%02u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return stamp;
}

static std::wstring ExpandMacrohelpPath(std::wstring value) {
    value = TrimWide(value);
    if (value.empty()) return value;
    std::replace(value.begin(), value.end(), L'/', L'\\');

    auto envValue = [](const wchar_t* name) -> std::wstring {
        wchar_t buf[32768] = {};
        DWORD len = GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        if (len == 0 || len >= (DWORD)(sizeof(buf) / sizeof(buf[0]))) return L"";
        return std::wstring(buf, len);
    };

    std::wstring userProfile = envValue(L"USERPROFILE");
    std::wstring localAppData = envValue(L"LOCALAPPDATA");
    auto replaceAll = [](std::wstring& text, const std::wstring& from, const std::wstring& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::wstring::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    if (!userProfile.empty() && value.size() >= 1 && value[0] == L'~') {
        value = userProfile + value.substr(1);
    }
    if (!userProfile.empty()) {
        replaceAll(value, L"$USERPROFILE", userProfile);
        replaceAll(value, L"$env:USERPROFILE", userProfile);
        replaceAll(value, L"${USERPROFILE}", userProfile);
        replaceAll(value, L"%USERPROFILE%", userProfile);
    }
    if (!localAppData.empty()) {
        replaceAll(value, L"$LOCALAPPDATA", localAppData);
        replaceAll(value, L"$env:LOCALAPPDATA", localAppData);
        replaceAll(value, L"${LOCALAPPDATA}", localAppData);
        replaceAll(value, L"%LOCALAPPDATA%", localAppData);
    }

    wchar_t expanded[32768] = {};
    DWORD len = ExpandEnvironmentStringsW(value.c_str(), expanded, (DWORD)(sizeof(expanded) / sizeof(expanded[0])));
    if (len > 0 && len < (DWORD)(sizeof(expanded) / sizeof(expanded[0]))) {
        value = expanded;
    }
    return value;
}

static std::wstring GetTasketSavedTasksDir() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::wstring(path) + L"\\Tasket++\\saved_tasks";
    }
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(buf) + L"\\Tasket++\\saved_tasks";
    }
    return L"";
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
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default: r += c; break;
        }
    }
    return r;
}

static std::string JsonStringArray(const std::vector<std::string>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out += ",";
        out += "\"" + EscapeJsonString(values[i]) + "\"";
    }
    out += "]";
    return out;
}

static std::string ReadUtf8File(const std::wstring& path) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return "";

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        fclose(fp);
        return "";
    }

    std::string data((size_t)size, '\0');
    size_t read = fread(&data[0], 1, (size_t)size, fp);
    fclose(fp);
    data.resize(read);
    return data;
}

static std::string ReadTasketSavedTaskTemplate(const std::wstring& fileName) {
    std::wstring dir = GetTasketSavedTasksDir();
    if (dir.empty()) return "";
    return ReadUtf8File(dir + L"\\" + fileName);
}

static bool ReplaceCursorMovementPointAt(std::string& json, size_t innerOpen, POINT point, size_t* nextFrom) {
    struct Range { size_t start = 0; size_t end = 0; };
    Range nums[4];
    size_t pos = innerOpen + 1;
    for (int i = 0; i < 4; ++i) {
        while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
        if (pos >= json.size()) return false;

        size_t start = pos;
        if (json[pos] == '-' || json[pos] == '+') pos++;
        bool sawDigit = false;
        while (pos < json.size() && isdigit((unsigned char)json[pos])) {
            sawDigit = true;
            pos++;
        }
        if (!sawDigit) return false;

        nums[i] = {start, pos};
        while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
        if (i < 3) {
            if (pos >= json.size() || json[pos] != ',') return false;
            pos++;
        }
    }

    std::string x = std::to_string(point.x);
    std::string y = std::to_string(point.y);
    json.replace(nums[3].start, nums[3].end - nums[3].start, y);
    json.replace(nums[2].start, nums[2].end - nums[2].start, x);
    if (nextFrom) *nextFrom = nums[3].start + y.size();
    return true;
}

static bool ReplaceNextCursorMovementPoint(std::string& json, size_t from, POINT point, size_t* nextFrom) {
    size_t localOpen = json.find('[', from);
    size_t mapPos = json.find("\"cursormovsmap\"", from);

    if (localOpen != std::string::npos && (mapPos == std::string::npos || localOpen < mapPos)) {
        if (ReplaceCursorMovementPointAt(json, localOpen, point, nextFrom)) {
            return true;
        }
    }

    if (mapPos == std::string::npos) return false;

    size_t outerOpen = json.find('[', mapPos);
    if (outerOpen == std::string::npos) return false;
    size_t innerOpen = json.find('[', outerOpen + 1);
    if (innerOpen == std::string::npos) return false;

    return ReplaceCursorMovementPointAt(json, innerOpen, point, nextFrom);
}

static std::string TrimAscii(std::string value) {
    size_t start = 0;
    while (start < value.size() && isspace((unsigned char)value[start])) start++;
    size_t end = value.size();
    while (end > start && isspace((unsigned char)value[end - 1])) end--;
    return value.substr(start, end - start);
}

static std::string LowerAscii(std::string value) {
    for (char& ch : value) {
        ch = (char)std::tolower((unsigned char)ch);
    }
    return value;
}

static bool WriteUtf8File(const std::wstring& path, const std::string& data) {
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0) {
        std::wstring parent = path.substr(0, slash);
        CreateDirectoryW(parent.c_str(), nullptr);
    }
    std::wstring tmp = path + L".tmp";
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, tmp.c_str(), L"wb") != 0 || !fp) return false;
    if (!data.empty()) fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);
    DeleteFileW(path.c_str());
    return MoveFileW(tmp.c_str(), path.c_str()) != 0;
}

static std::vector<std::string> SplitScriptArgs(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    bool escaped = false;
    for (char ch : text) {
        if (escaped) {
            cur.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && inQuote) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && (isspace((unsigned char)ch) || ch == ',')) {
            std::string trimmed = TrimAscii(cur);
            if (!trimmed.empty()) out.push_back(trimmed);
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    std::string trimmed = TrimAscii(cur);
    if (!trimmed.empty()) out.push_back(trimmed);
    return out;
}

static std::string SectionTextAfterMarker(const std::string& data, const std::string& marker) {
    size_t pos = data.find(marker);
    if (pos == std::string::npos) return "";
    pos += marker.size();
    if (pos < data.size() && data[pos] == '\r') pos++;
    if (pos < data.size() && data[pos] == '\n') pos++;
    size_t next = data.find("\n---", pos);
    if (next == std::string::npos) next = data.size();
    std::string out = data.substr(pos, next - pos);
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
    return out;
}

static std::wstring QuoteProcessArg(const std::wstring& value) {
    if (value.empty()) return L"\"\"";
    bool needsQuote = false;
    for (wchar_t ch : value) {
        if (iswspace(ch) || ch == L'"') {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) return value;
    std::wstring out = L"\"";
    size_t slashCount = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashCount;
            continue;
        }
        if (ch == L'"') {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(ch);
        } else {
            out.append(slashCount, L'\\');
            out.push_back(ch);
        }
        slashCount = 0;
    }
    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static bool FindMatchingJsonBracket(const std::string& json, size_t openPos, char openCh, char closeCh, size_t* closePos) {
    if (openPos >= json.size() || json[openPos] != openCh) return false;

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = openPos; i < json.size(); ++i) {
        char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == openCh) {
            depth++;
        } else if (c == closeCh) {
            depth--;
            if (depth == 0) {
                if (closePos) *closePos = i;
                return true;
            }
        }
    }
    return false;
}

static bool ExtractTasketActionsBlock(const std::string& taskJson, std::string* actions) {
    size_t keyPos = taskJson.find("\"actions\"");
    if (keyPos == std::string::npos) return false;
    size_t arrayOpen = taskJson.find('[', keyPos);
    if (arrayOpen == std::string::npos) return false;

    size_t arrayClose = 0;
    if (!FindMatchingJsonBracket(taskJson, arrayOpen, '[', ']', &arrayClose)) return false;

    if (actions) {
        *actions = TrimAscii(taskJson.substr(arrayOpen + 1, arrayClose - arrayOpen - 1));
    }
    return true;
}

static std::string JoinGeneratedActions(const std::vector<std::string>& actionJsons) {
    std::string out;
    for (size_t i = 0; i < actionJsons.size(); ++i) {
        if (i > 0) out += "\n";
        out += StripTrailingActionComma(actionJsons[i]);
        if (i + 1 < actionJsons.size()) out += ",";
    }
    return out;
}

static bool BuildNativeTemplateActions(const std::wstring& templateFileName, std::string* actions) {
    std::vector<std::string> generated;

    if (templateFileName == L"Open snipping tool example.scht") {
        generated = {
            BuildKeysSequenceActionJson("WinShiftS", {{"WINDOWS", "SHIFT_LEFT", "S"}}, 100, 100),
            BuildWaitActionJson(2.0)
        };
    } else if (templateFileName == L"Open snipping text example.scht") {
        generated = {
            BuildKeysSequenceActionJson("WinShiftY", {{"WINDOWS", "SHIFT_LEFT", "Y"}}, 100, 100),
            BuildWaitActionJson(2.0)
        };
    } else if (templateFileName == L"Open snipping text example singleline.scht") {
        generated = {
            BuildKeysSequenceActionJson("WinShiftY", {{"WINDOWS", "SHIFT_LEFT", "Y"}}, 100, 100),
            BuildWaitActionJson(2.0),
            BuildKeysSequenceActionJson("PressS", {{"S"}}, 100, 100),
            BuildWaitActionJson(0.8)
        };
    } else if (templateFileName == L"Ctrl C example.scht") {
        generated = {
            BuildKeysSequenceActionJson("CtrlCforCopy", {{"CONTROL_LEFT", "C"}}, 100, 100),
            BuildWaitActionJson(1.0)
        };
    } else {
        return false;
    }

    if (actions) *actions = JoinGeneratedActions(generated);
    return true;
}

static bool LoadTemplateActions(const std::wstring& templateFileName, std::string* actions, std::wstring* status) {
    std::string body = ReadTasketSavedTaskTemplate(templateFileName);
    if (body.empty()) {
        if (BuildNativeTemplateActions(templateFileName, actions)) {
            if (status) *status = L"Using built-in Macrohelp template actions for " + templateFileName;
            return true;
        }
        if (status) *status = L"Missing or empty Tasket example template: " + templateFileName;
        return false;
    }
    if (!ExtractTasketActionsBlock(body, actions)) {
        if (BuildNativeTemplateActions(templateFileName, actions)) {
            if (status) *status = L"Using built-in Macrohelp template actions for unreadable " + templateFileName;
            return true;
        }
        if (status) *status = L"Could not read actions from Tasket example template: " + templateFileName;
        return false;
    }
    return true;
}

static bool LoadZoneActions(POINT start, POINT end, std::string* actions, std::wstring* status) {
    std::vector<std::string> generated = {
        BuildCursorMovementActionJson(
            "CursorMoveExampleTopLeftBox",
            {{0, 200, start.x, start.y}}),
        BuildWaitActionJson(0.2),
        BuildCursorMovementActionJson(
            "ClickDragZoneFlow",
            {{0, 1500, end.x, end.y}},
            {"LEFT_MOUSE"}),
        BuildWaitActionJson(2.0)
    };
    if (actions) *actions = JoinGeneratedActions(generated);
    if (status) *status = L"Using canonical Macrohelp zone drag actions.";
    return true;
}

static bool LoadCursorMoveActions(POINT target, std::string* actions, std::wstring* status) {
    std::string body = ReadTasketSavedTaskTemplate(L"Move cursor example for codex.scht");
    if (body.empty()) {
        std::vector<std::string> generated = {
            BuildCursorMovementActionJson(
                "MacrohelpCursorMove",
                {{0, 200, target.x, target.y}})
        };
        if (actions) *actions = JoinGeneratedActions(generated);
        if (status) *status = L"Using built-in Macrohelp cursor move actions.";
        return true;
    }

    if (!ReplaceNextCursorMovementPoint(body, 0, target, nullptr)) {
        std::vector<std::string> generated = {
            BuildCursorMovementActionJson(
                "MacrohelpCursorMove",
                {{0, 200, target.x, target.y}})
        };
        if (actions) *actions = JoinGeneratedActions(generated);
        if (status) *status = L"Using built-in Macrohelp cursor move actions because template coordinates could not be patched.";
        return true;
    }

    if (!ExtractTasketActionsBlock(body, actions)) {
        std::vector<std::string> generated = {
            BuildCursorMovementActionJson(
                "MacrohelpCursorMove",
                {{0, 200, target.x, target.y}})
        };
        if (actions) *actions = JoinGeneratedActions(generated);
        if (status) *status = L"Using built-in Macrohelp cursor move actions because template actions could not be read.";
        return true;
    }
    return true;
}

static bool JsonNumberValue(const std::string& json, const std::string& key, double& out) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
    char* end = nullptr;
    double value = strtod(json.c_str() + pos, &end);
    if (end == json.c_str() + pos) return false;
    out = value;
    return true;
}

static bool JsonBoolValue(const std::string& json, const std::string& key, bool fallback) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    pos++;
    while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

static std::string JsonStringValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;

    std::string out;
    bool escaped = false;
    for (; pos < json.size(); pos++) {
        char c = json[pos];
        if (escaped) {
            switch (c) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                default: out += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') break;
        out += c;
    }
    return out;
}

static bool SameFileTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

void HideCirclePreview() {
    g_circlePreview.enabled = false;
    g_circlePreview.hasMouseAnchor = false;
    g_circlePreview.zoneEnabled = false;
    g_circlePreview.label.clear();
    g_circlePreview.angleLabel.clear();
}

static void SetCirclePreview(
    POINT origin,
    POINT target,
    double radius,
    const std::wstring& label,
    const std::wstring& angleLabel,
    DWORD ttlMs,
    bool showTarget = true
) {
    g_circlePreview.enabled = true;
    g_circlePreview.originX = origin.x;
    g_circlePreview.originY = origin.y;
    g_circlePreview.targetX = target.x;
    g_circlePreview.targetY = target.y;
    g_circlePreview.radius = (int)std::lround(std::fabs(radius));
    g_circlePreview.showQuadrants = true;
    g_circlePreview.showTarget = showTarget;
    g_circlePreview.zoneEnabled = false;
    g_circlePreview.expiresTickMs = ttlMs == 0 ? 0 : GetTickCount() + ttlMs;
    GetCursorPos(&g_circlePreview.mouseAnchor);
    g_circlePreview.hasMouseAnchor = true;
    g_circlePreview.label = label;
    g_circlePreview.angleLabel = angleLabel;
}

static void SetZonePreview(POINT start, POINT end) {
    g_circlePreview.enabled = true;
    g_circlePreview.zoneEnabled = true;
    g_circlePreview.zoneStart = start;
    g_circlePreview.zoneEnd = end;
    g_circlePreview.expiresTickMs = 0;
    GetCursorPos(&g_circlePreview.mouseAnchor);
    g_circlePreview.hasMouseAnchor = true;
}

void RefreshCirclePreviewFromFile() {
    if (g_circlePlacement.active) return;

    std::wstring path = GetMacrohelpCirclePreviewPath();
    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs)) {
        if (!g_circlePlacement.active) g_circlePreview.enabled = false;
        return;
    }

    DWORD now = GetTickCount();
    if (g_circlePreview.enabled && g_circlePreview.expiresTickMs != 0 &&
        (LONG)(now - g_circlePreview.expiresTickMs) >= 0) {
        g_circlePreview.enabled = false;
    }

    if (SameFileTime(g_circlePreview.lastWriteTime, attrs.ftLastWriteTime)) return;
    g_circlePreview.lastWriteTime = attrs.ftLastWriteTime;

    std::string json = ReadUtf8File(path);
    double ox = 0, oy = 0, tx = 0, ty = 0, radius = 0, ttl = 6000;
    if (!JsonNumberValue(json, "origin_x", ox) ||
        !JsonNumberValue(json, "origin_y", oy) ||
        !JsonNumberValue(json, "target_x", tx) ||
        !JsonNumberValue(json, "target_y", ty) ||
        !JsonNumberValue(json, "radius", radius)) {
        g_circlePreview.enabled = false;
        return;
    }
    JsonNumberValue(json, "ttl_ms", ttl);
    if (ttl < 250.0) ttl = 250.0;

    POINT origin = {(LONG)std::lround(ox), (LONG)std::lround(oy)};
    POINT target = {(LONG)std::lround(tx), (LONG)std::lround(ty)};
    SetCirclePreview(
        origin,
        target,
        radius,
        Utf8ToWString(JsonStringValue(json, "label")),
        Utf8ToWString(JsonStringValue(json, "angle_label")),
        (DWORD)ttl,
        JsonBoolValue(json, "show_target", true)
    );
    g_circlePreview.showQuadrants = JsonBoolValue(json, "show_quadrants", true);
}

void ProcessBackendCommandFile() {
    std::wstring path = GetMacrohelpCommandPath();
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return;

    std::string commandText = ReadUtf8File(path);
    DeleteFileW(path.c_str());
    std::wstring command = TrimWide(Utf8ToWString(commandText));
    std::string compact = CompactLower(command);
    if (compact.empty()) return;

    auto readPayload = [&](const std::wstring& prefix, std::wstring& payload) -> bool {
        if (command.size() < prefix.size()) return false;
        std::wstring head = command.substr(0, prefix.size());
        std::transform(head.begin(), head.end(), head.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        std::wstring lowerPrefix = prefix;
        std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        if (head != lowerPrefix) return false;
        payload = TrimWide(command.substr(prefix.size()));
        return true;
    };

    std::wstring payload;
    if (readPayload(L"input:", payload) ||
        readPayload(L"circle_input:", payload) ||
        readPayload(L"type:", payload)) {
        StartCirclePlacement();
        if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) {
            CircleStage stageBeforeInput = g_circlePlacement.stage;
            SetDlgItemTextW(g_hwndCircleDlg, IDC_EDIT_CIRCLE_VALUE, payload.c_str());
            SetFocus(GetDlgItem(g_hwndCircleDlg, IDC_EDIT_CIRCLE_VALUE));
            SetForegroundWindow(g_hwndCircleDlg);
            if ((stageBeforeInput == CircleStage::Distance ||
                 stageBeforeInput == CircleStage::Angle) &&
                g_circlePlacement.stage == stageBeforeInput) {
                g_pendingCircleInputOverride = payload;
                UpdatePreviewFromInput(g_hwndCircleDlg);
            } else {
                SetCircleStatus(L"Command input received. Press Enter to continue.");
            }
        }
        return;
    }

    if (compact == "circle" || compact == "circle_placement" || compact == "open_circle") {
        StartCirclePlacement();
    } else if (compact == "enter" || compact == "circle_enter") {
        if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) {
            PostMessage(g_hwndCircleDlg, WM_COMMAND, IDOK, 0);
        }
    } else if (compact == "circle_next" || compact == "circle_from_here" || compact == "next_circle") {
        if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) {
            PostMessage(g_hwndCircleDlg, WM_COMMAND, IDC_BTN_CIRCLE_NEXT_CIRCLE, 0);
        }
    } else if (compact == "circle_append" || compact == "append_coordinate") {
        if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) {
            PostMessage(g_hwndCircleDlg, WM_COMMAND, IDC_BTN_CIRCLE_APPEND_COORD, 0);
        }
    } else if (compact == "circle_move" || compact == "move_cursor_here") {
        if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) {
            PostMessage(g_hwndCircleDlg, WM_COMMAND, IDC_BTN_CIRCLE_MOVE_NOW, 0);
        }
    } else if (compact == "save" || compact == "save_cursor") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_SAVE_CURSOR, 0);
    } else if (compact == "record" || compact == "record_keys") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_RECORD_KEYS, 0);
    } else if (compact == "click_left" || compact == "left_click" || compact == "manual_left_click") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_LEFT, 0);
    } else if (compact == "click_right" || compact == "right_click" || compact == "manual_right_click") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_RIGHT, 0);
    } else if (compact == "click_middle" || compact == "middle_click" || compact == "manual_middle_click") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_MIDDLE, 0);
    } else if (compact == "stop_all" || compact == "stop_all_tasket" || compact == "panic" || compact == "kill_tasks") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_STOP_ALL_TASKET, 0);
    } else if (compact == "view" || compact == "view_toggles" || compact == "display" || compact == "grid") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_VIEW_TOGGLES, 0);
    } else if (compact == "paste" || compact == "paste_buffers" || compact == "paste_buffer" || compact == "buffer_paste") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_PASTE_BUFFERS, 0);
    } else if (compact == "hub" || compact == "registry" || compact == "registry_hub" || compact == "command_hub") {
        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_REGISTRY_HUB, 0);
    } else if (compact == "hide_preview" || compact == "clear_preview") {
        HideCirclePreview();
    }
}

static void HideCirclePreviewIfMouseMoved() {
    if (g_circlePlacement.active && g_circlePlacement.stage != CircleStage::Confirm) {
        POINT pt = {};
        GetCursorPos(&pt);
        DWORD now = GetTickCount();
        if (now - g_circlePlacement.openedTickMs < 700) return;
        int dx = std::abs(pt.x - g_circlePlacement.cancelOnMouseMoveFrom.x);
        int dy = std::abs(pt.y - g_circlePlacement.cancelOnMouseMoveFrom.y);
        if (dx > 3 || dy > 3) {
            ResetCirclePlacement(true);
        }
        return;
    }

    if (!g_circlePreview.enabled || !g_circlePreview.hasMouseAnchor) return;

    POINT pt = {};
    GetCursorPos(&pt);
    int dx = std::abs(pt.x - g_circlePreview.mouseAnchor.x);
    int dy = std::abs(pt.y - g_circlePreview.mouseAnchor.y);
    if (dx <= 3 && dy <= 3) return;

    HideCirclePreview();
    if (g_circlePlacement.active) {
        g_circlePlacement.cancelOnMouseMoveFrom = pt;
        if (g_circlePlacement.targetValid) {
            SetCircleStatus(L"Preview hidden after mouse movement. The computed target is still ready.");
        } else {
            SetCircleStatus(L"Preview hidden after mouse movement. Edit the value to draw it again.");
        }
    }
}

void WriteBackendState(bool force) {
    static DWORD lastWriteTick = 0;
    DWORD now = GetTickCount();
    if (!force && now - lastWriteTick < 100) return;
    lastWriteTick = now;

    POINT pt = {};
    GetCursorPos(&pt);

    std::vector<std::string> activeKeys;
    {
        std::lock_guard<std::mutex> lock(g_comboMutex);
        activeKeys = g_activeCombo;
    }

    std::vector<std::string> latestKeys;
    DWORD latestAge = 0;
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        if (g_keyHistoryCount > 0) {
            int idx = (g_keyHistoryHead - 1 + KEY_MAX_HISTORY) % KEY_MAX_HISTORY;
            latestKeys = g_keyHistory[idx].keys;
            latestAge = now - g_keyHistory[idx].tickMs;
        }
    }

    std::wstring statePath = GetMacrohelpStatePath();
    std::wstring tmpPath = statePath + L".tmp";
    std::ofstream f(tmpPath, std::ios::trunc);
    if (!f.is_open()) return;

    f << "{\n";
    f << "  \"schema\": \"macrohelp_state_v1\",\n";
    f << "  \"pid\": " << GetCurrentProcessId() << ",\n";
    f << "  \"tick_ms\": " << now << ",\n";
    f << "  \"state_path\": \"" << EscapeJsonString(WStringToUtf8(statePath)) << "\",\n";
    f << "  \"command_path\": \"" << EscapeJsonString(WStringToUtf8(GetMacrohelpCommandPath())) << "\",\n";
    f << "  \"cursor\": {\"x\": " << pt.x << ", \"y\": " << pt.y << "},\n";
    f << "  \"screen\": {\"x\": " << g_screenX << ", \"y\": " << g_screenY
      << ", \"w\": " << g_screenW << ", \"h\": " << g_screenH << "},\n";
    f << "  \"display\": {\"refresh_hz\": " << g_displayRefreshHz
      << ", \"update_ms\": " << g_updateMs
      << ", \"ui_scale\": " << std::fixed << std::setprecision(2) << g_uiScale << std::defaultfloat << "},\n";
    f << "  \"visible\": {\"crosshair\": " << (g_crosshairVisible ? "true" : "false")
      << ", \"coord_panel\": " << (g_coordPanelVisible ? "true" : "false")
      << ", \"key_display\": " << (g_keyDisplayVisible ? "true" : "false")
      << ", \"grid\": " << (g_gridVisible ? "true" : "false") << "},\n";
    f << "  \"circle_preview\": {\"active\": " << (g_circlePreview.enabled ? "true" : "false")
      << ", \"path\": \"" << EscapeJsonString(WStringToUtf8(GetMacrohelpCirclePreviewPath())) << "\"";
    if (g_circlePreview.enabled) {
        f << ", \"origin\": {\"x\": " << g_circlePreview.originX << ", \"y\": " << g_circlePreview.originY << "}"
          << ", \"target\": {\"x\": " << g_circlePreview.targetX << ", \"y\": " << g_circlePreview.targetY << "}"
          << ", \"radius\": " << g_circlePreview.radius;
    }
    f << "},\n";
    f << "  \"circle_placement\": {\"active\": " << (g_circlePlacement.active ? "true" : "false")
      << ", \"stage\": " << (int)g_circlePlacement.stage
      << ", \"target_valid\": " << (g_circlePlacement.targetValid ? "true" : "false")
      << ", \"origin\": {\"x\": " << g_circlePlacement.origin.x << ", \"y\": " << g_circlePlacement.origin.y << "}"
      << ", \"target\": {\"x\": " << g_circlePlacement.target.x << ", \"y\": " << g_circlePlacement.target.y << "}"
      << ", \"radius\": " << g_circlePlacement.radius
      << ", \"theta_radians\": " << g_circlePlacement.thetaRadians
      << ", \"last_input\": \"" << EscapeJsonString(WStringToUtf8(g_circlePlacement.lastInput)) << "\"},\n";
    f << "  \"key_hud_status\": \"" << EscapeJsonString(WStringToUtf8(GetKeyHudStatusText())) << "\",\n";
    f << "  \"latest_keys\": " << JsonStringArray(latestKeys) << ",\n";
    f << "  \"latest_key_age_ms\": " << latestAge << ",\n";
    f << "  \"active_keys\": " << JsonStringArray(activeKeys) << ",\n";
    const char* slotNames[4] = {"Z", "X", "C", "V"};
    f << "  \"paste_buffers\": {";
    for (int i = 0; i < 4; ++i) {
        if (i) f << ", ";
        f << "\"" << slotNames[i] << "\": {\"chars\": " << g_pasteBuffers[i].size()
          << ", \"text\": \"" << EscapeJsonString(WStringToUtf8(g_pasteBuffers[i])) << "\"}";
    }
    f << "},\n";
    f << "  \"zone_buffers\": {";
    for (int i = 0; i < 4; ++i) {
        if (i) f << ", ";
        f << "\"" << slotNames[i] << "\": {\"set\": " << (g_zoneBuffers[i].set ? "true" : "false")
          << ", \"start\": {\"x\": " << g_zoneBuffers[i].start.x << ", \"y\": " << g_zoneBuffers[i].start.y << "}"
          << ", \"end\": {\"x\": " << g_zoneBuffers[i].end.x << ", \"y\": " << g_zoneBuffers[i].end.y << "}}";
    }
    f << "},\n";
    f << "  \"registry_hub\": {\"active\": " << (g_hwndRegistryHubDlg && IsWindow(g_hwndRegistryHubDlg) ? "true" : "false")
      << ", \"stage\": " << (int)g_registryHubState.stage
      << ", \"state_path\": \"" << EscapeJsonString(WStringToUtf8(GetMacrohelpRegistryHubStatePath())) << "\""
      << ", \"registry_path\": \"" << EscapeJsonString(WStringToUtf8(g_registryFilePath)) << "\""
      << ", \"shell_alias_x\": \"" << EscapeJsonString(WStringToUtf8(g_registryPowerShellPath)) << "\""
      << ", \"shell_alias_c\": \"" << EscapeJsonString(WStringToUtf8(g_registryCmdPath)) << "\""
      << ", \"shell_alias_g\": \"" << EscapeJsonString(WStringToUtf8(g_regeditedExePath)) << "\""
      << ", \"script_chars\": " << g_registryScript.size() << "}\n";
    f << "}\n";
    f.close();

    MoveFileExW(tmpPath.c_str(), statePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static std::wstring TrimWide(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && (iswspace(value[start]) || value[start] == 0xFEFF)) start++;
    size_t end = value.size();
    while (end > start && iswspace(value[end - 1])) end--;
    return value.substr(start, end - start);
}

static std::string CompactLower(const std::wstring& value) {
    std::string out = WStringToUtf8(value);
    out.erase(std::remove_if(out.begin(), out.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }), out.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return out;
}

static double NormalizeRadians(double theta) {
    double twoPi = 2.0 * 3.14159265358979323846;
    double value = std::fmod(theta, twoPi);
    if (value < 0) value += twoPi;
    return value;
}

class AngleParser {
public:
    explicit AngleParser(std::string source) : s(std::move(source)) {}

    bool parse(double& out) {
        pos = 0;
        try {
            out = parseExpression();
            skip();
            return pos == s.size();
        } catch (...) {
            return false;
        }
    }

private:
    std::string s;
    size_t pos = 0;

    void skip() {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) pos++;
    }

    bool consume(char c) {
        skip();
        if (pos < s.size() && s[pos] == c) {
            pos++;
            return true;
        }
        return false;
    }

    bool beginsFactor() {
        skip();
        if (pos >= s.size()) return false;
        char c = s[pos];
        return c == '(' || c == '.' || std::isdigit((unsigned char)c) ||
               s.compare(pos, 2, "pi") == 0;
    }

    double parseExpression() {
        double value = parseTerm();
        while (true) {
            if (consume('+')) value += parseTerm();
            else if (consume('-')) value -= parseTerm();
            else return value;
        }
    }

    double parseTerm() {
        double value = parseFactor();
        while (true) {
            if (consume('*')) value *= parseFactor();
            else if (consume('/')) value /= parseFactor();
            else if (beginsFactor()) value *= parseFactor();
            else return value;
        }
    }

    double parseFactor() {
        skip();
        if (consume('+')) return parseFactor();
        if (consume('-')) return -parseFactor();
        if (consume('(')) {
            double value = parseExpression();
            if (!consume(')')) throw std::runtime_error("missing close paren");
            return value;
        }
        if (s.compare(pos, 2, "pi") == 0) {
            pos += 2;
            return 3.14159265358979323846;
        }

        const char* start = s.c_str() + pos;
        char* end = nullptr;
        double value = strtod(start, &end);
        if (end == start) throw std::runtime_error("number expected");
        pos += (size_t)(end - start);
        return value;
    }
};

static bool TryParseAngleRadians(const std::wstring& text, double& radians) {
    AngleParser parser(CompactLower(text));
    return parser.parse(radians);
}

static bool TryParseNumberOnly(const std::wstring& text, double& value) {
    std::wstring trimmed = TrimWide(text);
    if (trimmed.empty()) return false;
    wchar_t* end = nullptr;
    value = wcstod(trimmed.c_str(), &end);
    if (end == trimmed.c_str()) return false;
    while (*end && iswspace(*end)) end++;
    return *end == 0;
}

struct VectorParseResult {
    bool ok = false;
    POINT target = {0, 0};
    double radius = 0.0;
    double theta = 0.0;
    std::wstring label;
};

static bool ParseVectorTerms(const std::string& compact, double& iValue, double& jValue) {
    iValue = 0.0;
    jValue = 0.0;
    if (compact.find('i') == std::string::npos || compact.find('j') == std::string::npos) return false;

    size_t start = 0;
    bool found = false;
    while (start < compact.size()) {
        int sign = 1;
        if (compact[start] == '+') {
            start++;
        } else if (compact[start] == '-') {
            sign = -1;
            start++;
        }

        size_t end = start;
        while (end < compact.size() && compact[end] != '+' && compact[end] != '-') end++;
        std::string term = compact.substr(start, end - start);
        start = end;
        if (term.empty()) continue;

        char axis = 0;
        size_t axisPos = term.find('i');
        if (axisPos != std::string::npos) axis = 'i';
        else {
            axisPos = term.find('j');
            if (axisPos != std::string::npos) axis = 'j';
        }
        if (!axis) continue;

        std::string coeffText = term.substr(0, axisPos);
        double coeff = 1.0;
        if (!coeffText.empty()) {
            char* end = nullptr;
            coeff = strtod(coeffText.c_str(), &end);
            if (end == coeffText.c_str() || *end != 0) return false;
        }
        coeff *= sign;

        if (axis == 'i') iValue += coeff;
        else jValue += coeff;
        found = true;
    }
    return found;
}

static bool TryParseVectorInput(const std::wstring& text, POINT origin, VectorParseResult& result) {
    std::string compact = CompactLower(text);
    size_t at = compact.find('@');
    double radius = 0.0;
    std::string vectorPart = compact;
    bool hasRadius = false;

    if (at != std::string::npos) {
        std::wstring radiusPart = Utf8ToWString(compact.substr(0, at));
        if (!TryParseNumberOnly(radiusPart, radius) || radius <= 0) return false;
        vectorPart = compact.substr(at + 1);
        hasRadius = true;
    }

    double iValue = 0.0, jValue = 0.0;
    if (!ParseVectorTerms(vectorPart, iValue, jValue)) return false;

    double len = std::sqrt(iValue * iValue + jValue * jValue);
    if (len <= 0.000001) return false;

    double dx = iValue;
    double dyUp = jValue;
    if (hasRadius) {
        dx = iValue / len * radius;
        dyUp = jValue / len * radius;
    } else {
        radius = len;
    }

    result.ok = true;
    result.target = {
        (LONG)std::lround(origin.x + dx),
        (LONG)std::lround(origin.y - dyUp)
    };
    result.radius = radius;
    result.theta = NormalizeRadians(std::atan2(dyUp, dx));
    std::wstringstream label;
    label << L"vector " << Utf8ToWString(vectorPart) << L" -> X "
          << result.target.x << L" Y " << result.target.y;
    result.label = label.str();
    return true;
}

static POINT PointOnCircle(POINT origin, double radius, double theta) {
    POINT target = {
        (LONG)std::lround(origin.x + radius * std::cos(theta)),
        (LONG)std::lround(origin.y - radius * std::sin(theta))
    };
    return target;
}

static std::wstring FormatAngleLabel(double theta) {
    double normalized = NormalizeRadians(theta);
    double degrees = normalized * 180.0 / 3.14159265358979323846;
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << degrees << L" deg";
    ss << L" / ";
    ss << std::setprecision(4) << normalized << L" rad";
    return ss.str();
}

static std::wstring DialogText(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (!control) return L"";
    int len = GetWindowTextLengthW(control);
    if (len <= 0) return L"";
    std::wstring text((size_t)len + 1, L'\0');
    GetWindowTextW(control, text.data(), len + 1);
    text.resize(wcslen(text.c_str()));
    return text;
}

static bool MouseMovedPastAnchor(POINT anchor, DWORD openedTickMs, DWORD graceMs = 700, int tolerance = 3) {
    DWORD now = GetTickCount();
    if (now - openedTickMs < graceMs) return false;
    POINT pt = {};
    GetCursorPos(&pt);
    int dx = std::abs(pt.x - anchor.x);
    int dy = std::abs(pt.y - anchor.y);
    return dx > tolerance || dy > tolerance;
}

static int UiPx(int value) {
    if (value == 0) return 0;
    int scaled = (int)std::lround((double)value * g_uiScale);
    if (value > 0 && scaled < 1) return 1;
    if (value < 0 && scaled > -1) return -1;
    return scaled;
}

static int UiFontPx(int value) {
    return std::max(1, UiPx(value));
}

static double ReadUiScaleOverride() {
    wchar_t value[64] = {};
    DWORD len = GetEnvironmentVariableW(L"MACROHELP_UI_SCALE", value, (DWORD)(sizeof(value) / sizeof(value[0])));
    if (len == 0 || len >= (DWORD)(sizeof(value) / sizeof(value[0]))) return 0.0;

    wchar_t* end = nullptr;
    double parsed = wcstod(value, &end);
    if (end == value || parsed <= 0.0) return 0.0;
    return std::clamp(parsed, 0.75, 3.0);
}

static double DetectUiScale() {
    double overrideScale = ReadUiScaleOverride();
    if (overrideScale > 0.0) return overrideScale;

    int dpi = 96;
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int detected = GetDeviceCaps(hdc, LOGPIXELSX);
        if (detected >= 72 && detected <= 768) dpi = detected;
        ReleaseDC(nullptr, hdc);
    }

    double scale = std::max(1.0, (double)dpi / 96.0);
    if (g_screenW >= 3000 || g_screenH >= 1800) scale = std::max(scale, 2.0);
    if (g_screenW >= 5000 || g_screenH >= 2800) scale = std::max(scale, 2.5);
    return std::clamp(scale, 1.0, 3.0);
}

static BOOL CALLBACK ApplyDialogUiFontToChild(HWND child, LPARAM fontParam) {
    HFONT font = (HFONT)fontParam;
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);

    wchar_t className[32] = {};
    GetClassNameW(child, className, (int)(sizeof(className) / sizeof(className[0])));
    if (_wcsicmp(className, L"Edit") == 0) {
        SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(UiPx(8), UiPx(8)));
    }
    return TRUE;
}

static void ApplyDialogUiFont(HWND hwnd, HFONT font = nullptr) {
    if (!hwnd) return;
    HFONT effectiveFont = font ? font : g_hFontCoord;
    if (!effectiveFont) return;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)effectiveFont, TRUE);
    EnumChildWindows(hwnd, ApplyDialogUiFontToChild, (LPARAM)effectiveFont);
}

static BOOL CALLBACK ScaleResourceDialogChild(HWND child, LPARAM parentParam) {
    HWND parent = (HWND)parentParam;
    if (GetParent(child) != parent) return TRUE;

    RECT r = {};
    GetWindowRect(child, &r);
    POINT pts[2] = {{r.left, r.top}, {r.right, r.bottom}};
    MapWindowPoints(HWND_DESKTOP, parent, pts, 2);

    int x = UiPx(pts[0].x);
    int y = UiPx(pts[0].y);
    int w = UiPx(pts[1].x - pts[0].x);
    int h = UiPx(pts[1].y - pts[0].y);
    MoveWindow(child, x, y, w, h, TRUE);
    return TRUE;
}

static void ScaleResourceDialogFromCurrentLayout(HWND hwnd) {
    if (!hwnd || g_uiScale <= 1.05) return;

    RECT wr = {};
    RECT client = {};
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &client);

    int oldW = client.right - client.left;
    int oldH = client.bottom - client.top;
    int newClientW = UiPx(oldW);
    int newClientH = UiPx(oldH);
    RECT target = {0, 0, newClientW, newClientH};
    DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&target, style, FALSE, exStyle);

    int newW = target.right - target.left;
    int newH = target.bottom - target.top;
    int x = wr.left - (newW - (wr.right - wr.left)) / 2;
    int y = wr.top - (newH - (wr.bottom - wr.top)) / 2;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, newW, newH, SWP_NOACTIVATE);

    EnumChildWindows(hwnd, ScaleResourceDialogChild, (LPARAM)hwnd);
}

static int KeyDisplayW() { return UiPx(KEY_DISPLAY_W); }
static int KeyDisplayH() { return UiPx(KEY_DISPLAY_H); }
static int CoordPanelW() { return UiPx(COORD_PANEL_CLIENT_W); }
static int CoordPanelH() { return UiPx(COORD_PANEL_CLIENT_H); }

static void LayoutCircleDialog(HWND hwnd) {
    if (!hwnd) return;

    int width = UiPx(382);
    int height = UiPx(112);

    const int margin = UiPx(10);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = g_screenY + g_screenH - height - UiPx(72);
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_CIRCLE_PROMPT), UiPx(12), UiPx(8), width - UiPx(24), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDIT_CIRCLE_VALUE), UiPx(12), UiPx(30), width - UiPx(24), UiPx(26), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_CIRCLE_HINT), UiPx(12), UiPx(60), width - UiPx(24), UiPx(20), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_CIRCLE_STATUS), UiPx(12), UiPx(82), width - UiPx(24), UiPx(22), TRUE);

    HWND moveBtn = GetDlgItem(hwnd, IDC_BTN_CIRCLE_MOVE_NOW);
    HWND appendBtn = GetDlgItem(hwnd, IDC_BTN_CIRCLE_APPEND_COORD);
    HWND nextBtn = GetDlgItem(hwnd, IDC_BTN_CIRCLE_NEXT_CIRCLE);
    HWND zoneBtn = GetDlgItem(hwnd, IDC_BTN_CIRCLE_ZONE_ACTION);
    HWND enterBtn = GetDlgItem(hwnd, IDOK);
    HWND cancelBtn = GetDlgItem(hwnd, IDCANCEL);

    ShowWindow(moveBtn, SW_HIDE);
    ShowWindow(appendBtn, SW_HIDE);
    ShowWindow(nextBtn, SW_HIDE);
    ShowWindow(zoneBtn, SW_HIDE);
    ShowWindow(enterBtn, SW_HIDE);
    ShowWindow(cancelBtn, SW_HIDE);
    SetFocus(GetDlgItem(hwnd, IDC_EDIT_CIRCLE_VALUE));
}

static void ShowDlgItem(HWND hwnd, int id, bool visible) {
    HWND h = GetDlgItem(hwnd, id);
    if (h) ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
}

static const char* ClickButtonToken(int buttonIndex) {
    switch (buttonIndex) {
        case 1: return "RIGHT_MOUSE";
        case 2: return "MIDDLE_MOUSE";
        default: return "LEFT_MOUSE";
    }
}

static const char* ClickButtonSlug(int buttonIndex) {
    switch (buttonIndex) {
        case 1: return "right";
        case 2: return "middle";
        default: return "left";
    }
}

static std::wstring ClickButtonLabel(int buttonIndex) {
    switch (buttonIndex) {
        case 1: return L"right click";
        case 2: return L"middle click";
        default: return L"left click";
    }
}

static DWORD ClickButtonDownFlag(int buttonIndex) {
    switch (buttonIndex) {
        case 1: return MOUSEEVENTF_RIGHTDOWN;
        case 2: return MOUSEEVENTF_MIDDLEDOWN;
        default: return MOUSEEVENTF_LEFTDOWN;
    }
}

static DWORD ClickButtonUpFlag(int buttonIndex) {
    switch (buttonIndex) {
        case 1: return MOUSEEVENTF_RIGHTUP;
        case 2: return MOUSEEVENTF_MIDDLEUP;
        default: return MOUSEEVENTF_LEFTUP;
    }
}

static void SendNativeMouseButton(int buttonIndex, bool down) {
    INPUT ip = {};
    ip.type = INPUT_MOUSE;
    ip.mi.dwFlags = down ? ClickButtonDownFlag(buttonIndex) : ClickButtonUpFlag(buttonIndex);
    SendInput(1, &ip, sizeof(INPUT));
}

static void SendNativeMouseClick(int buttonIndex) {
    SendNativeMouseButton(buttonIndex, true);
    Sleep(35);
    SendNativeMouseButton(buttonIndex, false);
}

static void LayoutClickActionDialog(HWND hwnd) {
    if (!hwnd) return;

    int width = UiPx(560);
    int height = UiPx(118);
    const int margin = UiPx(12);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = g_screenY + g_screenH - height - UiPx(42);
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_CLICK_PROMPT), UiPx(14), UiPx(10), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDIT_CLICK_VALUE), UiPx(14), UiPx(30), width - UiPx(28), UiPx(26), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_CLICK_HINT), UiPx(14), UiPx(62), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_CLICK_STATUS), UiPx(14), UiPx(84), width - UiPx(28), UiPx(20), TRUE);
    ShowDlgItem(hwnd, IDOK, false);
    ShowDlgItem(hwnd, IDCANCEL, false);
    MoveWindow(GetDlgItem(hwnd, IDOK), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), -200, -200, 1, 1, TRUE);
}

static void LayoutViewTogglesDialog(HWND hwnd) {
    if (!hwnd) return;

    int width = UiPx(430);
    int height = UiPx(118);
    const int margin = UiPx(12);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = g_screenY + g_screenH - height - UiPx(42);
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_VIEW_PROMPT), UiPx(14), UiPx(10), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDIT_VIEW_VALUE), UiPx(14), UiPx(30), width - UiPx(28), UiPx(26), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_VIEW_HINT), UiPx(14), UiPx(62), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_VIEW_STATUS), UiPx(14), UiPx(84), width - UiPx(28), UiPx(20), TRUE);
    ShowDlgItem(hwnd, IDOK, false);
    ShowDlgItem(hwnd, IDCANCEL, false);
    MoveWindow(GetDlgItem(hwnd, IDOK), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), -200, -200, 1, 1, TRUE);
}

static void FocusViewTogglesEdit(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    HWND edit = GetDlgItem(hwnd, IDC_EDIT_VIEW_VALUE);
    if (!edit || !IsWindow(edit)) return;
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

static int PasteBufferSlotFromCommand(const std::string& command) {
    if (command == "z") return 0;
    if (command == "x") return 1;
    if (command == "c") return 2;
    if (command == "v") return 3;
    return -1;
}

static wchar_t PasteBufferSlotKey(int slotIndex) {
    static const wchar_t* keys = L"ZXCV";
    if (slotIndex < 0 || slotIndex > 3) return L'?';
    return keys[slotIndex];
}

static std::wstring PasteBufferSlotLabel(int slotIndex) {
    wchar_t key = PasteBufferSlotKey(slotIndex);
    std::wstring label = L"Buffer ";
    label.push_back(key);
    return label;
}

static std::wstring ZoneBufferSlotLabel(int slotIndex) {
    wchar_t key = PasteBufferSlotKey(slotIndex);
    std::wstring label = L"Zone ";
    label.push_back(key);
    return label;
}

static int ZoneFlowFromCommand(const std::string& command, bool textOnly = false) {
    if (!textOnly && (command == "z" || command == "snip" || command == "snipcopy")) return 0;
    if (command == "x" || command == "text") return 1;
    if (command == "c" || command == "single" || command == "singleline") return 2;
    if (command == "v" || command == "copy" || command == "zonecopy") return 3;
    return -1;
}

static std::wstring FormatZoneBufferCoordinates(POINT start, POINT end) {
    std::wstringstream ss;
    ss << start.x << L"," << start.y << L"," << end.x << L"," << end.y;
    return ss.str();
}

static bool ParseZoneBufferCoordinates(const std::wstring& raw, POINT& start, POINT& end) {
    std::vector<int> values;
    const wchar_t* p = raw.c_str();
    while (*p) {
        wchar_t* next = nullptr;
        long value = wcstol(p, &next, 10);
        if (next != p) {
            values.push_back((int)value);
            p = next;
        } else {
            ++p;
        }
    }
    if (values.size() != 4) return false;
    start = {values[0], values[1]};
    end = {values[2], values[3]};
    return true;
}

static int CountTextLinesLimited(const std::wstring& text, int maxLines) {
    int lines = 1;
    for (wchar_t ch : text) {
        if (ch == L'\n') ++lines;
    }
    if (lines < 1) lines = 1;
    if (maxLines > 0 && lines > maxLines) lines = maxLines;
    return lines;
}

static int CountTextLines(const std::wstring& text) {
    return CountTextLinesLimited(text, 8);
}

static int CountPasteBufferVisibleLines(const std::wstring& text) {
    return CountTextLinesLimited(text, PASTE_BUFFER_MAX_VISIBLE_LINES);
}

static bool RegistryHubStageUsesGrowEdit(RegistryHubStage stage) {
    return stage == RegistryHubStage::PowerShellPath ||
           stage == RegistryHubStage::CmdPath ||
           stage == RegistryHubStage::RegeditedPath ||
           stage == RegistryHubStage::VarEdit ||
           stage == RegistryHubStage::PasteBufferEdit;
}

static void FocusPasteBufferEdit(HWND hwnd, bool selectAll = true) {
    if (!hwnd || !IsWindow(hwnd)) return;
    HWND edit = GetDlgItem(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE);
    if (!edit || !IsWindow(edit)) return;
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(edit);
    if (selectAll) {
        SendMessageW(edit, EM_SETSEL, 0, -1);
    } else {
        SendMessageW(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    }
}

static void LayoutPasteBuffersDialog(HWND hwnd) {
    if (!hwnd) return;

    int width = UiPx(430);
    int editHeight = UiPx(26);
    if (g_pasteBufferState.stage == PasteBufferStage::Edit ||
        g_pasteBufferState.stage == PasteBufferStage::ZoneEdit) {
        int lineLimit = g_pasteBufferState.stage == PasteBufferStage::Edit ? PASTE_BUFFER_MAX_VISIBLE_LINES : 8;
        int lines = std::clamp(g_pasteBufferState.visibleLines, 1, lineLimit);
        editHeight = UiPx(26) + (lines - 1) * UiPx(20);
        int maxByScreen = std::max(UiPx(96), g_screenH - UiPx(178));
        int maxByLines = UiPx(26) + (lineLimit - 1) * UiPx(20);
        editHeight = std::min(editHeight, std::min(maxByScreen, maxByLines));
    }
    int height = UiPx(118) + (editHeight - UiPx(26));
    const int margin = UiPx(12);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = g_screenY + g_screenH - height - UiPx(42);
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT), UiPx(14), UiPx(10), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE), UiPx(14), UiPx(30), width - UiPx(28), editHeight, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_PASTE_BUFFER_HINT), UiPx(14), UiPx(40) + editHeight, width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS), UiPx(14), UiPx(62) + editHeight, width - UiPx(28), UiPx(20), TRUE);
    ShowDlgItem(hwnd, IDOK, false);
    ShowDlgItem(hwnd, IDCANCEL, false);
    MoveWindow(GetDlgItem(hwnd, IDOK), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), -200, -200, 1, 1, TRUE);
}

static void LayoutRecordKeysDialog(HWND hwnd) {
    if (!hwnd) return;

    bool isNameStage = g_recordKeysState.stage == RecordKeysStage::Name;
    bool isPasteStage = g_recordKeysState.stage == RecordKeysStage::Paste;
    bool isCommandStage = g_recordKeysState.stage == RecordKeysStage::Command;

    int width = UiPx(650);
    int height = isNameStage ? UiPx(124) : (isPasteStage ? UiPx(272) : UiPx(218));
    const int margin = UiPx(12);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = g_screenY + g_screenH - height - UiPx(34);
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);

    ShowDlgItem(hwnd, IDC_STATIC_SEQ_NAME, !isPasteStage);
    ShowDlgItem(hwnd, IDC_EDIT_SEQ_NAME, !isPasteStage);
    ShowDlgItem(hwnd, IDC_STATIC_SEQ_DELAY, isNameStage);
    ShowDlgItem(hwnd, IDC_EDIT_DELAY, isNameStage);
    ShowDlgItem(hwnd, IDC_STATIC_RECORDED_FLOW, !isNameStage);
    ShowDlgItem(hwnd, IDC_LIST_KEYS, !isNameStage);
    ShowDlgItem(hwnd, IDC_STATIC_PASTE_BLOCK, isPasteStage);
    ShowDlgItem(hwnd, IDC_EDIT_PASTE_TEXT, isPasteStage);
    ShowDlgItem(hwnd, IDC_STATIC_RECORD_STATUS_LABEL, !isNameStage);
    ShowDlgItem(hwnd, IDC_BTN_CAPTURE, false);
    ShowDlgItem(hwnd, IDC_BTN_ADD_STEP, false);
    ShowDlgItem(hwnd, IDC_BTN_MANUAL, false);
    ShowDlgItem(hwnd, IDC_BTN_ADD_ANOTHER_KEY, false);
    ShowDlgItem(hwnd, IDC_BTN_ADD_PASTE, false);
    ShowDlgItem(hwnd, IDOK, false);
    ShowDlgItem(hwnd, IDCANCEL, false);

    if (isNameStage) {
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_SEQ_NAME), UiPx(14), UiPx(12), UiPx(360), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_EDIT_SEQ_NAME), UiPx(14), UiPx(30), width - UiPx(128), UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_SEQ_DELAY), width - UiPx(100), UiPx(12), UiPx(86), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_EDIT_DELAY), width - UiPx(100), UiPx(30), UiPx(84), UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_STATUS), UiPx(14), UiPx(70), width - UiPx(28), UiPx(40), TRUE);
    } else if (isCommandStage) {
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_SEQ_NAME), UiPx(14), UiPx(12), width - UiPx(28), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_EDIT_SEQ_NAME), UiPx(14), UiPx(30), width - UiPx(28), UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_RECORDED_FLOW), UiPx(14), UiPx(66), UiPx(180), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LIST_KEYS), UiPx(14), UiPx(84), UiPx(306), UiPx(120), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_RECORD_STATUS_LABEL), UiPx(336), UiPx(66), UiPx(70), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_STATUS), UiPx(336), UiPx(84), width - UiPx(350), UiPx(70), TRUE);
    } else {
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_RECORDED_FLOW), UiPx(14), UiPx(14), UiPx(180), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LIST_KEYS), UiPx(14), UiPx(32), UiPx(292), UiPx(178), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_PASTE_BLOCK), UiPx(324), UiPx(14), width - UiPx(338), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_EDIT_PASTE_TEXT), UiPx(324), UiPx(32), width - UiPx(338), UiPx(148), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_RECORD_STATUS_LABEL), UiPx(324), UiPx(190), UiPx(70), UiPx(16), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATIC_STATUS), UiPx(324), UiPx(208), width - UiPx(338), UiPx(44), TRUE);
    }

    MoveWindow(GetDlgItem(hwnd, IDC_BTN_CAPTURE), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD_STEP), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_MANUAL), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD_ANOTHER_KEY), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD_PASTE), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDOK), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), -200, -200, 1, 1, TRUE);
}

static void LayoutSaveCursorDialog(HWND hwnd) {
    if (!hwnd) return;

    int width = UiPx(500);
    int height = UiPx(124);
    const int margin = UiPx(12);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = g_screenY + g_screenH - height - UiPx(42);
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_SAVE_NAME), UiPx(14), UiPx(10), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDIT_NAME), UiPx(14), UiPx(28), width - UiPx(28), UiPx(26), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_SAVE_COORD_LABEL), UiPx(14), UiPx(60), UiPx(90), UiPx(16), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_COORDS), UiPx(110), UiPx(60), width - UiPx(124), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_SAVE_HINT), UiPx(14), UiPx(88), width - UiPx(28), UiPx(24), TRUE);
    ShowDlgItem(hwnd, IDOK, false);
    ShowDlgItem(hwnd, IDC_BTN_ADD_ANOTHER_CURSOR, false);
    ShowDlgItem(hwnd, IDC_BTN_ADD_CLICK_CURSOR, false);
    ShowDlgItem(hwnd, IDCANCEL, false);
    MoveWindow(GetDlgItem(hwnd, IDOK), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD_ANOTHER_CURSOR), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_ADD_CLICK_CURSOR), -200, -200, 1, 1, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), -200, -200, 1, 1, TRUE);
}

static void UpdateSaveCursorDialogText(HWND hwnd) {
    if (!hwnd) return;
    if (g_saveCursorState.stage == SaveCursorStage::Name) {
        SetDlgItemTextW(hwnd, IDC_STATIC_SAVE_NAME, L"Name this cursor position, then press Enter");
        SetDlgItemTextW(hwnd, IDC_STATIC_SAVE_HINT,
            L"Before Enter: moving the mouse closes this HUD.");
        SetDlgItemTextW(hwnd, IDOK, L"&Enter");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_ANOTHER_CURSOR, L"&Add Point");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_CLICK_CURSOR, L"Save + &Click");
        SetDlgItemTextW(hwnd, IDCANCEL, L"&Cancel");
        SetFocus(GetDlgItem(hwnd, IDC_EDIT_NAME));
    } else {
        std::wstring prompt = L"Z Save  |  X Add point  |  C Save + click  |  V Cancel";
        SetDlgItemTextW(hwnd, IDC_STATIC_SAVE_NAME, prompt.c_str());
        std::wstringstream hint;
        hint << L"Stored name: " << g_saveCursorState.name
             << L"  |  points: " << g_saveCursorState.coords.size()
             << L"  |  Type one command key.";
        SetDlgItemTextW(hwnd, IDC_STATIC_SAVE_HINT, hint.str().c_str());
        SetDlgItemTextW(hwnd, IDC_EDIT_NAME, L"");
        SetDlgItemTextW(hwnd, IDOK, L"&Z Save");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_ANOTHER_CURSOR, L"&X Add Point");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_CLICK_CURSOR, L"&C + Click");
        SetDlgItemTextW(hwnd, IDCANCEL, L"&V Cancel");
        SetFocus(GetDlgItem(hwnd, IDC_EDIT_NAME));
    }
    LayoutSaveCursorDialog(hwnd);
}

static void AddSaveCursorPoint(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    g_saveCursorState.coords.push_back({0, 1000, pt.x, pt.y});
    std::wstringstream ss;
    ss << L"Added point " << g_saveCursorState.coords.size() << L": X=" << pt.x << L" Y=" << pt.y;
    SetDlgItemTextW(hwnd, IDC_STATIC_COORDS, ss.str().c_str());
}

static bool WriteSaveCursorActions(HWND hwnd, bool includeClick) {
    if (g_saveCursorState.name.empty()) return false;
    std::string name = EscapeJsonString(WStringToUtf8(g_saveCursorState.name));
    bool ok = AppendActionAndWait(BuildCursorMovementActionJson(name, g_saveCursorState.coords));
    if (ok && includeClick) {
        ok = AppendActionAndWait(BuildLeftClickActionJson(name + "_left_click"));
    }
    if (!ok) {
        MessageBoxW(hwnd, L"Failed to save to clicksession.txt.",
                   L"Error", MB_OK | MB_ICONERROR);
    }
    return ok;
}

static void CloseSaveCursorDialog(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, SAVE_CURSOR_WATCH_TIMER);
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    g_hwndSaveDlg = nullptr;
    g_saveCursorState = SaveCursorState{};
}

static bool AdvanceSaveCursorToCommand(HWND hwnd) {
    std::wstring name = TrimWide(DialogText(hwnd, IDC_EDIT_NAME));
    if (name.empty()) {
        MessageBoxW(hwnd, L"Please enter a name for this cursor position.",
                   L"Name Required", MB_OK | MB_ICONWARNING);
        return false;
    }
    g_saveCursorState.name = name;
    g_saveCursorState.stage = SaveCursorStage::Command;
    UpdateSaveCursorDialogText(hwnd);
    return true;
}

static bool RunSaveCursorCommand(HWND hwnd, const std::wstring& rawCommand) {
    std::string command = CompactLower(rawCommand);
    if (command.empty() || command == "z" || command == "save") {
        if (g_saveCursorState.stage == SaveCursorStage::Name) {
            return AdvanceSaveCursorToCommand(hwnd);
        }
        if (WriteSaveCursorActions(hwnd, false)) CloseSaveCursorDialog(hwnd);
        return true;
    }
    if (command == "x" || command == "add" || command == "point") {
        if (g_saveCursorState.stage == SaveCursorStage::Name) {
            AddSaveCursorPoint(hwnd);
        } else {
            AddSaveCursorPoint(hwnd);
            SetDlgItemTextW(hwnd, IDC_EDIT_NAME, L"");
            UpdateSaveCursorDialogText(hwnd);
        }
        return true;
    }
    if (command == "c" || command == "click") {
        if (g_saveCursorState.stage == SaveCursorStage::Name) {
            if (!AdvanceSaveCursorToCommand(hwnd)) return true;
        }
        if (WriteSaveCursorActions(hwnd, true)) CloseSaveCursorDialog(hwnd);
        return true;
    }
    if (command == "v" || command == "cancel" || command == "esc") {
        CloseSaveCursorDialog(hwnd);
        return true;
    }
    return false;
}

static void CloseClickActionDialog(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, CLICK_ACTION_WATCH_TIMER);
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    g_hwndClickDlg = nullptr;
    g_clickActionState = ClickActionState{};
}

static void UpdateClickActionDialogText(HWND hwnd) {
    if (!hwnd) return;
    std::wstring label = ClickButtonLabel(g_clickActionState.buttonIndex);
    if (g_clickActionState.stage == ClickActionStage::HoldMs) {
        std::wstring prompt = L"Hold " + label;
        SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_PROMPT, prompt.c_str());
        SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_HINT,
            L"Type hold/release-after milliseconds, then press Enter.");
        SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_STATUS,
            L"Example: 800 holds before release. Esc or mouse move cancels.");
    } else {
        std::wstring prompt = L"Manual " + label;
        SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_PROMPT, prompt.c_str());
        SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_HINT,
            L"Z click in 1s | X hold ms | C write JSON");
        SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_STATUS,
            L"Esc or mouse move cancels. Shift+Alt+7 stops Tasket.");
    }
    SetDlgItemTextW(hwnd, IDC_EDIT_CLICK_VALUE, L"");
    SetFocus(GetDlgItem(hwnd, IDC_EDIT_CLICK_VALUE));
    LayoutClickActionDialog(hwnd);
}

static bool WriteClickAction(HWND hwnd, int releaseAfterMs) {
    if (releaseAfterMs < 1) releaseAfterMs = 100;
    std::wstring status;
    if (!ScheduleTasketClick(g_clickActionState.buttonIndex, releaseAfterMs, 1, &status)) {
        std::wstring msg = status.empty() ? L"Failed to schedule click through Tasket HTTP." : status;
        MessageBoxW(hwnd, msg.c_str(), L"Tasket Click Failed", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

static bool WriteClickJsonForLater(HWND hwnd) {
    std::string slug = ClickButtonSlug(g_clickActionState.buttonIndex);
    std::string action = BuildMouseClickActionJson(
        "macrohelp_" + slug + "_click_json",
        ClickButtonToken(g_clickActionState.buttonIndex),
        100);
    if (!AppendActionAndWait(action)) {
        MessageBoxW(hwnd, L"Failed to append click JSON to clicksession.txt.",
                    L"Write Failed", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

static bool RunClickActionCommand(HWND hwnd, const std::wstring& rawCommand) {
    std::wstring trimmed = TrimWide(rawCommand);
    std::string command = CompactLower(trimmed);
    if (command == "cancel" || command == "esc") {
        CloseClickActionDialog(hwnd);
        return true;
    }

    if (g_clickActionState.stage == ClickActionStage::HoldMs) {
        int releaseAfterMs = 100;
        if (!command.empty()) {
            wchar_t* end = nullptr;
            long parsed = wcstol(trimmed.c_str(), &end, 10);
            while (end && *end && iswspace(*end)) end++;
            if (end == trimmed.c_str() || (end && *end != 0)) {
                SetDlgItemTextW(hwnd, IDC_EDIT_CLICK_VALUE, L"");
                SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_STATUS,
                    L"Type only milliseconds, like 100 or 800. Esc cancels.");
                return true;
            }
            if (parsed < 1) parsed = 1;
            if (parsed > 600000) parsed = 600000;
            releaseAfterMs = (int)parsed;
        }

        if (WriteClickAction(hwnd, releaseAfterMs)) {
            CloseClickActionDialog(hwnd);
        }
        return true;
    }

    if (command == "z" || command == "click") {
        if (WriteClickAction(hwnd, 100)) CloseClickActionDialog(hwnd);
        return true;
    }
    if (command == "x" || command == "hold") {
        g_clickActionState.stage = ClickActionStage::HoldMs;
        GetCursorPos(&g_clickActionState.cancelOnMouseMoveFrom);
        g_clickActionState.openedTickMs = GetTickCount();
        UpdateClickActionDialogText(hwnd);
        return true;
    }
    if (command == "c" || command == "write" || command == "json") {
        if (WriteClickJsonForLater(hwnd)) CloseClickActionDialog(hwnd);
        return true;
    }
    SetDlgItemTextW(hwnd, IDC_EDIT_CLICK_VALUE, L"");
    SetDlgItemTextW(hwnd, IDC_STATIC_CLICK_STATUS,
        L"Use Z click, X hold, C write JSON. Esc cancels.");
    return true;
}

void StartClickAction(int buttonIndex) {
    if (g_hwndClickDlg && IsWindow(g_hwndClickDlg)) CloseClickActionDialog(g_hwndClickDlg);
    if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) CloseSaveCursorDialog(g_hwndSaveDlg);
    if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) CloseRecordKeysDialog(g_hwndRecordDlg);
    if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) ResetCirclePlacement(true);
    if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg)) ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);

    g_clickActionState = ClickActionState{};
    g_clickActionState.buttonIndex = buttonIndex;
    GetCursorPos(&g_clickActionState.cancelOnMouseMoveFrom);
    g_clickActionState.openedTickMs = GetTickCount();
    if (!g_hwndClickDlg || !IsWindow(g_hwndClickDlg)) {
        HWND owner = g_hwndCrosshair ? g_hwndCrosshair : nullptr;
        g_hwndClickDlg = CreateDialogParamW(g_hInst,
            MAKEINTRESOURCE(IDD_CLICK_ACTION), owner,
            (DLGPROC)ClickActionDlgProc, 0);
    }
    if (g_hwndClickDlg) {
        ShowWindow(g_hwndClickDlg, SW_SHOW);
        SetWindowPos(g_hwndClickDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hwndClickDlg);
        UpdateClickActionDialogText(g_hwndClickDlg);
    }
}

static void CloseViewTogglesDialog(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, VIEW_TOGGLE_WATCH_TIMER);
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    g_hwndViewTogglesDlg = nullptr;
    g_viewToggleState = ViewToggleState{};
}

static void UpdateViewTogglesDialogText(HWND hwnd) {
    if (!hwnd) return;
    SetDlgItemTextW(hwnd, IDC_STATIC_VIEW_PROMPT, L"Display toggles");
    SetDlgItemTextW(hwnd, IDC_STATIC_VIEW_HINT, L"Z target | X grid | C coords only | K key HUD");

    std::wstringstream ss;
    ss << L"Crosshair " << (g_crosshairVisible ? L"on" : L"off")
       << L"  |  Grid " << (g_gridVisible ? L"on" : L"off")
       << L"  |  Coords-only " << (g_coordinatesOnlyMode ? L"on" : L"off")
       << L"  |  Keys " << (g_keyDisplayVisible ? L"on" : L"off")
       << L"  |  Type one command key.";
    SetDlgItemTextW(hwnd, IDC_STATIC_VIEW_STATUS, ss.str().c_str());
    SetDlgItemTextW(hwnd, IDC_EDIT_VIEW_VALUE, L"");
    LayoutViewTogglesDialog(hwnd);
    FocusViewTogglesEdit(hwnd);
}

static bool RunViewToggleCommand(HWND hwnd, const std::wstring& rawCommand) {
    std::string command = CompactLower(rawCommand);
    if (command == "cancel" || command == "esc" || command == "v") {
        CloseViewTogglesDialog(hwnd);
        return true;
    }

    if (command == "z" || command == "crosshair") {
        g_crosshairVisible = !g_crosshairVisible;
        ShowWindow(g_hwndCrosshair, g_crosshairVisible ? SW_SHOW : SW_HIDE);
        PushKeyHistory({g_crosshairVisible ? "CROSSHAIR_ON" : "CROSSHAIR_OFF"});
        CloseViewTogglesDialog(hwnd);
        return true;
    }

    if (command == "c" || command == "coords" || command == "coordinates") {
        g_coordinatesOnlyMode = !g_coordinatesOnlyMode;
        if (g_coordinatesOnlyMode && !g_crosshairVisible) {
            g_crosshairVisible = true;
            ShowWindow(g_hwndCrosshair, SW_SHOW);
        }
        InvalidateRect(g_hwndCrosshair, nullptr, FALSE);
        PushKeyHistory({g_coordinatesOnlyMode ? "COORDS_ONLY" : "TARGET_ON"});
        CloseViewTogglesDialog(hwnd);
        return true;
    }

    if (command == "x" || command == "grid" || command == "gridlines") {
        g_gridVisible = !g_gridVisible;
        if (g_gridVisible && !g_crosshairVisible) {
            g_crosshairVisible = true;
            ShowWindow(g_hwndCrosshair, SW_SHOW);
        }
        InvalidateRect(g_hwndCrosshair, nullptr, FALSE);
        PushKeyHistory({g_gridVisible ? "GRID_ON" : "GRID_OFF"});
        CloseViewTogglesDialog(hwnd);
        return true;
    }

    if (command == "k" || command == "keys" || command == "keyhud" ||
        command == "hidekeys" || command == "hide_keys" || command == "showkeys" || command == "show_keys") {
        g_keyDisplayVisible = !g_keyDisplayVisible;
        if (g_hwndKeyDisplay && IsWindow(g_hwndKeyDisplay)) {
            ShowWindow(g_hwndKeyDisplay, g_keyDisplayVisible ? SW_SHOW : SW_HIDE);
        }
        PushKeyHistory({g_keyDisplayVisible ? "KEYS_ON" : "KEYS_OFF"});
        CloseViewTogglesDialog(hwnd);
        return true;
    }

    SetDlgItemTextW(hwnd, IDC_EDIT_VIEW_VALUE, L"");
    SetDlgItemTextW(hwnd, IDC_STATIC_VIEW_STATUS,
        L"Use Z crosshair, X gridlines, C coordinates-only, or K key HUD. Esc cancels.");
    return true;
}

void StartViewToggles() {
    if (g_hwndViewTogglesDlg && IsWindow(g_hwndViewTogglesDlg)) CloseViewTogglesDialog(g_hwndViewTogglesDlg);
    if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) CloseSaveCursorDialog(g_hwndSaveDlg);
    if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) CloseRecordKeysDialog(g_hwndRecordDlg);
    if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) ResetCirclePlacement(true);
    if (g_hwndClickDlg && IsWindow(g_hwndClickDlg)) CloseClickActionDialog(g_hwndClickDlg);
    if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg)) ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);

    g_viewToggleState = ViewToggleState{};
    GetCursorPos(&g_viewToggleState.cancelOnMouseMoveFrom);
    g_viewToggleState.openedTickMs = GetTickCount();

    HWND owner = (g_hwndCoordPanel && IsWindow(g_hwndCoordPanel) && IsWindowVisible(g_hwndCoordPanel))
        ? g_hwndCoordPanel
        : nullptr;
    g_hwndViewTogglesDlg = CreateDialogParamW(g_hInst,
        MAKEINTRESOURCE(IDD_VIEW_TOGGLES), owner,
        (DLGPROC)ViewTogglesDlgProc, 0);
    if (g_hwndViewTogglesDlg) {
        ShowWindow(g_hwndViewTogglesDlg, SW_SHOW);
        SetWindowPos(g_hwndViewTogglesDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hwndViewTogglesDlg);
        UpdateViewTogglesDialogText(g_hwndViewTogglesDlg);
        FocusViewTogglesEdit(g_hwndViewTogglesDlg);
    }
}

static void ClosePasteBuffersDialog(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, PASTE_BUFFER_WATCH_TIMER);
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    g_hwndPasteBuffersDlg = nullptr;
    g_pasteBufferState = PasteBufferState{};
}

static void UpdatePasteBuffersDialogText(HWND hwnd) {
    if (!hwnd) return;

    if (g_pasteBufferState.stage == PasteBufferStage::Edit) {
        std::wstring prompt = PasteBufferSlotLabel(g_pasteBufferState.activeSlot);
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, prompt.c_str());
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Enter saves. Shift+Enter adds a line and grows upward, capped at 50 visible lines.");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Mouse move cancels. Esc cancels without saving.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::PasteSelect) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, L"Paste one");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Z/X/C/V pastes one | B sequence list");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Single paste fires immediately. B waits for Enter.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::PasteSequence) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, L"Paste sequence");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Enter comma-delimited buffers, then press Enter. Example: Z,C,V");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"One focus click, then each paste with micro-delays.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZoneCommand) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT,
            g_pasteBufferState.pendingZoneFromCircle ? L"Save circle zone" : L"Zone buffers");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            g_pasteBufferState.pendingZoneFromCircle ?
                L"Z/X/C/V stores the current circle zone coordinates." :
                L"Z/X/C/V edit zone buffers | B play or clip a zone");
        std::wstring status = g_pasteBufferState.pendingZoneFromCircle ?
            (L"Pending: " + FormatZoneBufferCoordinates(
                g_pasteBufferState.pendingZoneStart, g_pasteBufferState.pendingZoneEnd)) :
            L"Zone format: x1,y1,x2,y2. Esc or mouse move cancels.";
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS, status.c_str());
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZoneEdit) {
        std::wstring prompt = ZoneBufferSlotLabel(g_pasteBufferState.activeZoneSlot);
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, prompt.c_str());
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Enter x1,y1,x2,y2, then press Enter.");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Stores a reusable zone for snip/text/copy playback.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZonePlaySelect) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, L"Play zone");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Z/X/C/V chooses a stored zone | B clips a zone into a paste buffer");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Playback asks how to extract after the zone slot is selected.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZonePlayFlow) {
        std::wstring prompt = ZoneBufferSlotLabel(g_pasteBufferState.activeZoneSlot) + L" method";
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, prompt.c_str());
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Z snip+copy | X text | C single-line | V zone+copy");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Runs the saved zone immediately through Tasket.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureZoneSelect) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, L"Clip zone to buffer");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Z/X/C/V chooses the stored zone to clip.");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Next: choose text extraction style, then destination paste buffer.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureFlow) {
        std::wstring prompt = ZoneBufferSlotLabel(g_pasteBufferState.activeZoneSlot) + L" clip style";
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, prompt.c_str());
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"X text | C single-line | V zone+copy. Z image is intentionally skipped.");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Only text clipboard results can be stored into paste buffers.");
    } else if (g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureTarget) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, L"Store clipped text");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Z/X/C/V chooses the paste buffer destination.");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Tasket runs the zone, opens this HUD, pastes clipboard, and saves.");
    } else {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_PROMPT, L"Paste buffers");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_HINT,
            L"Z/X/C/V edit buffers | B paste one | N zone buffers");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Type one command key. Esc or mouse move cancels.");
    }

    if (g_pasteBufferState.stage != PasteBufferStage::Edit &&
        g_pasteBufferState.stage != PasteBufferStage::ZoneEdit) {
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
    }
    LayoutPasteBuffersDialog(hwnd);
    FocusPasteBufferEdit(hwnd,
        g_pasteBufferState.stage != PasteBufferStage::Edit &&
        g_pasteBufferState.stage != PasteBufferStage::ZoneEdit);
}

static bool SavePasteBufferEdit(HWND hwnd) {
    if (g_pasteBufferState.activeSlot < 0 || g_pasteBufferState.activeSlot > 3) return false;
    g_pasteBuffers[g_pasteBufferState.activeSlot] =
        DialogText(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE);
    wchar_t key = PasteBufferSlotKey(g_pasteBufferState.activeSlot);
    PushKeyHistory({"BUFFER", std::string(1, (char)key), "SAVED"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool PasteStoredBuffer(HWND hwnd, int slotIndex) {
    if (slotIndex < 0 || slotIndex > 3) return false;
    std::wstring text = g_pasteBuffers[slotIndex];
    if (text.empty()) {
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"That buffer is empty. Use Z/X/C/V from the first screen to store text.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    std::wstring status;
    if (!ScheduleTasketPasteBuffer(slotIndex, text, &status)) {
        std::wstring msg = status.empty() ? L"Failed to schedule paste through Tasket HTTP." : status;
        MessageBoxW(hwnd, msg.c_str(), L"Tasket Paste Failed", MB_OK | MB_ICONERROR);
        return true;
    }
    wchar_t key = PasteBufferSlotKey(slotIndex);
    PushKeyHistory({"BUFFER", std::string(1, (char)key), "PASTE"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool ParsePasteSequence(HWND hwnd, const std::wstring& raw, std::vector<int>& slots) {
    slots.clear();
    std::wstring token;
    auto flushToken = [&]() -> bool {
        std::wstring trimmed = TrimWide(token);
        token.clear();
        if (trimmed.empty()) return true;
        std::string command = CompactLower(trimmed);
        int slot = PasteBufferSlotFromCommand(command);
        if (slot < 0) {
            SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
                L"Use comma-delimited buffer keys only, like Z,C,V.");
            return false;
        }
        if (g_pasteBuffers[slot].empty()) {
            std::wstring msg = PasteBufferSlotLabel(slot) + L" is empty. Save text there first.";
            SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS, msg.c_str());
            return false;
        }
        slots.push_back(slot);
        return true;
    };

    for (wchar_t ch : raw) {
        if (ch == L',' || ch == L';' || iswspace(ch)) {
            if (!flushToken()) return false;
        } else {
            token.push_back(ch);
        }
    }
    if (!flushToken()) return false;
    if (slots.empty()) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Enter at least one buffer key, like Z or Z,C,V.");
        return false;
    }
    return true;
}

static bool PasteStoredSequence(HWND hwnd) {
    std::vector<int> slots;
    if (!ParsePasteSequence(hwnd, DialogText(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE), slots)) {
        FocusPasteBufferEdit(hwnd, false);
        return true;
    }

    std::wstring status;
    if (!ScheduleTasketPasteSequence(slots, &status)) {
        std::wstring msg = status.empty() ? L"Failed to schedule paste sequence through Tasket HTTP." : status;
        MessageBoxW(hwnd, msg.c_str(), L"Tasket Paste Sequence Failed", MB_OK | MB_ICONERROR);
        return true;
    }
    PushKeyHistory({"BUFFER", "SEQUENCE", "PASTE"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool SaveZoneBufferEdit(HWND hwnd) {
    int slot = g_pasteBufferState.activeZoneSlot;
    if (slot < 0 || slot > 3) return false;
    POINT start = {0, 0};
    POINT end = {0, 0};
    if (!ParseZoneBufferCoordinates(DialogText(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE), start, end)) {
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Use four numbers: x1,y1,x2,y2.");
        FocusPasteBufferEdit(hwnd, false);
        return true;
    }

    g_zoneBuffers[slot].set = true;
    g_zoneBuffers[slot].start = start;
    g_zoneBuffers[slot].end = end;
    wchar_t key = PasteBufferSlotKey(slot);
    PushKeyHistory({"ZONE", std::string(1, (char)key), "SAVED"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool StorePendingCircleZone(HWND hwnd, int slot) {
    if (slot < 0 || slot > 3 || !g_pasteBufferState.pendingZoneFromCircle) return false;
    g_zoneBuffers[slot].set = true;
    g_zoneBuffers[slot].start = g_pasteBufferState.pendingZoneStart;
    g_zoneBuffers[slot].end = g_pasteBufferState.pendingZoneEnd;
    wchar_t key = PasteBufferSlotKey(slot);
    PushKeyHistory({"ZONE", std::string(1, (char)key), "CIRCLE"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool EnsureZoneBufferReady(HWND hwnd, int slot) {
    if (slot < 0 || slot > 3) return false;
    if (!g_zoneBuffers[slot].set) {
        std::wstring msg = ZoneBufferSlotLabel(slot) + L" is empty. Store x1,y1,x2,y2 first.";
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS, msg.c_str());
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        FocusPasteBufferEdit(hwnd);
        return false;
    }
    return true;
}

static bool PlayZoneBufferFlow(HWND hwnd, int flowIndex) {
    int slot = g_pasteBufferState.activeZoneSlot;
    if (!EnsureZoneBufferReady(hwnd, slot)) return true;
    std::wstring status;
    if (!ScheduleTasketZoneFlow(flowIndex, g_zoneBuffers[slot].start, g_zoneBuffers[slot].end, &status)) {
        std::wstring msg = status.empty() ? L"Failed to schedule zone flow through Tasket HTTP." : status;
        MessageBoxW(hwnd, msg.c_str(), L"Tasket Zone Failed", MB_OK | MB_ICONERROR);
        return true;
    }
    wchar_t key = PasteBufferSlotKey(slot);
    PushKeyHistory({"ZONE", std::string(1, (char)key), "PLAY"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool CaptureZoneToPasteBuffer(HWND hwnd, int targetSlot) {
    int zoneSlot = g_pasteBufferState.activeZoneSlot;
    int flowIndex = g_pasteBufferState.activeZoneFlow;
    if (targetSlot < 0 || targetSlot > 3 || flowIndex < 1 || flowIndex > 3) return false;
    if (!EnsureZoneBufferReady(hwnd, zoneSlot)) return true;

    std::wstring status;
    if (!ScheduleTasketZoneCaptureToPasteBuffer(
            flowIndex,
            g_zoneBuffers[zoneSlot].start,
            g_zoneBuffers[zoneSlot].end,
            targetSlot,
            &status)) {
        std::wstring msg = status.empty() ? L"Failed to schedule zone clip through Tasket HTTP." : status;
        MessageBoxW(hwnd, msg.c_str(), L"Tasket Zone Clip Failed", MB_OK | MB_ICONERROR);
        return true;
    }
    wchar_t zoneKey = PasteBufferSlotKey(zoneSlot);
    wchar_t targetKey = PasteBufferSlotKey(targetSlot);
    std::wstringstream ss;
    ss << L"Scheduled Zone " << zoneKey << L" -> Buffer " << targetKey << L" through the HUD.";
    SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS, ss.str().c_str());
    PushKeyHistory({"ZONE", "CLIP", "BUFFER"});
    ClosePasteBuffersDialog(hwnd);
    return true;
}

static bool RunPasteBufferCommand(HWND hwnd, const std::wstring& rawCommand) {
    std::string command = CompactLower(rawCommand);
    if (command == "cancel" || command == "esc") {
        ClosePasteBuffersDialog(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::Edit) {
        return SavePasteBufferEdit(hwnd);
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZoneEdit) {
        return SaveZoneBufferEdit(hwnd);
    }

    if (g_pasteBufferState.stage == PasteBufferStage::PasteSequence) {
        return PasteStoredSequence(hwnd);
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZoneCommand) {
        int slot = PasteBufferSlotFromCommand(command);
        if (slot >= 0) {
            if (g_pasteBufferState.pendingZoneFromCircle) {
                return StorePendingCircleZone(hwnd, slot);
            }
            g_pasteBufferState.stage = PasteBufferStage::ZoneEdit;
            g_pasteBufferState.activeZoneSlot = slot;
            g_pasteBufferState.visibleLines = 1;
            if (g_zoneBuffers[slot].set) {
                SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE,
                    FormatZoneBufferCoordinates(g_zoneBuffers[slot].start, g_zoneBuffers[slot].end).c_str());
            } else {
                SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
            }
            UpdatePasteBuffersDialogText(hwnd);
            FocusPasteBufferEdit(hwnd, false);
            return true;
        }
        if (!g_pasteBufferState.pendingZoneFromCircle &&
            (command == "b" || command == "play" || command == "buffer")) {
            g_pasteBufferState.stage = PasteBufferStage::ZonePlaySelect;
            UpdatePasteBuffersDialogText(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            g_pasteBufferState.pendingZoneFromCircle ?
                L"Choose Z, X, C, or V to store this circle zone." :
                L"Choose Z/X/C/V to edit zones, or B to play/capture. Esc cancels.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZonePlaySelect) {
        int slot = PasteBufferSlotFromCommand(command);
        if (slot >= 0) {
            if (!EnsureZoneBufferReady(hwnd, slot)) return true;
            g_pasteBufferState.activeZoneSlot = slot;
            g_pasteBufferState.stage = PasteBufferStage::ZonePlayFlow;
            UpdatePasteBuffersDialogText(hwnd);
            return true;
        }
        if (command == "b" || command == "clip" || command == "capture") {
            g_pasteBufferState.stage = PasteBufferStage::ZoneCaptureZoneSelect;
            UpdatePasteBuffersDialogText(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Choose a saved zone Z/X/C/V, or B to clip into paste buffers.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZonePlayFlow) {
        int flowIndex = ZoneFlowFromCommand(command);
        if (flowIndex >= 0) return PlayZoneBufferFlow(hwnd, flowIndex);
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Choose Z snip, X text, C single-line, or V zone+copy.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureZoneSelect) {
        int slot = PasteBufferSlotFromCommand(command);
        if (slot >= 0) {
            if (!EnsureZoneBufferReady(hwnd, slot)) return true;
            g_pasteBufferState.activeZoneSlot = slot;
            g_pasteBufferState.stage = PasteBufferStage::ZoneCaptureFlow;
            UpdatePasteBuffersDialogText(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Choose a stored zone Z/X/C/V first.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureFlow) {
        int flowIndex = ZoneFlowFromCommand(command, true);
        if (flowIndex >= 1) {
            g_pasteBufferState.activeZoneFlow = flowIndex;
            g_pasteBufferState.stage = PasteBufferStage::ZoneCaptureTarget;
            UpdatePasteBuffersDialogText(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Choose X text, C single-line, or V zone+copy. Z is image-only here.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureTarget) {
        int targetSlot = PasteBufferSlotFromCommand(command);
        if (targetSlot >= 0) return CaptureZoneToPasteBuffer(hwnd, targetSlot);
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Choose destination paste buffer Z/X/C/V.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    if (g_pasteBufferState.stage == PasteBufferStage::PasteSelect) {
        int slot = PasteBufferSlotFromCommand(command);
        if (slot >= 0) return PasteStoredBuffer(hwnd, slot);
        if (command == "b" || command == "sequence" || command == "list") {
            g_pasteBufferState.stage = PasteBufferStage::PasteSequence;
            SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
            UpdatePasteBuffersDialogText(hwnd);
            FocusPasteBufferEdit(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
            L"Choose Z, X, C, V, or B for sequence. Esc cancels.");
        FocusPasteBufferEdit(hwnd);
        return true;
    }

    int slot = PasteBufferSlotFromCommand(command);
    if (slot >= 0) {
        g_pasteBufferState.stage = PasteBufferStage::Edit;
        g_pasteBufferState.activeSlot = slot;
        g_pasteBufferState.visibleLines = CountPasteBufferVisibleLines(g_pasteBuffers[slot]);
        SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, g_pasteBuffers[slot].c_str());
        UpdatePasteBuffersDialogText(hwnd);
        FocusPasteBufferEdit(hwnd, false);
        return true;
    }

    if (command == "b" || command == "paste" || command == "pasteone") {
        g_pasteBufferState.stage = PasteBufferStage::PasteSelect;
        UpdatePasteBuffersDialogText(hwnd);
        return true;
    }

    if (command == "n" || command == "zone" || command == "zones") {
        g_pasteBufferState.stage = PasteBufferStage::ZoneCommand;
        UpdatePasteBuffersDialogText(hwnd);
        return true;
    }

    SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE, L"");
    SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BUFFER_STATUS,
        L"Use Z/X/C/V to edit, B to paste one, or N for zones. Esc cancels.");
    FocusPasteBufferEdit(hwnd);
    return true;
}

void StartPasteBuffers() {
    if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg)) ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);
    if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) CloseSaveCursorDialog(g_hwndSaveDlg);
    if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) CloseRecordKeysDialog(g_hwndRecordDlg);
    if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) ResetCirclePlacement(true);
    if (g_hwndClickDlg && IsWindow(g_hwndClickDlg)) CloseClickActionDialog(g_hwndClickDlg);
    if (g_hwndViewTogglesDlg && IsWindow(g_hwndViewTogglesDlg)) CloseViewTogglesDialog(g_hwndViewTogglesDlg);

    g_pasteBufferState = PasteBufferState{};
    GetCursorPos(&g_pasteBufferState.cancelOnMouseMoveFrom);
    g_pasteBufferState.openedTickMs = GetTickCount();

    HWND owner = (g_hwndCoordPanel && IsWindow(g_hwndCoordPanel) && IsWindowVisible(g_hwndCoordPanel))
        ? g_hwndCoordPanel
        : nullptr;
    g_hwndPasteBuffersDlg = CreateDialogParamW(g_hInst,
        MAKEINTRESOURCE(IDD_PASTE_BUFFERS), owner,
        (DLGPROC)PasteBuffersDlgProc, 0);
    if (g_hwndPasteBuffersDlg) {
        ShowWindow(g_hwndPasteBuffersDlg, SW_SHOW);
        SetWindowPos(g_hwndPasteBuffersDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hwndPasteBuffersDlg);
        UpdatePasteBuffersDialogText(g_hwndPasteBuffersDlg);
        FocusPasteBufferEdit(g_hwndPasteBuffersDlg);
    }
}

void StartPasteBuffersZoneStore(POINT start, POINT end) {
    StartPasteBuffers();
    if (!g_hwndPasteBuffersDlg || !IsWindow(g_hwndPasteBuffersDlg)) return;

    g_pasteBufferState.stage = PasteBufferStage::ZoneCommand;
    g_pasteBufferState.pendingZoneFromCircle = true;
    g_pasteBufferState.pendingZoneStart = start;
    g_pasteBufferState.pendingZoneEnd = end;
    g_pasteBufferState.activeZoneSlot = -1;
    g_pasteBufferState.activeZoneFlow = -1;
    g_pasteBufferState.visibleLines = 1;

    SetDlgItemTextW(g_hwndPasteBuffersDlg, IDC_EDIT_PASTE_BUFFER_VALUE,
        FormatZoneBufferCoordinates(start, end).c_str());
    UpdatePasteBuffersDialogText(g_hwndPasteBuffersDlg);
    FocusPasteBufferEdit(g_hwndPasteBuffersDlg);
}

static void AddRecordStepToList(HWND hwnd, const std::vector<std::string>& step) {
    HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
    if (!hList || step.empty()) return;

    std::wstringstream ss;
    ss << L"Step " << g_recordedSteps.size() << L": ";
    for (size_t i = 0; i < step.size(); i++) {
        if (i > 0) ss << L" + ";
        ss << Utf8ToWString(step[i]);
    }
    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
    int cnt = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
    if (cnt > 0) SendMessage(hList, LB_SETTOPINDEX, cnt - 1, 0);
}

static bool FlushCurrentRecordStep(HWND hwnd) {
    if (g_currentStepKeys.empty()) return false;
    g_recordedSteps.push_back(g_currentStepKeys);
    AddRecordStepToList(hwnd, g_currentStepKeys);
    g_currentStepKeys.clear();
    return true;
}

static void CloseRecordKeysDialog(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, RECORD_KEYS_WATCH_TIMER);
    g_recordingKeys = false;
    g_waitingForKey = false;
    g_hwndRecordKeysDlg = nullptr;
    g_recordExtraActions.clear();
    g_recordKeysState = RecordKeysState{};
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    g_hwndRecordDlg = nullptr;
}

static void UpdateRecordKeysDialogText(HWND hwnd) {
    if (!hwnd) return;
    if (g_recordKeysState.stage == RecordKeysStage::Name) {
        SetDlgItemTextW(hwnd, IDC_STATIC_SEQ_NAME, L"Sequence name, then press Enter");
        SetDlgItemTextW(hwnd, IDC_STATIC_SEQ_DELAY, L"Delay (ms)");
        SetDlgItemTextW(hwnd, IDC_STATIC_STATUS,
            L"Enter sequence name/delay, then press Enter. Before Enter: moving mouse closes this HUD. Esc cancels.");
        SetDlgItemTextW(hwnd, IDOK, L"&Enter");
        SetDlgItemTextW(hwnd, IDC_BTN_CAPTURE, L"Capture Key");
        SetDlgItemTextW(hwnd, IDC_BTN_MANUAL, L"Manual Input");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_PASTE, L"Add Text/Paste");
        SetDlgItemTextW(hwnd, IDCANCEL, L"Cancel");
        SetFocus(GetDlgItem(hwnd, IDC_EDIT_SEQ_NAME));
    } else if (g_recordKeysState.stage == RecordKeysStage::Command) {
        SetDlgItemTextW(hwnd, IDC_STATIC_SEQ_NAME, L"Type Z Capture, X Manual, C Paste mode, Enter Write, or V Cancel");
        SetDlgItemTextW(hwnd, IDC_STATIC_RECORDED_FLOW, L"Recorded flow");
        SetDlgItemTextW(hwnd, IDC_STATIC_RECORD_STATUS_LABEL, L"Status");
        std::wstringstream ss;
        ss << L"Z Capture  |  X Manual  |  C Paste mode  |  Enter Write  |  V Cancel"
           << L"    steps: " << g_recordedSteps.size()
           << L" paste: " << g_recordExtraActions.size();
        SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, ss.str().c_str());
        SetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, L"");
        SetDlgItemTextW(hwnd, IDOK, L"&Write");
        SetDlgItemTextW(hwnd, IDC_BTN_CAPTURE, L"&Z Capture");
        SetDlgItemTextW(hwnd, IDC_BTN_MANUAL, L"&X Manual");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_PASTE, L"&C Queue Paste");
        SetDlgItemTextW(hwnd, IDCANCEL, L"&V Cancel");
        SetFocus(GetDlgItem(hwnd, IDC_EDIT_SEQ_NAME));
    } else {
        SetDlgItemTextW(hwnd, IDC_STATIC_RECORDED_FLOW, L"Recorded flow");
        SetDlgItemTextW(hwnd, IDC_STATIC_PASTE_BLOCK, L"Typed / paste block");
        SetDlgItemTextW(hwnd, IDC_STATIC_RECORD_STATUS_LABEL, L"Status");
        SetDlgItemTextW(hwnd, IDC_STATIC_STATUS,
            L"Paste mode: paste/type text, Shift+Enter inserts a newline, Enter queues the paste block.");
        SetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, L"");
        SetDlgItemTextW(hwnd, IDOK, L"&Write");
        SetDlgItemTextW(hwnd, IDC_BTN_ADD_PASTE, L"&C Queue Paste");
        SetDlgItemTextW(hwnd, IDCANCEL, L"&V Cancel");
        SetFocus(GetDlgItem(hwnd, IDC_EDIT_PASTE_TEXT));
    }
    LayoutRecordKeysDialog(hwnd);
}

static bool AdvanceRecordKeysToCommand(HWND hwnd) {
    std::wstring name = TrimWide(DialogText(hwnd, IDC_EDIT_SEQ_NAME));
    if (name.empty()) {
        MessageBoxW(hwnd, L"Please enter a name for this key sequence.",
                   L"Name Required", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::wstring delayText = TrimWide(DialogText(hwnd, IDC_EDIT_DELAY));
    int stepDelay = 200;
    if (!delayText.empty()) stepDelay = _wtoi(delayText.c_str());
    if (stepDelay < 10) stepDelay = 10;

    g_recordKeysState.name = name;
    g_recordKeysState.stepDelay = stepDelay;
    g_recordKeysState.stage = RecordKeysStage::Command;
    UpdateRecordKeysDialogText(hwnd);
    return true;
}

static bool QueueRecordPasteBlock(HWND hwnd) {
    std::wstring text = DialogText(hwnd, IDC_EDIT_PASTE_TEXT);
    if (text.empty()) {
        MessageBoxW(hwnd, L"Type or paste text first.",
                   L"No Text", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::string content = WStringToUtf8(text);
    std::string baseName = WStringToUtf8(g_recordKeysState.name);
    if (baseName.empty()) baseName = "paste_block";
    std::string contentId = baseName + "_" + std::to_string(g_recordExtraActions.size() + 1);
    g_recordExtraActions.push_back(BuildPasteActionJson(contentId, content));

    HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
    if (hList) {
        std::wstring preview = text;
        size_t newline = preview.find_first_of(L"\r\n");
        if (newline != std::wstring::npos) preview = preview.substr(0, newline);
        if (preview.size() > 64) preview = preview.substr(0, 61) + L"...";
        std::wstringstream ss;
        ss << L"Paste " << g_recordExtraActions.size() << L": " << preview;
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
        int cnt = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
        if (cnt > 0) SendMessage(hList, LB_SETTOPINDEX, cnt - 1, 0);
    }
    SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_TEXT, L"");
    g_recordKeysState.stage = RecordKeysStage::Command;
    UpdateRecordKeysDialogText(hwnd);
    return true;
}

static void QueueManualRecordSteps(HWND hwnd) {
    g_manualSteps.clear();
    FlushCurrentRecordStep(hwnd);
    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCE(IDD_MANUAL_KEYS),
                       hwnd, (DLGPROC)ManualKeysDlgProc, 0) == IDOK) {
        for (const auto& step : g_manualSteps) {
            if (!step.empty()) {
                g_recordedSteps.push_back(step);
                AddRecordStepToList(hwnd, step);
            }
        }
    }
    g_manualSteps.clear();
    UpdateRecordKeysDialogText(hwnd);
}

static bool WriteRecordKeyActions(HWND hwnd) {
    FlushCurrentRecordStep(hwnd);
    if (g_recordedSteps.empty() && g_recordExtraActions.empty()) {
        MessageBoxW(hwnd, L"No keys or paste blocks recorded.",
                   L"Nothing to Save", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (g_recordKeysState.name.empty()) {
        MessageBoxW(hwnd, L"Please enter a name for this key sequence.",
                   L"Name Required", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::string name = EscapeJsonString(WStringToUtf8(g_recordKeysState.name));
    bool ok = true;
    if (!g_recordedSteps.empty()) {
        ok = AppendActionAndWait(BuildKeysSequenceActionJson(name, g_recordedSteps, g_recordKeysState.stepDelay));
    }
    for (const auto& action : g_recordExtraActions) {
        if (ok) ok = AppendActionAndWait(action);
    }
    if (!ok) {
        MessageBoxW(hwnd, L"Failed to save to clicksession.txt.",
                   L"Error", MB_OK | MB_ICONERROR);
        return false;
    }
    CloseRecordKeysDialog(hwnd);
    return true;
}

static bool RunRecordKeysCommand(HWND hwnd, const std::wstring& rawCommand) {
    std::string command = CompactLower(rawCommand);
    if (command == "v" || command == "cancel" || command == "esc") {
        CloseRecordKeysDialog(hwnd);
        return true;
    }
    if (g_recordKeysState.stage == RecordKeysStage::Name) {
        return AdvanceRecordKeysToCommand(hwnd);
    }
    if (command.empty() || command == "write" || command == "enter") {
        return WriteRecordKeyActions(hwnd);
    }
    if (command == "z" || command == "capture") {
        g_currentStepKeys.clear();
        g_waitingForKey = true;
        SetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, L"");
        SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, L"Capture armed. Press key or combo now.");
        return true;
    }
    if (command == "x" || command == "manual") {
        QueueManualRecordSteps(hwnd);
        return true;
    }
    if (command == "c" || command == "paste") {
        g_recordKeysState.stage = RecordKeysStage::Paste;
        UpdateRecordKeysDialogText(hwnd);
        return true;
    }
    return false;
}

static void UpdateCircleDialogText(HWND hwnd) {
    if (!hwnd) return;

    switch (g_circlePlacement.stage) {
        case CircleStage::Distance:
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_PROMPT, L"Distance in Px");
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_HINT,
                L"Type 316 for radius, -20i+20j for pixel offset, or 316 @ -2i+2j to scale a direction.");
            break;
        case CircleStage::Mode:
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_PROMPT, L"Radians or Degrees?");
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_HINT,
                L"Press R or D. Zero is the far-right X axis.");
            break;
        case CircleStage::Angle:
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_PROMPT,
                g_circlePlacement.angleMode == CircleAngleMode::Radians ? L"Radians: use pi syntax" : L"Degrees");
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_HINT,
                g_circlePlacement.angleMode == CircleAngleMode::Radians ?
                    L"Examples: pi, 2pi, 5pi/6, pi/2. Press Enter to confirm." :
                    L"Examples: 0, 90, 180, 360, 720. Press Enter to confirm.");
            break;
        case CircleStage::Confirm:
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_PROMPT, L"Confirm target");
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_HINT,
                g_circlePlacement.zoneMode ?
                    L"Press Z move | X append | C circle | V EndZone" :
                    L"Press Z move | X append | C circle | V ZoneMake");
            break;
        case CircleStage::ZoneAction:
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_PROMPT, L"Zone action");
            SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_HINT,
                L"Press Z snip+copy | X text | C single-line | V zone+copy | B buffer zone");
            break;
        default:
            break;
    }

    SetDlgItemTextW(hwnd, IDC_STATIC_CIRCLE_STATUS, g_circlePlacement.status.c_str());

    BOOL enableFinal = g_circlePlacement.stage == CircleStage::Confirm && g_circlePlacement.targetValid;
    BOOL enableZoneAction = g_circlePlacement.stage == CircleStage::ZoneAction;
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_CIRCLE_MOVE_NOW), enableFinal || enableZoneAction);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_CIRCLE_APPEND_COORD), enableFinal || enableZoneAction);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_CIRCLE_NEXT_CIRCLE), enableFinal || enableZoneAction);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_CIRCLE_ZONE_ACTION), enableFinal || enableZoneAction);
    if (g_circlePlacement.stage == CircleStage::ZoneAction) {
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_MOVE_NOW, L"&Z Snip+Copy");
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_APPEND_COORD, L"&X Text");
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_NEXT_CIRCLE, L"&C SingleLine");
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_ZONE_ACTION, L"&V Zone+Copy");
    } else {
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_MOVE_NOW, L"&Z Move Cursor");
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_APPEND_COORD, L"&X Append Coord");
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_NEXT_CIRCLE, L"&C Circle Here");
        SetDlgItemTextW(hwnd, IDC_BTN_CIRCLE_ZONE_ACTION,
            g_circlePlacement.zoneMode ? L"&V EndZone" : L"&V ZoneMake");
    }
    LayoutCircleDialog(hwnd);
}

static void SetCircleStatus(const std::wstring& value) {
    g_circlePlacement.status = value;
    if (g_hwndCircleDlg) {
        SetDlgItemTextW(g_hwndCircleDlg, IDC_STATIC_CIRCLE_STATUS, value.c_str());
    }
}

static void SetCircleStage(CircleStage stage) {
    g_circlePlacement.stage = stage;
    if (g_hwndCircleDlg && stage != CircleStage::Idle) {
        g_pendingCircleInputOverride.clear();
        g_circlePlacement.lastInput.clear();
        SetDlgItemTextW(g_hwndCircleDlg, IDC_EDIT_CIRCLE_VALUE, L"");
        SetFocus(GetDlgItem(g_hwndCircleDlg, IDC_EDIT_CIRCLE_VALUE));
    }
    UpdateCircleDialogText(g_hwndCircleDlg);
}

static void PreviewCurrentCircle(bool showTarget) {
    std::wstring label;
    if (g_circlePlacement.stage == CircleStage::Distance && !showTarget) {
        label = L"distance " + std::to_wstring((int)std::lround(g_circlePlacement.radius)) + L" px";
    } else {
        label = L"X " + std::to_wstring(g_circlePlacement.target.x) +
                L" Y " + std::to_wstring(g_circlePlacement.target.y);
    }
    SetCirclePreview(
        g_circlePlacement.origin,
        showTarget ? g_circlePlacement.target : PointOnCircle(g_circlePlacement.origin, g_circlePlacement.radius, 0.0),
        g_circlePlacement.radius,
        label,
        showTarget ? FormatAngleLabel(g_circlePlacement.thetaRadians) : L"",
        0,
        showTarget
    );
}

static bool UpdatePreviewFromInput(HWND hwnd) {
    wchar_t value[512] = {};
    GetDlgItemTextW(hwnd, IDC_EDIT_CIRCLE_VALUE, value, 512);
    std::wstring input = TrimWide(value);
    if (!g_pendingCircleInputOverride.empty()) {
        input = TrimWide(g_pendingCircleInputOverride);
        g_pendingCircleInputOverride.clear();
    }
    g_circlePlacement.lastInput = input;
    GetCursorPos(&g_circlePlacement.cancelOnMouseMoveFrom);
    g_circlePlacement.openedTickMs = GetTickCount();

    if (g_circlePlacement.stage == CircleStage::Distance) {
        g_circlePlacement.targetValid = false;
        VectorParseResult vectorResult;
        if (TryParseVectorInput(input, g_circlePlacement.origin, vectorResult)) {
            g_circlePlacement.radius = vectorResult.radius;
            g_circlePlacement.thetaRadians = vectorResult.theta;
            g_circlePlacement.target = vectorResult.target;
            g_circlePlacement.targetValid = true;
            SetCirclePreview(
                g_circlePlacement.origin,
                g_circlePlacement.target,
                g_circlePlacement.radius,
                vectorResult.label,
                FormatAngleLabel(g_circlePlacement.thetaRadians),
                0,
                true
            );
            SetCircleStatus(L"Vector accepted. Press Enter to confirm, or keep editing.");
            return true;
        }

        double radius = 0.0;
        if (TryParseNumberOnly(input, radius) && radius > 0) {
            g_circlePlacement.radius = radius;
            g_circlePlacement.thetaRadians = 0.0;
            g_circlePlacement.target = PointOnCircle(g_circlePlacement.origin, radius, 0.0);
            g_circlePlacement.targetValid = false;
            PreviewCurrentCircle(false);
            SetCircleStatus(L"Circle preview updated. Press Enter, then choose R or D.");
            return true;
        }
        HideCirclePreview();
        SetCircleStatus(input.empty() ? L"Enter a distance in pixels." : L"Could not parse distance/vector yet.");
        return false;
    }

    if (g_circlePlacement.stage == CircleStage::Angle) {
        g_circlePlacement.targetValid = false;
        double theta = 0.0;
        bool ok = false;
        if (g_circlePlacement.angleMode == CircleAngleMode::Radians) {
            ok = TryParseAngleRadians(input, theta);
        } else if (g_circlePlacement.angleMode == CircleAngleMode::Degrees) {
            double degrees = 0.0;
            ok = TryParseNumberOnly(input, degrees);
            theta = degrees * 3.14159265358979323846 / 180.0;
        }

        if (ok) {
            g_circlePlacement.thetaRadians = NormalizeRadians(theta);
            g_circlePlacement.target = PointOnCircle(g_circlePlacement.origin, g_circlePlacement.radius, g_circlePlacement.thetaRadians);
            g_circlePlacement.targetValid = true;
            PreviewCurrentCircle(true);
            SetCircleStatus(L"Target preview updated. Press Enter to confirm.");
            return true;
        }
        HideCirclePreview();
        SetCircleStatus(input.empty() ? L"Enter the angle value." : L"Could not parse angle yet.");
        return false;
    }

    return false;
}

static void ResetCirclePlacement(bool closeDialog) {
    HideCirclePreview();
    g_circlePlacement = CirclePlacementState{};
    if (closeDialog && g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) {
        HWND hwnd = g_hwndCircleDlg;
        g_hwndCircleDlg = nullptr;
        DestroyWindow(hwnd);
    }
}

static bool AppendCircleCoordinate() {
    if (!g_circlePlacement.targetValid) return false;
    std::string id = "circle_" + std::to_string(g_circlePlacement.target.x) + "_" + std::to_string(g_circlePlacement.target.y);

    std::string json;
    json += "        {\n";
    json += "            \"cursorMovsId\": \"" + id + "\",\n";
    json += "            \"cursormovsmap\": [\n";
    json += "                [\n";
    json += "                    0,\n";
    json += "                    1000,\n";
    json += "                    " + std::to_string(g_circlePlacement.target.x) + ",\n";
    json += "                    " + std::to_string(g_circlePlacement.target.y) + "\n";
    json += "                ]\n";
    json += "            ],\n";
    json += "            \"loop\": 1,\n";
    json += "            \"optionalkeysstroke\": [\n";
    json += "            ],\n";
    json += "            \"type\": \"cursormovements\"\n";
    json += "        },";
    return AppendActionAndWait(json);
}

static void StartNextCircleFromTarget() {
    POINT target = g_circlePlacement.target;
    POINT cancelPt = g_circlePlacement.cancelOnMouseMoveFrom;
    bool zoneMode = g_circlePlacement.zoneMode;
    bool zoneHasStart = g_circlePlacement.zoneHasStart;
    POINT zoneStart = g_circlePlacement.zoneStart;
    g_circlePlacement = CirclePlacementState{};
    g_circlePlacement.active = true;
    g_circlePlacement.stage = CircleStage::Distance;
    g_circlePlacement.origin = target;
    g_circlePlacement.zoneMode = zoneMode;
    g_circlePlacement.zoneHasStart = zoneHasStart;
    g_circlePlacement.zoneStart = zoneStart;
    g_circlePlacement.cancelOnMouseMoveFrom = cancelPt;
    g_circlePlacement.openedTickMs = GetTickCount();
    g_circlePlacement.status = zoneMode ?
        L"Zone mode: choose the opposite corner, then press V EndZone." :
        L"New circle center set at the last target. Enter distance in Px.";
    if (zoneMode && zoneHasStart) {
        SetZonePreview(zoneStart, target);
    } else {
        HideCirclePreview();
    }
    SetCircleStage(CircleStage::Distance);
}

void StartCirclePlacement() {
    if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg)) ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);
    if (!g_hwndCircleDlg || !IsWindow(g_hwndCircleDlg)) {
        HWND owner = g_hwndCoordPanel && IsWindow(g_hwndCoordPanel) ? g_hwndCoordPanel : g_hwndCrosshair;
        g_hwndCircleDlg = CreateDialogParamW(g_hInst,
            MAKEINTRESOURCE(IDD_CIRCLE_PLACER), owner,
            (DLGPROC)CirclePlacerDlgProc, 0);
    }
    if (g_hwndCircleDlg) {
        ShowWindow(g_hwndCircleDlg, SW_SHOW);
        SetWindowPos(g_hwndCircleDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hwndCircleDlg);
    }
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

static std::string BuildCursorMovementActionJson(
    const std::string& name,
    const std::vector<std::vector<int>>& coords,
    const std::vector<std::string>& optionalKeys) {
    std::string json;
    json += "        {\n";
    json += "            \"cursorMovsId\": \"" + name + "\",\n";
    json += "            \"cursormovsmap\": [\n";
    for (size_t i = 0; i < coords.size(); i++) {
        json += "                [\n";
        json += "                    " + std::to_string(coords[i][0]) + ",\n";
        json += "                    " + std::to_string(coords[i][1]) + ",\n";
        json += "                    " + std::to_string(coords[i][2]) + ",\n";
        json += "                    " + std::to_string(coords[i][3]) + "\n";
        json += "                ]";
        if (i + 1 < coords.size()) json += ",";
        json += "\n";
    }
    json += "            ],\n";
    json += "            \"loop\": 1,\n";
    json += "            \"optionalkeysstroke\": [\n";
    for (size_t i = 0; i < optionalKeys.size(); ++i) {
        json += "                \"" + EscapeJsonString(optionalKeys[i]) + "\"";
        if (i + 1 < optionalKeys.size()) json += ",";
        json += "\n";
    }
    json += "            ],\n";
    json += "            \"type\": \"cursormovements\"\n";
    json += "        },";
    return json;
}

static std::string BuildKeysSequenceActionJson(
    const std::string& name,
    const std::vector<std::vector<std::string>>& steps,
    int stepDelay,
    int releaseAfterMs) {
    if (releaseAfterMs < 1) releaseAfterMs = 1;
    std::string json;
    json += "        {\n";
    json += "            \"keysSeqId\": \"" + name + "\",\n";
    json += "            \"keysmap\": {\n";
    for (size_t i = 0; i < steps.size(); i++) {
        int tOff = (int)(i * stepDelay);
        json += "                \"" + std::to_string(tOff) + "\": [\n";
        json += "                    " + std::to_string(releaseAfterMs) + ",\n";
        json += "                    [\n";
        for (size_t j = 0; j < steps[i].size(); j++) {
            json += "                        \"" + EscapeJsonString(steps[i][j]) + "\"";
            if (j + 1 < steps[i].size()) json += ",";
            json += "\n";
        }
        json += "                    ]\n";
        json += "                ]";
        if (i + 1 < steps.size()) json += ",";
        json += "\n";
    }
    json += "            },\n";
    json += "            \"loop\": 1,\n";
    json += "            \"type\": \"keyssequence\"\n";
    json += "        },";
    return json;
}

static std::string BuildMouseClickActionJson(const std::string& name, const std::string& mouseToken, int releaseAfterMs) {
    std::vector<std::vector<std::string>> clickSteps = {{mouseToken}};
    return BuildKeysSequenceActionJson(name, clickSteps, 200, releaseAfterMs);
}

static std::string BuildLeftClickActionJson(const std::string& name) {
    return BuildMouseClickActionJson(name, "LEFT_MOUSE", 100);
}

static std::string BuildPasteActionJson(const std::string& contentId, const std::string& content) {
    std::string json;
    json += "        {\n";
    json += "            \"content\": \"" + EscapeJsonString(content) + "\",\n";
    json += "            \"contentId\": \"" + EscapeJsonString(contentId) + "\",\n";
    json += "            \"loop\": 1,\n";
    json += "            \"type\": \"paste\"\n";
    json += "        },";
    return json;
}

static std::string StripTrailingActionComma(std::string actionJson) {
    while (!actionJson.empty() && isspace((unsigned char)actionJson.back())) {
        actionJson.pop_back();
    }
    if (!actionJson.empty() && actionJson.back() == ',') {
        actionJson.pop_back();
    }
    return actionJson;
}

static std::string BuildTasketDocumentJson(
    const std::string& description,
    const std::vector<std::string>& actionJsons,
    int httpDelay,
    bool includeHttpDelay = true) {
    std::string json;
    json += "{\n";
    json += "  \"docType\": \"ScheduleTask File\",\n";
    json += "  \"description\": \"" + EscapeJsonString(description) + "\",\n";
    if (includeHttpDelay) {
        json += "  \"http_delay\": " + std::to_string(httpDelay) + ",\n";
    }
    json += "  \"actions\": [\n";
    for (size_t i = 0; i < actionJsons.size(); ++i) {
        json += StripTrailingActionComma(actionJsons[i]);
        if (i + 1 < actionJsons.size()) json += ",";
        json += "\n";
    }
    json += "  ]\n";
    json += "}";
    return json;
}

static std::string BuildWaitActionJson(double durationSeconds) {
    std::ostringstream ss;
    ss << "        {\n";
    ss << "            \"duration\": " << std::fixed << std::setprecision(3) << durationSeconds << ",\n";
    ss << "            \"type\": \"wait\"\n";
    ss << "        },";
    return ss.str();
}

static bool HttpPostLocalTasket(const std::wstring& path, const std::string& body, std::string* response, std::wstring* status) {
    if (response) response->clear();

    HINTERNET hSession = WinHttpOpen(L"Macrohelp/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) {
        if (status) *status = L"WinHTTP session failed.";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 7777, 0);
    if (!hConnect) {
        if (status) *status = L"Could not connect to Tasket HTTP daemon on 127.0.0.1:7777.";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        if (status) *status = L"Could not open Tasket HTTP request.";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD timeoutMs = 2500;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    std::wstring headers = L"Content-Type: application/json\r\n";
    BOOL sent = WinHttpSendRequest(hRequest,
                                   headers.c_str(), (DWORD)-1L,
                                   (LPVOID)body.data(), (DWORD)body.size(),
                                   (DWORD)body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        if (status) *status = L"Tasket HTTP request failed. Is tasket-httpd.exe running?";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode,
                        &statusCodeSize,
                        WINHTTP_NO_HEADER_INDEX);

    std::string out;
    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) break;
        chunk.resize(read);
        out += chunk;
    } while (available > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response) *response = out;
    if (statusCode < 200 || statusCode >= 300) {
        if (status) {
            std::wstringstream ss;
            ss << L"Tasket HTTP returned " << statusCode << L".";
            *status = ss.str();
        }
        return false;
    }

    if (status) *status = L"Tasket HTTP request accepted.";
    return true;
}

static bool HttpGetLocalTasket(const std::wstring& path, std::string* response, std::wstring* status) {
    if (response) response->clear();

    HINTERNET hSession = WinHttpOpen(L"Macrohelp/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) {
        if (status) *status = L"WinHTTP session failed.";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 7777, 0);
    if (!hConnect) {
        if (status) *status = L"Could not connect to Tasket HTTP daemon on 127.0.0.1:7777.";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        if (status) *status = L"Could not open Tasket HTTP request.";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD timeoutMs = 2500;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    BOOL sent = WinHttpSendRequest(hRequest,
                                   WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0,
                                   0, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        if (status) *status = L"Tasket HTTP request failed. Is tasket-httpd.exe running?";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode,
                        &statusCodeSize,
                        WINHTTP_NO_HEADER_INDEX);

    std::string out;
    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) break;
        chunk.resize(read);
        out += chunk;
    } while (available > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response) *response = out;
    if (statusCode < 200 || statusCode >= 300) {
        if (status) {
            std::wstringstream ss;
            ss << L"Tasket HTTP returned " << statusCode << L".";
            *status = ss.str();
        }
        return false;
    }

    if (status) *status = L"Tasket HTTP request accepted.";
    return true;
}

static bool ScheduleTasketTempTask(const std::string& taskName, const std::string& taskJson, int delaySeconds, std::wstring* status, bool run, bool cleanup) {
    std::string body;
    body += "{";
    body += "\"name\":\"" + EscapeJsonString(taskName) + "\",";
    body += "\"run\":";
    body += run ? "true," : "false,";
    body += "\"cleanup\":";
    body += cleanup ? "true," : "false,";
    body += "\"delay\":" + std::to_string(delaySeconds < 0 ? 0 : delaySeconds) + ",";
    body += "\"loop\":1,";
    body += "\"task\":" + taskJson;
    body += "}";

    std::string response;
    bool ok = HttpPostLocalTasket(L"/temp-task", body, &response, status);
    if (ok && status) {
        double taskNumber = 0.0;
        if (JsonNumberValue(response, "task_number", taskNumber)) {
            g_lastTasketTaskNumber = (int)taskNumber;
        }
        *status = Utf8ToWString(JsonStringValue(response, "message"));
        if (status->empty()) *status = L"Temporary Tasket task scheduled.";
    }
    return ok;
}

static bool ScheduleTasketCursorMove(const POINT& target, int moveMs, int delaySeconds, std::wstring* status) {
    (void)moveMs;
    std::string actions;
    if (!LoadCursorMoveActions(target, &actions, status)) {
        return false;
    }

    std::string task = BuildTasketDocumentJson(
        "Macrohelp temporary circle cursor move.",
        {actions},
        delaySeconds);
    return ScheduleTasketTempTask("Macrohelp circle move temp", task, delaySeconds, status);
}

static bool ScheduleTasketClick(int buttonIndex, int releaseAfterMs, int delaySeconds, std::wstring* status) {
    if (releaseAfterMs < 1) releaseAfterMs = 100;
    if (releaseAfterMs > 600000) releaseAfterMs = 600000;
    std::string slug = ClickButtonSlug(buttonIndex);
    std::string action = BuildMouseClickActionJson(
        "macrohelp_" + slug + "_click_temp",
        ClickButtonToken(buttonIndex),
        releaseAfterMs);
    std::string task = BuildTasketDocumentJson(
        "Macrohelp generated " + slug + " click.",
        {action, BuildWaitActionJson(0.1)},
        delaySeconds);
    return ScheduleTasketTempTask("macrohelp_" + slug + "_click_temp", task, delaySeconds, status);
}

static bool ScheduleTasketPasteBuffer(int slotIndex, const std::wstring& text, std::wstring* status) {
    if (slotIndex < 0 || slotIndex > 3) {
        if (status) *status = L"Paste buffer slot out of range.";
        return false;
    }

    char slotKey = (char)PasteBufferSlotKey(slotIndex);
    std::string slotName;
    slotName.push_back((char)std::tolower((unsigned char)slotKey));
    std::string content = WStringToUtf8(text);
    std::string task = BuildTasketDocumentJson(
        "Macrohelp paste buffer " + slotName + ".",
        {
            BuildWaitActionJson(0.1),
            BuildLeftClickActionJson("MacrohelpPasteBufferFocusClick"),
            BuildPasteActionJson("macrohelp_buffer_" + slotName, content),
            BuildWaitActionJson(0.1)
        },
        1);
    return ScheduleTasketTempTask("macrohelp_paste_buffer_" + slotName + "_temp", task, 1, status);
}

static bool ScheduleTasketPasteSequence(const std::vector<int>& slots, std::wstring* status) {
    if (slots.empty()) {
        if (status) *status = L"Paste sequence is empty.";
        return false;
    }

    std::vector<std::string> actions;
    std::string slug;
    actions.push_back(BuildWaitActionJson(0.1));
    actions.push_back(BuildLeftClickActionJson("MacrohelpPasteSequenceFocusClick"));
    actions.push_back(BuildWaitActionJson(0.08));

    for (size_t i = 0; i < slots.size(); ++i) {
        int slot = slots[i];
        if (slot < 0 || slot > 3 || g_pasteBuffers[slot].empty()) {
            if (status) *status = L"Paste sequence contains an empty or invalid buffer.";
            return false;
        }
        char slotKey = (char)PasteBufferSlotKey(slot);
        char lowerKey = (char)std::tolower((unsigned char)slotKey);
        slug.push_back(lowerKey);
        std::string keyName(1, lowerKey);
        actions.push_back(BuildPasteActionJson(
            "macrohelp_sequence_buffer_" + keyName + "_" + std::to_string(i + 1),
            WStringToUtf8(g_pasteBuffers[slot])));
        if (i + 1 < slots.size()) {
            actions.push_back(BuildWaitActionJson(0.08));
        }
    }
    actions.push_back(BuildWaitActionJson(0.1));

    std::string task = BuildTasketDocumentJson(
        "Macrohelp paste buffer sequence " + slug + ".",
        actions,
        1);
    return ScheduleTasketTempTask("macrohelp_paste_sequence_" + slug + "_temp", task, 1, status);
}

static bool LoadZoneFlowActionList(
    int flowIndex,
    POINT start,
    POINT end,
    std::vector<std::string>& actions,
    std::string* desc,
    std::string* taskName,
    std::wstring* status) {
    actions.clear();
    std::string openActions;
    std::string zoneActions;
    std::string copyActions;
    std::string focusMoveActions;

    if (!LoadZoneActions(start, end, &zoneActions, status)) {
        return false;
    }

    if (flowIndex == 0) {
        if (!LoadTemplateActions(L"Open snipping tool example.scht", &openActions, status)) return false;
        if (desc) *desc = "Macrohelp temporary Snipping Tool zone capture.";
        if (taskName) *taskName = "Macrohelp zone snip copy temp";
        actions = {openActions, zoneActions};
    } else if (flowIndex == 1) {
        if (!LoadTemplateActions(L"Open snipping text example.scht", &openActions, status)) return false;
        if (!LoadCursorMoveActions(start, &focusMoveActions, status)) return false;
        if (desc) *desc = "Macrohelp temporary Snipping Text zone drag.";
        if (taskName) *taskName = "Macrohelp zone text temp";
        actions = {focusMoveActions, BuildLeftClickActionJson("MacrohelpTextFocusClick"), BuildWaitActionJson(0.2), openActions, zoneActions};
    } else if (flowIndex == 2) {
        if (!LoadTemplateActions(L"Open snipping text example singleline.scht", &openActions, status)) return false;
        if (!LoadCursorMoveActions(start, &focusMoveActions, status)) return false;
        if (desc) *desc = "Macrohelp temporary Snipping Text single-line zone drag.";
        if (taskName) *taskName = "Macrohelp zone text singleline temp";
        actions = {focusMoveActions, BuildLeftClickActionJson("MacrohelpTextFocusClick"), BuildWaitActionJson(0.2), openActions, zoneActions};
    } else if (flowIndex == 3) {
        if (!LoadTemplateActions(L"Ctrl C example.scht", &copyActions, status)) return false;
        if (desc) *desc = "Macrohelp temporary zone drag plus copy.";
        if (taskName) *taskName = "Macrohelp zone copy temp";
        actions = {zoneActions, copyActions};
    } else {
        if (status) *status = L"Unknown zone flow.";
        return false;
    }
    return true;
}

static bool ScheduleTasketZoneFlow(int flowIndex, POINT start, POINT end, std::wstring* status) {
    std::vector<std::string> actions;
    std::string desc;
    std::string taskName;
    if (!LoadZoneFlowActionList(flowIndex, start, end, actions, &desc, &taskName, status)) {
        return false;
    }

    std::string task = BuildTasketDocumentJson(desc, actions, 1);
    return ScheduleTasketTempTask(taskName, task, 1, status);
}

static bool ScheduleTasketZoneCaptureToPasteBuffer(int flowIndex, POINT start, POINT end, int targetSlot, std::wstring* status) {
    if (targetSlot < 0 || targetSlot > 3) {
        if (status) *status = L"Target paste buffer is invalid.";
        return false;
    }
    if (flowIndex < 1 || flowIndex > 3) {
        if (status) *status = L"Zone-to-buffer capture only supports text/copy flows X, C, or V.";
        return false;
    }

    std::vector<std::string> actions;
    std::string desc;
    std::string taskName;
    if (!LoadZoneFlowActionList(flowIndex, start, end, actions, &desc, &taskName, status)) {
        return false;
    }

    char targetKey = (char)PasteBufferSlotKey(targetSlot);
    std::vector<std::vector<std::string>> saveSteps = {
        {"SHIFT_LEFT", "ALT_LEFT", "0"},
        {std::string(1, targetKey)},
        {"CONTROL_LEFT", "A"},
        {"CONTROL_LEFT", "V"},
        {"ENTER"}
    };

    actions.push_back(BuildWaitActionJson(flowIndex == 3 ? 0.8 : 1.4));
    actions.push_back(BuildKeysSequenceActionJson(
        "MacrohelpStoreZoneClipboardInBuffer",
        saveSteps,
        360,
        100));
    actions.push_back(BuildWaitActionJson(0.2));

    std::string targetSlug(1, (char)std::tolower((unsigned char)targetKey));
    std::string task = BuildTasketDocumentJson(
        desc + " Then store clipboard in Macrohelp buffer " + targetSlug + ".",
        actions,
        1);
    return ScheduleTasketTempTask(
        "macrohelp_zone_to_buffer_" + targetSlug + "_temp",
        task,
        1,
        status);
}

static bool StopAllTasketTasks(std::wstring* status) {
    std::string response;
    bool ok = HttpPostLocalTasket(L"/stop", "{}", &response, status);
    if (ok && status) {
        *status = Utf8ToWString(JsonStringValue(response, "message"));
        if (status->empty()) *status = L"All Tasket tasks stopped.";
    }
    return ok;
}

static wchar_t RegistryVarKey(int slot) {
    static const wchar_t* keys = L"ASDF";
    if (slot < 0 || slot > 3) return L'?';
    return keys[slot];
}

static int RegistryVarIndexFromCommand(const std::string& command) {
    std::string key = LowerAscii(TrimAscii(command));
    if (key == "a") return 0;
    if (key == "s") return 1;
    if (key == "d") return 2;
    if (key == "f") return 3;
    return -1;
}

static int RegistryPointIndexFromCommand(const std::string& command) {
    std::string key = LowerAscii(TrimAscii(command));
    std::string compact;
    compact.reserve(key.size());
    for (char ch : key) {
        if (ch == '_' || ch == '-' || isspace((unsigned char)ch)) continue;
        compact.push_back(ch);
    }
    if (compact.rfind("point", 0) == 0) compact = compact.substr(5);
    else if (compact.rfind("p", 0) == 0 && compact.size() > 1) compact = compact.substr(1);
    if (compact.empty()) return -1;
    for (char ch : compact) {
        if (!isdigit((unsigned char)ch)) return -1;
    }
    int pointNumber = atoi(compact.c_str());
    if (pointNumber < 1 || pointNumber > REGISTRY_POINT_COUNT) return -1;
    return pointNumber - 1;
}

static std::string PointValueUtf8(int pointIndex) {
    if (pointIndex < 0 || pointIndex >= REGISTRY_POINT_COUNT || !g_registryPointSet[pointIndex]) return "";
    std::ostringstream ss;
    ss << g_registryPoints[pointIndex].x << "," << g_registryPoints[pointIndex].y;
    return ss.str();
}

static bool ParsePointCoordinatesLoose(const std::wstring& raw, POINT& point) {
    std::vector<int> values;
    const wchar_t* p = raw.c_str();
    while (*p) {
        wchar_t* next = nullptr;
        long value = wcstol(p, &next, 10);
        if (next != p) {
            values.push_back((int)value);
            p = next;
        } else {
            ++p;
        }
    }
    if (values.size() < 2) return false;
    point = {values[0], values[1]};
    return true;
}

static bool ResolveRegistryPointReference(const std::string& raw, POINT& point) {
    int idx = RegistryPointIndexFromCommand(raw);
    if (idx < 0 || idx >= REGISTRY_POINT_COUNT || !g_registryPointSet[idx]) return false;
    point = g_registryPoints[idx];
    return true;
}

static bool ResolveRegistryZoneReference(const std::string& raw, POINT& start, POINT& end) {
    std::string key = LowerAscii(TrimAscii(raw));
    std::string compact;
    compact.reserve(key.size());
    for (char ch : key) {
        if (ch == '_' || ch == '-' || isspace((unsigned char)ch)) continue;
        compact.push_back(ch);
    }
    if (compact.rfind("zone", 0) == 0) compact = compact.substr(4);
    int slot = PasteBufferSlotFromCommand(compact);
    if (slot < 0 || slot > 3 || !g_zoneBuffers[slot].set) return false;
    start = g_zoneBuffers[slot].start;
    end = g_zoneBuffers[slot].end;
    return true;
}

static bool ParsePointCoordinatesOrReference(const std::wstring& raw, POINT& point) {
    std::string utf8 = TrimAscii(WStringToUtf8(raw));
    if (ResolveRegistryPointReference(utf8, point)) return true;
    return ParsePointCoordinatesLoose(raw, point);
}

static bool ParseZoneCoordinatesOrPointRefs(const std::wstring& raw, POINT& start, POINT& end) {
    if (ParseZoneBufferCoordinates(raw, start, end)) return true;

    std::vector<std::string> args = SplitScriptArgs(WStringToUtf8(raw));
    if (args.size() == 1 && ResolveRegistryZoneReference(args[0], start, end)) return true;
    if (args.size() != 2) return false;
    POINT a = {}, b = {};
    if (!ResolveRegistryPointReference(args[0], a) || !ResolveRegistryPointReference(args[1], b)) return false;
    start = a;
    end = b;
    return true;
}

static std::string ZoneValueUtf8(int slot) {
    if (slot < 0 || slot > 3 || !g_zoneBuffers[slot].set) return "";
    std::ostringstream ss;
    ss << g_zoneBuffers[slot].start.x << "," << g_zoneBuffers[slot].start.y
       << "," << g_zoneBuffers[slot].end.x << "," << g_zoneBuffers[slot].end.y;
    return ss.str();
}

static bool LookupScriptTextValue(const std::string& rawKey, std::wstring& text) {
    std::string key = LowerAscii(TrimAscii(rawKey));
    int varSlot = RegistryVarIndexFromCommand(key);
    if (varSlot >= 0) {
        text = g_registryVars[varSlot];
        return true;
    }
    int pasteSlot = PasteBufferSlotFromCommand(key);
    if (pasteSlot >= 0) {
        text = g_pasteBuffers[pasteSlot];
        return true;
    }
    return false;
}

static std::string ExpandRegistryHubValueReferences(const std::string& value) {
    std::string trimmed = TrimAscii(value);
    if (trimmed.size() < 2 || trimmed[0] != '$') return value;

    int pointSlot = RegistryPointIndexFromCommand(trimmed.substr(1));
    if (pointSlot >= 0) {
        std::string pointValue = PointValueUtf8(pointSlot);
        if (!pointValue.empty()) return pointValue;
    }

    std::wstring text;
    if (LookupScriptTextValue(trimmed.substr(1), text)) return WStringToUtf8(text);
    return value;
}

static bool ResolveRegistryHubOperand(const std::string& raw, std::string& value) {
    std::string trimmed = TrimAscii(raw);
    if (trimmed.empty()) {
        value.clear();
        return true;
    }
    if (trimmed[0] == '$') {
        std::wstring text;
        if (!LookupScriptTextValue(trimmed.substr(1), text)) return false;
        value = WStringToUtf8(text);
        return true;
    }
    std::wstring text;
    if (LookupScriptTextValue(trimmed, text)) {
        value = WStringToUtf8(text);
        return true;
    }
    value = trimmed;
    return true;
}

static bool TryParseDoubleStrict(const std::string& raw, double& value) {
    std::string trimmed = TrimAscii(raw);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    value = strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str()) return false;
    while (end && *end) {
        if (!isspace((unsigned char)*end)) return false;
        ++end;
    }
    return true;
}

static std::string NormalizeTaskNameForCompare(std::string name) {
    name = LowerAscii(TrimAscii(name));
    if (name.size() > 5 && name.substr(name.size() - 5) == ".scht") {
        name.resize(name.size() - 5);
    }
    return name;
}

static bool JsonContainsTaskName(const std::string& json, const std::string& rawTaskName) {
    std::string wanted = NormalizeTaskNameForCompare(rawTaskName);
    if (wanted.empty()) return false;

    size_t pos = 0;
    while (true) {
        pos = json.find("\"name\"", pos);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos + 6);
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && isspace((unsigned char)json[pos])) ++pos;
        if (pos >= json.size() || json[pos] != '"') continue;
        ++pos;

        std::string value;
        bool escaped = false;
        for (; pos < json.size(); ++pos) {
            char c = json[pos];
            if (escaped) {
                value.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') break;
            value.push_back(c);
        }
        if (NormalizeTaskNameForCompare(value) == wanted) return true;
    }
}

static bool CompareNumericCondition(double leftNumber, const std::string& op, double rightNumber, bool& result, std::wstring* status) {
    std::string lower = LowerAscii(op);
    if (lower == "==" || lower == "=" || lower == "eq" || lower == "is") {
        result = fabs(leftNumber - rightNumber) < 0.0000001;
        return true;
    }
    if (lower == "!=" || lower == "<>" || lower == "ne" || lower == "not") {
        result = fabs(leftNumber - rightNumber) >= 0.0000001;
        return true;
    }
    if (lower == ">" || lower == "gt") {
        result = leftNumber > rightNumber;
        return true;
    }
    if (lower == ">=" || lower == "gte") {
        result = leftNumber >= rightNumber;
        return true;
    }
    if (lower == "<" || lower == "lt") {
        result = leftNumber < rightNumber;
        return true;
    }
    if (lower == "<=" || lower == "lte") {
        result = leftNumber <= rightNumber;
        return true;
    }
    if (status) *status = L"Unknown numeric comparison operator.";
    return false;
}

static bool QueryTasketTaskStatus(int taskNumber, std::string& response, std::wstring* status) {
    if (taskNumber <= 0) {
        if (status) *status = L"No Tasket task number is available yet.";
        return false;
    }
    std::wstring path = L"/check?id=" + std::to_wstring(taskNumber);
    return HttpGetLocalTasket(path, &response, status);
}

static bool EvaluateTasketDaemonCondition(const std::vector<std::string>& args, bool& result, std::wstring* status) {
    if (args.empty()) return false;
    std::string head = LowerAscii(args[0]);

    if (head == "saved-task" || head == "savedtask") {
        if (args.size() < 3) {
            if (status) *status = L"{if saved-task ...} needs a task name and exists/missing.";
            return false;
        }
        std::string response;
        if (!HttpGetLocalTasket(L"/tasks", &response, status)) return false;
        bool exists = JsonContainsTaskName(response, args[1]);
        std::string op = LowerAscii(args[2]);
        if (op == "exists" || op == "present" || op == "available") {
            result = exists;
            return true;
        }
        if (op == "missing" || op == "absent" || op == "notexists" || op == "!exists") {
            result = !exists;
            return true;
        }
        if (status) *status = L"{if saved-task ...} accepts exists or missing.";
        return false;
    }

    if (head == "saved-tasks" || head == "savedtasks") {
        if (args.size() < 4 || LowerAscii(args[1]) != "count") {
            if (status) *status = L"{if saved-tasks count ...} needs count plus a comparison.";
            return false;
        }
        std::string response;
        if (!HttpGetLocalTasket(L"/tasks", &response, status)) return false;
        double count = 0.0;
        if (!JsonNumberValue(response, "count", count)) {
            if (status) *status = L"Tasket /tasks response did not include count.";
            return false;
        }
        double right = 0.0;
        if (!TryParseDoubleStrict(args[3], right)) {
            if (status) *status = L"{if saved-tasks count ...} needs a numeric right side.";
            return false;
        }
        return CompareNumericCondition(count, args[2], right, result, status);
    }

    if (head != "task") return false;
    if (args.size() < 4) {
        if (status) *status = L"{if task ...} needs task id/last, field, and value.";
        return false;
    }

    int taskNumber = 0;
    if (LowerAscii(args[1]) == "last") taskNumber = g_lastTasketTaskNumber;
    else taskNumber = atoi(args[1].c_str());

    std::string response;
    if (!QueryTasketTaskStatus(taskNumber, response, status)) return false;
    std::string state = JsonStringValue(response, "state");
    std::string message = JsonStringValue(response, "message");
    double remaining = 0.0;
    JsonNumberValue(response, "remaining_seconds", remaining);

    if (taskNumber == g_lastTasketTaskNumber) {
        g_lastTasketTaskState = Utf8ToWString(state);
        g_lastTasketTaskMessage = Utf8ToWString(message);
    }

    std::string field = LowerAscii(args[2]);
    if (field == "is") {
        std::string expected = LowerAscii(args[3]);
        if (expected == "done" || expected == "success" || expected == "complete" || expected == "completed") expected = "finished";
        result = LowerAscii(state) == expected;
        return true;
    }
    if (field == "state") {
        if (args.size() < 5) {
            if (status) *status = L"{if task ... state ...} needs an operator and value.";
            return false;
        }
        std::string op = LowerAscii(args[3]);
        std::string expected = LowerAscii(args[4]);
        if (expected == "done" || expected == "success" || expected == "complete" || expected == "completed") expected = "finished";
        if (op == "==" || op == "=" || op == "eq" || op == "is") {
            result = LowerAscii(state) == expected;
            return true;
        }
        if (op == "!=" || op == "<>" || op == "ne" || op == "not") {
            result = LowerAscii(state) != expected;
            return true;
        }
        if (status) *status = L"{if task ... state ...} accepts == or !=.";
        return false;
    }
    if (field == "remaining" || field == "remaining_seconds") {
        if (args.size() < 5) {
            if (status) *status = L"{if task ... remaining ...} needs an operator and number.";
            return false;
        }
        double right = 0.0;
        if (!TryParseDoubleStrict(args[4], right)) {
            if (status) *status = L"{if task ... remaining ...} needs a numeric right side.";
            return false;
        }
        return CompareNumericCondition(remaining, args[3], right, result, status);
    }
    if (field == "message" || field == "status") {
        if (args.size() < 5) {
            if (status) *status = L"{if task ... message ...} needs an operator and text.";
            return false;
        }
        std::string op = LowerAscii(args[3]);
        size_t opPos = 0;
        std::string expr;
        for (size_t i = 4; i < args.size(); ++i) {
            if (!expr.empty()) expr += " ";
            expr += args[i];
        }
        (void)opPos;
        std::string leftLower = LowerAscii(message);
        std::string rightLower = LowerAscii(expr);
        if (op == "contains" || op == "~" || op == "has") {
            result = leftLower.find(rightLower) != std::string::npos;
            return true;
        }
        if (op == "notcontains" || op == "!contains" || op == "!~" || op == "lacks") {
            result = leftLower.find(rightLower) == std::string::npos;
            return true;
        }
        if (status) *status = L"{if task ... message ...} accepts contains or notcontains.";
        return false;
    }

    if (status) *status = L"{if task ...} accepts is, state, remaining, or message.";
    return false;
}

static bool EvaluateRegistryHubCondition(const std::string& expression, bool& result, std::wstring* status) {
    std::string body = TrimAscii(expression);
    std::vector<std::string> args = SplitScriptArgs(body);
    if (args.size() < 2) {
        if (status) *status = L"{if ...} needs left operand and operator.";
        return false;
    }

    std::string head = LowerAscii(args[0]);
    if (head == "task" || head == "saved-task" || head == "savedtask" ||
        head == "saved-tasks" || head == "savedtasks") {
        return EvaluateTasketDaemonCondition(args, result, status);
    }

    std::string left;
    if (!ResolveRegistryHubOperand(args[0], left)) {
        if (status) *status = L"{if ...} left operand must be A/S/D/F/Z/X/C/V, $name, or literal text.";
        return false;
    }

    std::string op = LowerAscii(args[1]);
    if (op == "empty") {
        result = TrimAscii(left).empty();
        return true;
    }
    if (op == "notempty") {
        result = !TrimAscii(left).empty();
        return true;
    }
    if (args.size() < 3) {
        if (status) *status = L"{if ...} needs a right operand for this operator.";
        return false;
    }
    size_t rightPos = body.find(args[1]);
    if (rightPos == std::string::npos) {
        if (status) *status = L"{if ...} could not read operator.";
        return false;
    }
    rightPos += args[1].size();
    std::string rightRaw = TrimAscii(body.substr(rightPos));
    std::string right;
    if (!ResolveRegistryHubOperand(rightRaw, right)) {
        if (status) *status = L"{if ...} right operand could not be resolved.";
        return false;
    }

    std::string leftLower = LowerAscii(left);
    std::string rightLower = LowerAscii(right);

    if (op == "contains" || op == "~" || op == "has") {
        result = leftLower.find(rightLower) != std::string::npos;
        return true;
    }
    if (op == "notcontains" || op == "!contains" || op == "!~" || op == "lacks") {
        result = leftLower.find(rightLower) == std::string::npos;
        return true;
    }
    double leftNumber = 0.0;
    double rightNumber = 0.0;
    bool numeric = TryParseDoubleStrict(left, leftNumber) && TryParseDoubleStrict(right, rightNumber);

    if (op == "==" || op == "=" || op == "eq") {
        result = numeric ? (fabs(leftNumber - rightNumber) < 0.0000001) : (left == right);
        return true;
    }
    if (op == "!=" || op == "<>" || op == "ne") {
        result = numeric ? (fabs(leftNumber - rightNumber) >= 0.0000001) : (left != right);
        return true;
    }
    if (op == ">" || op == "gt" || op == ">=" || op == "gte" || op == "<" || op == "lt" || op == "<=" || op == "lte") {
        if (!numeric) {
            if (status) *status = L"Numeric {if ...} comparison needs numeric operands.";
            return false;
        }
        if (op == ">" || op == "gt") result = leftNumber > rightNumber;
        else if (op == ">=" || op == "gte") result = leftNumber >= rightNumber;
        else if (op == "<" || op == "lt") result = leftNumber < rightNumber;
        else result = leftNumber <= rightNumber;
        return true;
    }

    if (status) *status = L"Unknown {if ...} operator. Use contains, ==, !=, >, >=, <, <=, empty, or notempty.";
    return false;
}

static bool ResolveScriptTextValue(const std::string& rawKey, std::wstring& text) {
    if (!LookupScriptTextValue(rawKey, text)) return false;
    return !text.empty();
}

static bool TokenIsScriptTextKey(const std::string& rawKey) {
    std::string key = LowerAscii(TrimAscii(rawKey));
    return RegistryVarIndexFromCommand(key) >= 0 || PasteBufferSlotFromCommand(key) >= 0;
}

static std::string CompactKeyAlias(std::string value) {
    value = LowerAscii(TrimAscii(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '_' || ch == '-' || ch == ' ') continue;
        out.push_back(ch);
    }
    return out;
}

static std::string UpperAscii(std::string value) {
    for (char& ch : value) {
        ch = (char)std::toupper((unsigned char)ch);
    }
    return value;
}

static std::string NormalizeTasketScriptKeyToken(const std::string& rawKey) {
    std::string trimmed = TrimAscii(rawKey);
    if (trimmed.empty()) return "";

    std::string compact = CompactKeyAlias(trimmed);
    if (compact.size() == 1 && std::isalnum((unsigned char)compact[0])) {
        return UpperAscii(compact);
    }

    if (compact == "left" || compact == "leftarrow" || compact == "arrowleft") return "LEFT_ARROW";
    if (compact == "right" || compact == "rightarrow" || compact == "arrowright") return "RIGHT_ARROW";
    if (compact == "up" || compact == "uparrow" || compact == "arrowup") return "UP_ARROW";
    if (compact == "down" || compact == "downarrow" || compact == "arrowdown") return "DOWN_ARROW";

    if (compact == "ctrl" || compact == "control" || compact == "leftctrl" ||
        compact == "ctrlleft" || compact == "leftcontrol" || compact == "controlleft" ||
        compact == "lctrl" || compact == "lcontrol") {
        return "CONTROL_LEFT";
    }
    if (compact == "rightctrl" || compact == "ctrlright" || compact == "rightcontrol" ||
        compact == "controlright" || compact == "rctrl" || compact == "rcontrol") {
        return "CONTROL_RIGHT";
    }
    if (compact == "shift" || compact == "leftshift" || compact == "shiftleft" || compact == "lshift") {
        return "SHIFT_LEFT";
    }
    if (compact == "rightshift" || compact == "shiftright" || compact == "rshift") {
        return "SHIFT_RIGHT";
    }
    if (compact == "alt" || compact == "leftalt" || compact == "altleft" || compact == "lalt") {
        return "ALT_LEFT";
    }
    if (compact == "rightalt" || compact == "altright" || compact == "altgr" || compact == "ralt") {
        return "ALT_GR";
    }
    if (compact == "win" || compact == "windows" || compact == "leftwin" || compact == "rightwin") {
        return "WINDOWS";
    }

    if (compact == "space" || compact == "spacebar") return "SPACEBAR";
    if (compact == "return" || compact == "enter") return "ENTER";
    if (compact == "escape" || compact == "esc") return "ESCAPE";
    if (compact == "backspace" || compact == "bksp") return "BACKSPACE";
    if (compact == "delete" || compact == "del") return "DELETE";
    if (compact == "insert" || compact == "ins") return "INSERT";
    if (compact == "tab") return "TAB";
    if (compact == "home") return "HOME";
    if (compact == "end") return "END";
    if (compact == "pageup" || compact == "pgup") return "PAGE_UP";
    if (compact == "pagedown" || compact == "pgdn") return "PAGE_DOWN";

    if (compact == "leftmouse" || compact == "mouseleft" || compact == "lmouse") return "LEFT_MOUSE";
    if (compact == "rightmouse" || compact == "mouseright" || compact == "rmouse") return "RIGHT_MOUSE";
    if (compact == "middlemouse" || compact == "mousemiddle" || compact == "mmouse") return "MIDDLE_MOUSE";

    if (compact.rfind("f", 0) == 0 && compact.size() <= 3) {
        bool allDigits = compact.size() > 1;
        for (size_t i = 1; i < compact.size(); ++i) {
            if (!std::isdigit((unsigned char)compact[i])) allDigits = false;
        }
        if (allDigits) return UpperAscii(compact);
    }

    return trimmed;
}

static bool TryParsePositiveInt(const std::string& raw, int& value) {
    std::string trimmed = TrimAscii(raw);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    long parsed = strtol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str()) return false;
    while (end && *end) {
        if (!isspace((unsigned char)*end)) return false;
        ++end;
    }
    if (parsed < 1) return false;
    if (parsed > 1000) parsed = 1000;
    value = (int)parsed;
    return true;
}

static bool AppendModifierRepeatToken(
    const std::vector<std::string>& args,
    std::vector<std::string>& actions,
    std::wstring* status) {
    if (args.size() < 2) {
        if (status) *status = L"{ctrlmod key count} or {shiftmod key count} needs a key.";
        return false;
    }

    std::string head = LowerAscii(TrimAscii(args[0]));
    std::string modifier;
    std::string namePrefix;
    if (head == "ctrlmod" || head == "controlmod") {
        modifier = "CONTROL_LEFT";
        namePrefix = "RegistryHubCtrlMod";
    } else if (head == "shiftmod") {
        modifier = "SHIFT_LEFT";
        namePrefix = "RegistryHubShiftMod";
    } else {
        return false;
    }

    std::string target = NormalizeTasketScriptKeyToken(args[1]);
    if (target.empty()) {
        if (status) *status = L"Modifier token target key is empty.";
        return false;
    }

    int count = 1;
    if (args.size() >= 3 && !TryParsePositiveInt(args[2], count)) {
        if (status) *status = L"Modifier token repeat count must be a positive integer.";
        return false;
    }

    std::vector<std::vector<std::string>> steps;
    steps.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        steps.push_back({modifier, target});
    }

    actions.push_back(BuildKeysSequenceActionJson(namePrefix + "_" + target + "_" + std::to_string(count), steps, 120, 90));
    actions.push_back(BuildWaitActionJson(0.05));
    return true;
}

static bool AppendPasteListActions(
    const std::vector<std::string>& keys,
    bool pressEnter,
    bool focusClickEnabled,
    std::vector<std::string>& actions,
    std::wstring* status) {
    if (keys.empty()) {
        if (status) *status = L"Paste token needs at least one buffer key.";
        return false;
    }
    if (focusClickEnabled) {
        actions.push_back(BuildWaitActionJson(0.1));
        actions.push_back(BuildLeftClickActionJson("RegistryHubPasteFocusClick"));
        actions.push_back(BuildWaitActionJson(0.08));
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        std::wstring text;
        if (!ResolveScriptTextValue(keys[i], text)) {
            if (status) *status = L"Paste token references an empty or unknown buffer/var.";
            return false;
        }
        actions.push_back(BuildPasteActionJson(
            "registry_hub_paste_" + LowerAscii(keys[i]) + "_" + std::to_string(i + 1),
            WStringToUtf8(text)));
        if (i + 1 < keys.size()) actions.push_back(BuildWaitActionJson(0.08));
    }
    if (pressEnter) {
        actions.push_back(BuildWaitActionJson(0.05));
        actions.push_back(BuildKeysSequenceActionJson("RegistryHubPasteEnter", {{"ENTER"}}, 200, 100));
    }
    actions.push_back(BuildWaitActionJson(0.1));
    return true;
}

static bool AppendDirectPasteListActions(const std::vector<std::string>& keys, std::vector<std::string>& actions, std::wstring* status) {
    if (keys.empty()) {
        if (status) *status = L"{paste ...} needs at least one buffer key.";
        return false;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        std::wstring text;
        if (!ResolveScriptTextValue(keys[i], text)) {
            if (status) *status = L"{paste ...} references an empty or unknown buffer/var.";
            return false;
        }
        actions.push_back(BuildPasteActionJson(
            "registry_hub_direct_paste_" + LowerAscii(keys[i]) + "_" + std::to_string(i + 1),
            WStringToUtf8(text)));
        if (i + 1 < keys.size()) actions.push_back(BuildWaitActionJson(0.08));
    }
    return true;
}

static void AppendOpenPowerShellPreviewActions(std::vector<std::string>& actions) {
    actions.push_back(BuildKeysSequenceActionJson(
        "RegistryHubOpenPowerShellPreview",
        {{"WINDOWS", "ALT_LEFT", "SPACEBAR"}},
        200,
        100));
    actions.push_back(BuildWaitActionJson(0.1));
    actions.push_back(BuildKeysSequenceActionJson(
        "RegistryHubConfirmPowerShellPreview",
        {{"ENTER"}},
        200,
        100));
    actions.push_back(BuildWaitActionJson(0.7));
}

static void AppendLiteralPasteEnterActions(
    const std::string& contentId,
    const std::wstring& text,
    std::vector<std::string>& actions) {
    if (TrimWide(text).empty()) return;
    actions.push_back(BuildPasteActionJson(contentId, WStringToUtf8(text)));
    actions.push_back(BuildWaitActionJson(0.08));
    actions.push_back(BuildKeysSequenceActionJson(contentId + "_enter", {{"ENTER"}}, 200, 100));
    actions.push_back(BuildWaitActionJson(0.15));
}

static bool ResolveShellAlias(const std::string& rawKey, std::wstring& alias, std::string& slug) {
    std::string key = LowerAscii(TrimAscii(rawKey));
    if (key == "x" || key == "bash") {
        alias = g_registryPowerShellPath;
        slug = "x";
        return true;
    }
    if (key == "c" || key == "cmd") {
        alias = g_registryCmdPath;
        slug = "c";
        return true;
    }
    if (key == "g" || key == "python" || key == "py") {
        alias = g_regeditedExePath;
        slug = "g";
        return true;
    }
    return false;
}

static bool AppendShellAliasToken(const std::vector<std::string>& args, std::vector<std::string>& actions, std::wstring* status) {
    if (args.empty()) {
        if (status) *status = L"{shell ...} needs X, C, or G.";
        return false;
    }
    std::wstring alias;
    std::string slug;
    if (!ResolveShellAlias(args[0], alias, slug)) {
        if (status) *status = L"{shell ...} only accepts X, C, G, bash, cmd, python, or py.";
        return false;
    }
    if (TrimWide(alias).empty()) {
        if (status) *status = L"Selected shell alias is empty. Configure X, C, or G first.";
        return false;
    }
    AppendOpenPowerShellPreviewActions(actions);
    AppendLiteralPasteEnterActions("registry_hub_shell_alias_" + slug, alias, actions);
    return true;
}

static std::vector<std::string> ExtractRegistryHubTokens(const std::wstring& script) {
    std::string text = WStringToUtf8(script);
    std::vector<std::string> tokens;
    size_t pos = 0;
    while (true) {
        size_t open = text.find('{', pos);
        if (open == std::string::npos) break;
        size_t close = text.find('}', open + 1);
        if (close == std::string::npos) break;
        tokens.push_back(TrimAscii(text.substr(open + 1, close - open - 1)));
        pos = close + 1;
    }
    return tokens;
}

static std::string BuildRunningOtherTaskActionJson(const std::string& taskName, int delaySeconds = 0, int loops = 1) {
    std::string cleanName = taskName;
    if (cleanName.size() > 5 && LowerAscii(cleanName.substr(cleanName.size() - 5)) == ".scht") {
        cleanName = cleanName.substr(0, cleanName.size() - 5);
    }
    std::string json;
    json += "        {\n";
    json += "            \"otherTaskDelay\": " + std::to_string(delaySeconds < 0 ? 0 : delaySeconds) + ",\n";
    json += "            \"otherTaskLoops\": " + std::to_string(loops < 1 ? 1 : loops) + ",\n";
    json += "            \"otherTaskName\": \"" + EscapeJsonString(cleanName) + "\",\n";
    json += "            \"type\": \"runningothertask\"\n";
    json += "        },";
    return json;
}

static bool AppendZoneCaptureSaveActions(
    std::vector<std::string>& actions,
    int targetSlot,
    int flowIndex,
    std::wstring* status) {
    if (targetSlot < 0 || targetSlot > 3) {
        if (status) *status = L"Zone capture target paste buffer is invalid.";
        return false;
    }
    char targetKey = (char)PasteBufferSlotKey(targetSlot);
    std::vector<std::vector<std::string>> saveSteps = {
        {"SHIFT_LEFT", "ALT_LEFT", "0"},
        {std::string(1, targetKey)},
        {"CONTROL_LEFT", "A"},
        {"CONTROL_LEFT", "V"},
        {"ENTER"}
    };
    actions.push_back(BuildWaitActionJson(flowIndex == 3 ? 0.8 : 1.4));
    actions.push_back(BuildKeysSequenceActionJson("RegistryHubStoreZoneClipboardInBuffer", saveSteps, 360, 100));
    actions.push_back(BuildWaitActionJson(0.2));
    return true;
}

static bool AppendZoneFlowToken(const std::vector<std::string>& args, std::vector<std::string>& actions, std::wstring* status) {
    if (args.empty()) {
        if (status) *status = L"{zone ...} needs arguments.";
        return false;
    }

    auto zoneSlotOf = [](const std::string& key) { return PasteBufferSlotFromCommand(LowerAscii(key)); };
    auto flowOf = [](const std::string& key) { return ZoneFlowFromCommand(LowerAscii(key), true); };

    int zoneSlot = -1;
    int flowIndex = -1;
    int targetSlot = -1;
    bool captureToPaste = false;

    if (LowerAscii(args[0]) == "b") {
        if (args.size() >= 5 && LowerAscii(args[1]) == "b") {
            zoneSlot = zoneSlotOf(args[2]);
            targetSlot = zoneSlotOf(args[3]);
            flowIndex = flowOf(args[4]);
            captureToPaste = true;
        } else if (args.size() >= 3) {
            zoneSlot = zoneSlotOf(args[1]);
            flowIndex = flowOf(args[2]);
        }
    } else if (args.size() >= 2) {
        zoneSlot = zoneSlotOf(args[0]);
        flowIndex = flowOf(args[1]);
    }

    if (zoneSlot < 0 || zoneSlot > 3 || !g_zoneBuffers[zoneSlot].set) {
        if (status) *status = L"Zone token references an unset zone buffer.";
        return false;
    }
    if (flowIndex < 0 || flowIndex > 3) {
        if (status) *status = L"Zone token needs method Z, X, C, or V.";
        return false;
    }
    if (captureToPaste && (flowIndex == 0 || targetSlot < 0)) {
        if (status) *status = L"Zone-to-buffer capture needs X, C, or V and a target paste buffer.";
        return false;
    }

    std::vector<std::string> zoneActions;
    std::string desc;
    std::string taskName;
    if (!LoadZoneFlowActionList(flowIndex, g_zoneBuffers[zoneSlot].start, g_zoneBuffers[zoneSlot].end, zoneActions, &desc, &taskName, status)) {
        return false;
    }
    actions.insert(actions.end(), zoneActions.begin(), zoneActions.end());
    if (captureToPaste && !AppendZoneCaptureSaveActions(actions, targetSlot, flowIndex, status)) return false;
    return true;
}

static bool AppendZoneBufToken(const std::vector<std::string>& args, std::vector<std::string>& actions, std::wstring* status) {
    if (args.size() < 3) {
        if (status) *status = L"{zonebuf Z,X,V} needs zone, method, and target paste buffer.";
        return false;
    }
    std::vector<std::string> rewritten = {"b", "b", args[0], args[2], args[1]};
    return AppendZoneFlowToken(rewritten, actions, status);
}

static bool ApplyRegistryHubSetToken(const std::string& token, std::wstring* status) {
    std::string body = TrimAscii(token.substr(4));
    std::vector<std::string> args = SplitScriptArgs(body);
    if (args.size() < 2) {
        if (status) *status = L"{set ...} needs a target and value.";
        return false;
    }
    std::string target = LowerAscii(args[0]);
    std::string value;
    size_t valuePos = body.find(args[1]);
    if (valuePos != std::string::npos) value = TrimAscii(body.substr(valuePos));
    value = ExpandRegistryHubValueReferences(value);

    int varSlot = RegistryVarIndexFromCommand(target);
    if (varSlot >= 0) {
        g_registryVars[varSlot] = Utf8ToWString(value);
        return true;
    }
    int pasteSlot = PasteBufferSlotFromCommand(target);
    if (pasteSlot >= 0) {
        g_pasteBuffers[pasteSlot] = Utf8ToWString(value);
        return true;
    }
    if ((target == "paste" || target == "buffer") && args.size() >= 3) {
        int slot = PasteBufferSlotFromCommand(args[1]);
        size_t pos = body.find(args[2]);
        if (slot >= 0 && pos != std::string::npos) {
            g_pasteBuffers[slot] = Utf8ToWString(ExpandRegistryHubValueReferences(TrimAscii(body.substr(pos))));
            return true;
        }
    }
    if (target == "zone" && args.size() >= 3) {
        int slot = PasteBufferSlotFromCommand(LowerAscii(args[1]));
        POINT start = {}, end = {};
        size_t pos = body.find(args[2]);
        if (slot >= 0 && pos != std::string::npos &&
            ParseZoneCoordinatesOrPointRefs(Utf8ToWString(ExpandRegistryHubValueReferences(TrimAscii(body.substr(pos)))), start, end)) {
            g_zoneBuffers[slot].set = true;
            g_zoneBuffers[slot].start = start;
            g_zoneBuffers[slot].end = end;
            return true;
        }
    }
    if (target == "point" && args.size() >= 3) {
        int point = RegistryPointIndexFromCommand(args[1]);
        POINT parsed = {};
        size_t pos = body.find(args[2]);
        if (point >= 0 && point < REGISTRY_POINT_COUNT && pos != std::string::npos &&
            ParsePointCoordinatesOrReference(Utf8ToWString(ExpandRegistryHubValueReferences(TrimAscii(body.substr(pos)))), parsed)) {
            g_registryPointSet[point] = true;
            g_registryPoints[point] = parsed;
            return true;
        }
    }
    if (target == "registry") {
        g_registryFilePath = ExpandMacrohelpPath(Utf8ToWString(value));
        return true;
    }
    if (target == "shell" && args.size() >= 3) {
        size_t aliasPos = body.find(args[2]);
        if (aliasPos == std::string::npos) {
            if (status) *status = L"{set shell ...} could not read alias text.";
            return false;
        }
        std::wstring aliasValue = Utf8ToWString(TrimAscii(body.substr(aliasPos)));
        std::string slot = LowerAscii(args[1]);
        if (slot == "x" || slot == "bash") {
            g_registryPowerShellPath = aliasValue;
            return true;
        }
        if (slot == "c" || slot == "cmd") {
            g_registryCmdPath = TrimWide(aliasValue).empty() ? L"cmd" : aliasValue;
            return true;
        }
        if (slot == "g" || slot == "python" || slot == "py") {
            g_regeditedExePath = aliasValue;
            return true;
        }
    }
    if (target == "x" || target == "bash") {
        g_registryPowerShellPath = Utf8ToWString(value);
        return true;
    }
    if (target == "c" || target == "cmd") {
        std::wstring aliasValue = Utf8ToWString(value);
        g_registryCmdPath = TrimWide(aliasValue).empty() ? L"cmd" : aliasValue;
        return true;
    }
    if (target == "g" || target == "python" || target == "py" || target == "regedited" || target == "regbin") {
        g_regeditedExePath = Utf8ToWString(value);
        return true;
    }
    if (status) *status = L"Unknown {set ...} target.";
    return false;
}

static bool CompileRegistryHubToken(
    const std::string& rawToken,
    std::vector<std::string>& actions,
    bool& pasteFocusClickEnabled,
    std::wstring* status) {
    std::string token = TrimAscii(rawToken);
    std::string lower = LowerAscii(token);
    if (token.empty()) return true;

    if (lower.rfind("set ", 0) == 0) {
        return ApplyRegistryHubSetToken(token, status);
    }
    if (lower.rfind("wait ", 0) == 0) {
        int ms = atoi(token.c_str() + 5);
        if (ms < 0) ms = 0;
        actions.push_back(BuildWaitActionJson(ms / 1000.0));
        return true;
    }
    if (lower.rfind("point ", 0) == 0) {
        int idx = RegistryPointIndexFromCommand(token.substr(6));
        if (idx < 0 || idx >= REGISTRY_POINT_COUNT || !g_registryPointSet[idx]) {
            if (status) *status = L"Point token references an unset point.";
            return false;
        }
        std::string moveActions;
        if (!LoadCursorMoveActions(g_registryPoints[idx], &moveActions, status)) return false;
        actions.push_back(moveActions);
        return true;
    }
    if (lower == "click on" || lower == "paste click on" || lower == "focus click on") {
        pasteFocusClickEnabled = true;
        return true;
    }
    if (lower == "click off" || lower == "paste click off" || lower == "focus click off") {
        pasteFocusClickEnabled = false;
        return true;
    }
    if (lower == "click" || lower == "left") {
        actions.push_back(BuildMouseClickActionJson("RegistryHubLeftClick", "LEFT_MOUSE", 100));
        return true;
    }
    if (lower == "right") {
        actions.push_back(BuildMouseClickActionJson("RegistryHubRightClick", "RIGHT_MOUSE", 100));
        return true;
    }
    if (lower == "middle") {
        actions.push_back(BuildMouseClickActionJson("RegistryHubMiddleClick", "MIDDLE_MOUSE", 100));
        return true;
    }
    if (lower.rfind("exec ", 0) == 0) {
        if (status) *status = L"{exec ...} is disabled in Registry Hub. Open PowerShell with {powershell}, paste commands, and use Tasket key/paste primitives.";
        return false;
    }
    if (lower.rfind("play ", 0) == 0) {
        actions.push_back(BuildRunningOtherTaskActionJson(TrimAscii(token.substr(5)), 0, 1));
        return true;
    }
    if (lower == "powershell") {
        AppendOpenPowerShellPreviewActions(actions);
        return true;
    }
    if (lower == "cmd") {
        return AppendShellAliasToken({"c"}, actions, status);
    }
    if (lower == "bash") {
        return AppendShellAliasToken({"x"}, actions, status);
    }
    if (lower == "python" || lower == "py") {
        return AppendShellAliasToken({"g"}, actions, status);
    }
    if (lower.rfind("shell ", 0) == 0) {
        return AppendShellAliasToken(SplitScriptArgs(token.substr(6)), actions, status);
    }
    if (lower.rfind("ctrlmod ", 0) == 0 || lower.rfind("controlmod ", 0) == 0 ||
        lower.rfind("shiftmod ", 0) == 0) {
        return AppendModifierRepeatToken(SplitScriptArgs(token), actions, status);
    }
    if (lower.rfind("system ", 0) == 0) {
        if (status) *status = L"{system ...} is disabled in Registry Hub. Use Tasket keyboard/paste primitives instead.";
        return false;
    }
    if (lower.rfind("sys ", 0) == 0) {
        if (status) *status = L"{sys ...} is disabled in Registry Hub. Use Tasket keyboard/paste primitives instead.";
        return false;
    }
    if (lower.rfind("paste ", 0) == 0) {
        std::vector<std::string> args = SplitScriptArgs(token.substr(6));
        for (const auto& arg : args) {
            if (!TokenIsScriptTextKey(arg)) {
                if (status) *status = L"{paste ...} only accepts A/S/D/F/Z/X/C/V.";
                return false;
            }
        }
        return AppendDirectPasteListActions(args, actions, status);
    }
    if (lower.rfind("zonebuf ", 0) == 0) {
        return AppendZoneBufToken(SplitScriptArgs(token.substr(8)), actions, status);
    }
    if (lower.rfind("zone ", 0) == 0) {
        return AppendZoneFlowToken(SplitScriptArgs(token.substr(5)), actions, status);
    }
    if (lower.rfind("enter ", 0) == 0) {
        std::vector<std::string> args = SplitScriptArgs(token.substr(6));
        for (const auto& arg : args) {
            if (!TokenIsScriptTextKey(arg)) {
                if (status) *status = L"{enter ...} only accepts A/S/D/F/Z/X/C/V.";
                return false;
            }
        }
        return AppendPasteListActions(args, true, pasteFocusClickEnabled, actions, status);
    }

    std::vector<std::string> args = SplitScriptArgs(token);
    if (!args.empty()) {
        bool allTextKeys = true;
        for (const auto& arg : args) {
            if (!TokenIsScriptTextKey(arg)) {
                allTextKeys = false;
                break;
            }
        }
        if (allTextKeys) return AppendPasteListActions(args, false, pasteFocusClickEnabled, actions, status);

        std::vector<std::string> keyStep;
        for (const auto& arg : args) keyStep.push_back(NormalizeTasketScriptKeyToken(arg));
        actions.push_back(BuildKeysSequenceActionJson("RegistryHubKeys", {keyStep}, 200, 100));
        return true;
    }

    if (status) *status = L"Unknown registry hub token.";
    return false;
}

struct RegistryHubBranchFrame {
    bool parentActive = true;
    bool conditionActive = false;
    bool elseMode = false;
};

static bool RegistryHubBranchesActive(const std::vector<RegistryHubBranchFrame>& branches) {
    if (branches.empty()) return true;
    const RegistryHubBranchFrame& frame = branches.back();
    return frame.parentActive && (frame.elseMode ? !frame.conditionActive : frame.conditionActive);
}

static bool CompileRegistryHubScriptToTaskJson(std::string* taskJson, int* actionCount, std::wstring* status) {
    if (taskJson) taskJson->clear();
    if (actionCount) *actionCount = 0;

    std::vector<std::string> tokens = ExtractRegistryHubTokens(g_registryScript);
    if (tokens.empty()) {
        if (status) *status = L"Registry Hub script has no {...} tokens.";
        return false;
    }

    std::vector<std::string> actions;
    std::vector<RegistryHubBranchFrame> branches;
    bool pasteFocusClickEnabled = false;
    for (const auto& token : tokens) {
        std::string trimmed = TrimAscii(token);
        std::string lower = LowerAscii(trimmed);
        if (lower.rfind("if ", 0) == 0) {
            bool parentActive = RegistryHubBranchesActive(branches);
            bool condition = false;
            if (parentActive && !EvaluateRegistryHubCondition(trimmed.substr(3), condition, status)) return false;
            branches.push_back({parentActive, condition, false});
            continue;
        }
        if (lower == "else") {
            if (branches.empty()) {
                if (status) *status = L"{else} needs a matching {if ...}.";
                return false;
            }
            if (branches.back().elseMode) {
                if (status) *status = L"Duplicate {else} in Registry Hub branch.";
                return false;
            }
            branches.back().elseMode = true;
            continue;
        }
        if (lower == "endif" || lower == "end if") {
            if (branches.empty()) {
                if (status) *status = L"{endif} needs a matching {if ...}.";
                return false;
            }
            branches.pop_back();
            continue;
        }
        if (!RegistryHubBranchesActive(branches)) continue;
        if (!CompileRegistryHubToken(token, actions, pasteFocusClickEnabled, status)) return false;
    }
    if (!branches.empty()) {
        if (status) *status = L"Registry Hub script has an unclosed {if ...}. Add {endif}.";
        return false;
    }
    if (actions.empty()) {
        if (status) *status = L"Registry Hub state updated. No Tasket actions were emitted.";
        return true;
    }

    if (actionCount) *actionCount = (int)actions.size();
    if (taskJson) *taskJson = BuildTasketDocumentJson("Macrohelp Registry Hub compiled native Tasket flow.", actions, 1);
    if (status) *status = L"Registry Hub script compiled.";
    return true;
}

static bool ScheduleTasketRegistryHubScript(std::wstring* status) {
    std::string task;
    int actionCount = 0;
    if (!CompileRegistryHubScriptToTaskJson(&task, &actionCount, status)) return false;
    if (task.empty() || actionCount <= 0) return true;
    bool ok = ScheduleTasketTempTask("macrohelp_registry_hub_flow_temp", task, 1, status);
    return ok;
}

static bool SaveRegistryHubCompiledSchts(std::wstring* status) {
    std::string task;
    int actionCount = 0;
    if (!CompileRegistryHubScriptToTaskJson(&task, &actionCount, status)) return false;
    if (task.empty() || actionCount <= 0) {
        if (status) *status = L"No Tasket actions were emitted; no .scht file saved.";
        return false;
    }

    std::string stamp = LocalTimestampForFileName();
    std::wstring dir = DefaultDesktopTempsDir();
    std::wstring base = dir + L"\\macrohelp-registry-hub-compiled-" + Utf8ToWString(stamp);
    std::wstring schtPath = base + L"-1.scht";
    std::wstring manifestPath = base + L"-manifest.txt";

    if (!WriteUtf8File(schtPath, task)) {
        if (status) *status = L"Failed to write compiled .scht: " + schtPath;
        return false;
    }

    std::ostringstream manifest;
    manifest << "Macrohelp Registry Hub compiled schedule dump\n";
    manifest << "GeneratedLocal=" << stamp << "\n";
    manifest << "ScheduleCount=1\n";
    manifest << "ActionCount=" << actionCount << "\n";
    manifest << "Schedule1=" << WStringToUtf8(schtPath) << "\n";
    manifest << "Note=This is the exact Tasket JSON Macrohelp would schedule as macrohelp_registry_hub_flow_temp.\n";
    WriteUtf8File(manifestPath, manifest.str());

    if (status) *status = L"Saved compiled .scht to Desktop\\temps: " + schtPath;
    return true;
}

// =============================================================================
// VK CODE -> TASKET NAME
// =============================================================================

std::string VkCodeToTasketName(UINT vkCode, bool extended) {
    switch (vkCode) {
        case 'A': return "A";
        case 'B': return "B";
        case 'C': return "C";
        case 'D': return "D";
        case 'E': return "E";
        case 'F': return "F";
        case 'G': return "G";
        case 'H': return "H";
        case 'I': return "I";
        case 'J': return "J";
        case 'K': return "K";
        case 'L': return "L";
        case 'M': return "M";
        case 'N': return "N";
        case 'O': return "O";
        case 'P': return "P";
        case 'Q': return "Q";
        case 'R': return "R";
        case 'S': return "S";
        case 'T': return "T";
        case 'U': return "U";
        case 'V': return "V";
        case 'W': return "W";
        case 'X': return "X";
        case 'Y': return "Y";
        case 'Z': return "Z";
        case '0': return "0"; case '1': return "1"; case '2': return "2";
        case '3': return "3"; case '4': return "4"; case '5': return "5";
        case '6': return "6"; case '7': return "7"; case '8': return "8"; case '9': return "9";
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
        case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
        case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        case VK_CONTROL: return extended ? "CONTROL_RIGHT" : "CONTROL_LEFT";
        case VK_LCONTROL: return "CONTROL_LEFT";
        case VK_RCONTROL: return "CONTROL_RIGHT";
        case VK_SHIFT: return extended ? "SHIFT_RIGHT" : "SHIFT_LEFT";
        case VK_LSHIFT: return "SHIFT_LEFT";
        case VK_RSHIFT: return "SHIFT_RIGHT";
        case VK_MENU: return extended ? "ALT_GR" : "ALT_LEFT";
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
        case VK_RETURN: return extended ? "NUMPAD_ENTER" : "ENTER";
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

    g_promptFontCollection = new PrivateFontCollection();
    if (g_promptFontCollection->AddMemoryFont(pData, (INT)size) != Ok) {
        delete g_promptFontCollection;
        g_promptFontCollection = nullptr;
    }

    // Create HFONT handles for PromptFont
    g_hPromptFont = CreateFontW(UiFontPx(KEY_FONT_SIZE + 2), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"PromptFont");

    g_hPromptFontSmall = CreateFontW(UiFontPx(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"PromptFont");

    return g_hPromptFont != nullptr;
}

std::unique_ptr<Font> CreatePromptFont(REAL size) {
    if (g_promptFontCollection) {
        INT familyCount = g_promptFontCollection->GetFamilyCount();
        if (familyCount > 0) {
            FontFamily family;
            INT found = 0;
            if (g_promptFontCollection->GetFamilies(1, &family, &found) == Ok && found > 0) {
                auto font = std::make_unique<Font>(&family, size, FontStyleRegular, UnitPixel);
                if (font->GetLastStatus() == Ok) {
                    return font;
                }
            }
        }
    }

    return std::make_unique<Font>(L"Segoe UI", size, FontStyleRegular, UnitPixel);
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

        Font font(L"Segoe UI", (REAL)UiFontPx(10), FontStyleRegular, UnitPixel);
        SolidBrush brush(Color(a, 160, 160, 170));
        PointF pt((REAL)startX, (REAL)(y + UiPx(4)));
        gfx.DrawString(prefix.c_str(), (INT)prefix.length(), &font, pt, &brush);
    }

    int x = startX + UiPx(KEY_PREFIX_W);

    // Draw each key in the combo
    for (size_t i = 0; i < keys.size(); i++) {
        // "+" separator between keys
        if (i > 0) {
            Font sepFont(L"Segoe UI", (REAL)UiFontPx(9), FontStyleRegular, UnitPixel);
            SolidBrush sepBrush(Color(a, 180, 180, 190));
            PointF sepPt((REAL)x, (REAL)(y + UiPx(4)));
            gfx.DrawString(L"+", 1, &sepFont, sepPt, &sepBrush);
            x += UiPx(16);
        }

        // Get glyph or text for this key
        std::wstring display = KeyToGlyph(keys[i]);

        // Measure text to size the box
        auto measureFont = CreateKeyTextFont(keys[i], (REAL)UiFontPx(KEY_FONT_SIZE));
        int textW = MeasureKeyTextWidth(gfx, display, *measureFont);
        if (textW < UiPx(12)) textW = UiPx(12);
        int boxW = textW + UiPx(KEY_BOX_PAD_X) * 2;

        // Draw rounded rect background
        Color bgColor(a, KEY_BG_R, KEY_BG_G, KEY_BG_B);
        Color borderColor(a, KEY_BORDER_R, KEY_BORDER_G, KEY_BORDER_B);
        DrawRoundedRect(gfx, x, y, boxW, boxH, UiPx(KEY_BOX_RADIUS), bgColor, borderColor);

        // Draw text
        auto font = CreateKeyTextFont(keys[i], (REAL)UiFontPx(KEY_FONT_SIZE));
        SolidBrush textBrush(Color(a, 255, 255, 255));

        // Center text in box
        RectF layoutRect((REAL)(x + UiPx(2)), (REAL)(y - UiPx(1)), (REAL)(boxW - UiPx(4)), (REAL)(boxH + UiPx(2)));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        gfx.DrawString(display.c_str(), (INT)display.length(), font.get(), layoutRect, &sf, &textBrush);

        x += boxW + UiPx(KEY_GAP_X);
    }
}

void RenderKeyDisplay() {
    if (!g_hwndKeyDisplay || !g_hKeyDibDC) return;

    // Get cursor position to position the window
    POINT cursorPt;
    GetCursorPos(&cursorPt);

    // Anchor to the upper-right of the cursor. Entries are drawn from the
    // bottom upward so the newest key feels attached to the pointer.
    int keyDisplayW = KeyDisplayW();
    int keyDisplayH = KeyDisplayH();
    int winX = cursorPt.x + UiPx(KEY_OFFSET_X);
    int winY = cursorPt.y - keyDisplayH - UiPx(KEY_OFFSET_Y);

    // Clamp to screen bounds
    if (winX < g_screenX) winX = g_screenX;
    if (winX + keyDisplayW > g_screenX + g_screenW) {
        winX = cursorPt.x - keyDisplayW - UiPx(KEY_OFFSET_X);
    }
    if (winX < g_screenX) winX = g_screenX;
    if (winY < g_screenY) winY = g_screenY;
    if (winY + keyDisplayH > g_screenY + g_screenH) winY = g_screenY + g_screenH - keyDisplayH;

    // Move window if needed
    RECT rc;
    GetWindowRect(g_hwndKeyDisplay, &rc);
    if (rc.left != winX || rc.top != winY) {
        SetWindowPos(g_hwndKeyDisplay, nullptr, winX, winY, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Draw directly into the 32bpp DIB memory as premultiplied alpha.
    // Constructing a GDI+ bitmap from HBITMAP can discard/ignore alpha,
    // which makes UpdateLayeredWindow receive a fully transparent surface.
    if (!g_pKeyDibBits) return;
    Bitmap bmp(g_keyDibW, g_keyDibH, g_keyDibW * 4,
               PixelFormat32bppPARGB, static_cast<BYTE*>(g_pKeyDibBits));
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
            entryCount = std::min(g_keyHistoryCount, KEY_MAX_VISIBLE);
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
    int keyBoxH = UiPx(KEY_BOX_H);
    int y = keyDisplayH - keyBoxH - UiPx(8);

    // Draw active (unfinalized) combo closest to the cursor with pulsing opacity
    if (!activeKeys.empty()) {
        // Pulse between 60% and 100% opacity
        float pulse = 0.6f + 0.4f * sinf((now % 1000) / 1000.0f * 3.14159f * 2);
        DrawKeyDisplayEntry(gfx, activeKeys, UiPx(8), y, keyBoxH, 0, pulse);
        y -= keyBoxH + UiPx(KEY_GAP_Y) + UiPx(4);
    }

    // Draw history entries upward like a compact input timeline
    for (int i = 0; i < entryCount; i++) {
        DWORD age = now - entries[i].tickMs;
        if (age > (DWORD)KEY_FADE_MS) continue;

        float opacity = 1.0f - (float)age / (float)KEY_FADE_MS;
        if (opacity < 0.02f) continue;

        // History index: 0 = current (most recent finalized), -1 = next, etc.
        int histIdx = -i;

        DrawKeyDisplayEntry(gfx, entries[i].keys, UiPx(8), y, keyBoxH, histIdx, opacity);
        y -= keyBoxH + UiPx(KEY_GAP_Y);

        if (y < UiPx(8)) break;
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

static void DrawLabelBox(HDC hdc, const std::wstring& text, int x, int y, COLORREF accent) {
    if (text.empty()) return;

    HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontCoord);
    SIZE size = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size);

    const int padX = UiPx(8);
    const int padY = UiPx(4);
    RECT rc = {x, y, x + size.cx + padX * 2, y + size.cy + padY * 2};
    HBRUSH bg = CreateSolidBrush(RGB(18, 22, 27));
    HPEN pen = CreatePen(PS_SOLID, 1, accent);
    HBRUSH oldBg = (HBRUSH)SelectObject(hdc, bg);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, UiPx(10), UiPx(10));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBg);
    DeleteObject(pen);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(245, 250, 255));
    RECT textRc = {rc.left + padX, rc.top, rc.right - padX, rc.bottom};
    DrawTextW(hdc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
}

void DrawCirclePreview(HDC hdc) {
    if (!g_circlePreview.enabled) return;

    DWORD now = GetTickCount();
    if (g_circlePreview.expiresTickMs != 0 &&
        (LONG)(now - g_circlePreview.expiresTickMs) >= 0) {
        g_circlePreview.enabled = false;
        return;
    }

    POINT origin = {g_circlePreview.originX, g_circlePreview.originY};
    POINT target = {g_circlePreview.targetX, g_circlePreview.targetY};
    ScreenToClient(g_hwndCrosshair, &origin);
    ScreenToClient(g_hwndCrosshair, &target);

    SetBkMode(hdc, TRANSPARENT);

    HPEN oldPen = nullptr;
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (g_circlePreview.zoneEnabled) {
        POINT zs = g_circlePreview.zoneStart;
        POINT ze = g_circlePreview.zoneEnd;
        ScreenToClient(g_hwndCrosshair, &zs);
        ScreenToClient(g_hwndCrosshair, &ze);
        RECT zoneRc = {
            std::min(zs.x, ze.x),
            std::min(zs.y, ze.y),
            std::max(zs.x, ze.x),
            std::max(zs.y, ze.y)
        };
        HPEN zonePen = CreatePen(PS_DASH, 2, RGB(100, 255, 165));
        oldPen = (HPEN)SelectObject(hdc, zonePen);
        Rectangle(hdc, zoneRc.left, zoneRc.top, zoneRc.right, zoneRc.bottom);
        SelectObject(hdc, oldPen);
        DeleteObject(zonePen);
        DrawLabelBox(hdc, L"zone", zoneRc.left + 8, zoneRc.top + 8, RGB(100, 255, 165));
    }

    int r = g_circlePreview.radius;
    if (r <= 0) {
        SelectObject(hdc, oldBrush);
        return;
    }

    HPEN circlePen = CreatePen(PS_DASH, 2, RGB(0, 185, 255));
    oldPen = (HPEN)SelectObject(hdc, circlePen);
    Ellipse(hdc, origin.x - r, origin.y - r, origin.x + r, origin.y + r);
    SelectObject(hdc, oldPen);
    DeleteObject(circlePen);

    if (g_circlePreview.showQuadrants) {
        HPEN axisPen = CreatePen(PS_DOT, 1, RGB(92, 174, 210));
        oldPen = (HPEN)SelectObject(hdc, axisPen);
        MoveToEx(hdc, origin.x - r, origin.y, nullptr);
        LineTo(hdc, origin.x + r, origin.y);
        MoveToEx(hdc, origin.x, origin.y - r, nullptr);
        LineTo(hdc, origin.x, origin.y + r);
        SelectObject(hdc, oldPen);
        DeleteObject(axisPen);
    }

    HBRUSH originBrush = CreateSolidBrush(RGB(0, 185, 255));
    HBRUSH oldOriginBrush = (HBRUSH)SelectObject(hdc, originBrush);
    HPEN originPen = CreatePen(PS_SOLID, 1, RGB(210, 250, 255));
    oldPen = (HPEN)SelectObject(hdc, originPen);
    Ellipse(hdc, origin.x - 4, origin.y - 4, origin.x + 4, origin.y + 4);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldOriginBrush);
    DeleteObject(originPen);
    DeleteObject(originBrush);

    if (g_circlePreview.showTarget) {
        HPEN vectorPen = CreatePen(PS_SOLID, 2, RGB(255, 205, 60));
        oldPen = (HPEN)SelectObject(hdc, vectorPen);
        MoveToEx(hdc, origin.x, origin.y, nullptr);
        LineTo(hdc, target.x, target.y);
        SelectObject(hdc, oldPen);
        DeleteObject(vectorPen);

        HBRUSH targetBrush = CreateSolidBrush(RGB(255, 205, 60));
        HBRUSH oldTargetBrush = (HBRUSH)SelectObject(hdc, targetBrush);
        HPEN targetPen = CreatePen(PS_SOLID, 1, RGB(255, 252, 210));
        oldPen = (HPEN)SelectObject(hdc, targetPen);
        Ellipse(hdc, target.x - 6, target.y - 6, target.x + 6, target.y + 6);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldTargetBrush);
        DeleteObject(targetPen);
        DeleteObject(targetBrush);
    }

    SelectObject(hdc, oldBrush);

    std::wstring radiusText = L"r " + std::to_wstring(r) + L" px";
    DrawLabelBox(hdc, radiusText, origin.x + 12, origin.y - 32, RGB(0, 185, 255));
    if (g_circlePreview.showTarget) {
        DrawLabelBox(hdc, g_circlePreview.label, target.x + 12, target.y + 12, RGB(255, 205, 60));
        DrawLabelBox(hdc, g_circlePreview.angleLabel, target.x + 12, target.y + 40, RGB(255, 205, 60));
    }
}

static std::wstring GridOffsetLabel(int dxPx, int dyPx) {
    if (dxPx == 0 && dyPx == 0) return L"0i+0j";

    int jPx = -dyPx; // math-style positive j points upward on screen.
    std::wstringstream ss;
    if (dxPx >= 0) ss << L"+";
    ss << dxPx << L"i";
    if (jPx >= 0) ss << L"+";
    ss << jPx << L"j";
    return ss.str();
}

void DrawCursorGrid(HDC hdc, const RECT& rc, POINT pt) {
    if (!g_gridVisible) return;

    const int spacing = GRID_SPACING_PX;
    if (spacing < 24) return;

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(42, 46, 52));
    HPEN oldPen = (HPEN)SelectObject(hdc, gridPen);

    for (int x = pt.x; x >= rc.left; x -= spacing) {
        MoveToEx(hdc, x, rc.top, nullptr);
        LineTo(hdc, x, rc.bottom);
    }
    for (int x = pt.x + spacing; x <= rc.right; x += spacing) {
        MoveToEx(hdc, x, rc.top, nullptr);
        LineTo(hdc, x, rc.bottom);
    }
    for (int y = pt.y; y >= rc.top; y -= spacing) {
        MoveToEx(hdc, rc.left, y, nullptr);
        LineTo(hdc, rc.right, y);
    }
    for (int y = pt.y + spacing; y <= rc.bottom; y += spacing) {
        MoveToEx(hdc, rc.left, y, nullptr);
        LineTo(hdc, rc.right, y);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    HFONT hGridFont = CreateFontW(
        UiFontPx(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hGridFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(92, 100, 108));

    const int minGx = (int)floor((double)(rc.left + UiPx(14) - pt.x) / spacing);
    const int maxGx = (int)ceil((double)(rc.right - UiPx(80) - pt.x) / spacing);
    const int minGy = (int)floor((double)(rc.top + UiPx(12) - pt.y) / spacing);
    const int maxGy = (int)ceil((double)(rc.bottom - UiPx(20) - pt.y) / spacing);

    for (int gx = minGx; gx <= maxGx; ++gx) {
        int x = pt.x + gx * spacing;
        if (x < rc.left + UiPx(14) || x > rc.right - UiPx(80)) continue;
        for (int gy = minGy; gy <= maxGy; ++gy) {
            int y = pt.y + gy * spacing;
            if (y < rc.top + UiPx(12) || y > rc.bottom - UiPx(20)) continue;
            if (gx == 0 && gy == 0) continue;
            if ((abs(gx) + abs(gy)) % 2 != 0) continue;

            std::wstring label = GridOffsetLabel(gx * spacing, gy * spacing);
            TextOutW(hdc, x + UiPx(4), y + UiPx(4), label.c_str(), (int)label.size());
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hGridFont);
}

static void DrawCrosshair(HDC hdc) {
    POINT screenPt;
    GetCursorPos(&screenPt);
    POINT pt = screenPt;
    ScreenToClient(g_hwndCrosshair, &pt);
    RECT rc;
    GetClientRect(g_hwndCrosshair, &rc);

    // Fill with colorkey (transparent)
    FillRect(hdc, &rc, g_hbrColorkey);

    if (!g_coordinatesOnlyMode) {
        DrawCursorGrid(hdc, rc, pt);
        DrawCirclePreview(hdc);

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

        // Existing cursor target dot.
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

    // Cursor-relative coordinate tag. Keep it detached from the key HUD and
    // let it flip around the pointer when it approaches a screen edge.
    std::wstringstream ss;
    ss << L"X " << screenPt.x << L"   Y " << screenPt.y;

    SetBkMode(hdc, TRANSPARENT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontCoord);
    SIZE textSize = {};
    std::wstring coordText = ss.str();
    GetTextExtentPoint32W(hdc, coordText.c_str(), (int)coordText.length(), &textSize);

    const int padX = UiPx(8);
    const int padY = UiPx(4);
    const int gapX = UiPx(14);
    const int gapY = UiPx(14);
    int boxW = textSize.cx + padX * 2;
    int boxH = textSize.cy + padY * 2;
    const int margin = UiPx(8);
    int x = pt.x + gapX;
    int y = pt.y + gapY;

    if (x + boxW > rc.right - margin) {
        x = pt.x - boxW - gapX;
    }
    if (y + boxH > rc.bottom - margin) {
        y = pt.y - boxH - gapY;
    }
    if (x < rc.left + margin) x = rc.left + margin;
    if (x + boxW > rc.right - margin) x = rc.right - boxW - margin;
    if (y < rc.top + margin) y = rc.top + margin;
    if (y + boxH > rc.bottom - margin) y = rc.bottom - boxH - margin;

    RECT rcTag = {x, y, x + boxW, y + boxH};
    HBRUSH hTagBg = CreateSolidBrush(RGB(18, 22, 27));
    HPEN hTagPen = CreatePen(PS_SOLID, 1, RGB(62, 132, 176));
    HBRUSH hOldTagBg = (HBRUSH)SelectObject(hdc, hTagBg);
    HPEN hOldTagPen = (HPEN)SelectObject(hdc, hTagPen);
    RoundRect(hdc, rcTag.left, rcTag.top, rcTag.right, rcTag.bottom, UiPx(10), UiPx(10));
    SelectObject(hdc, hOldTagPen);
    SelectObject(hdc, hOldTagBg);
    DeleteObject(hTagPen);
    DeleteObject(hTagBg);

    SetTextColor(hdc, RGB(245, 250, 255));
    RECT rcText = {rcTag.left + padX, rcTag.top, rcTag.right - padX, rcTag.bottom};
    DrawTextW(hdc, coordText.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
}

// =============================================================================
// WINDOW PROCEDURE: CROSSHAIR
// =============================================================================

LRESULT CALLBACK CrosshairWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            if (wParam == IDT_UPDATE_CURSOR) {
                static POINT s_lastPaintCursor = {LONG_MIN, LONG_MIN};
                static bool s_lastPreviewActive = false;
                POINT beforeCursor = {};
                GetCursorPos(&beforeCursor);

                HideCirclePreviewIfMouseMoved();
                bool previewBeforeRefresh = g_circlePreview.enabled;
                RefreshCirclePreviewFromFile();
                ProcessBackendCommandFile();
                WriteBackendState();

                bool cursorMoved = beforeCursor.x != s_lastPaintCursor.x ||
                                   beforeCursor.y != s_lastPaintCursor.y;
                bool previewChanged = previewBeforeRefresh != g_circlePreview.enabled ||
                                      s_lastPreviewActive != g_circlePreview.enabled;
                bool needsFrame = cursorMoved || previewChanged || g_circlePreview.enabled;
                if (needsFrame) {
                    s_lastPaintCursor = beforeCursor;
                    s_lastPreviewActive = g_circlePreview.enabled;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_HOTKEY: {
            if (wParam == IDH_SAVE_CURSOR) {
                if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) {
                    CloseRecordKeysDialog(g_hwndRecordDlg);
                }
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
                if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) {
                    CloseSaveCursorDialog(g_hwndSaveDlg);
                }
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
            else if (wParam == IDH_CIRCLE_PLACER) {
                StartCirclePlacement();
            }
            else if (wParam == IDH_CLICK_LEFT) {
                StartClickAction(0);
            }
            else if (wParam == IDH_CLICK_RIGHT) {
                StartClickAction(1);
            }
            else if (wParam == IDH_CLICK_MIDDLE) {
                StartClickAction(2);
            }
            else if (wParam == IDH_STOP_ALL_TASKET) {
                std::wstring status;
                StopAllTasketTasks(&status);
                PushKeyHistory({"TASKET", "STOP"});
            }
            else if (wParam == IDH_VIEW_TOGGLES) {
                g_coordPanelVisible = !IsWindowVisible(g_hwndCoordPanel);
                ShowWindow(g_hwndCoordPanel, g_coordPanelVisible ? SW_SHOW : SW_HIDE);
                PushKeyHistory({g_coordPanelVisible ? "PANEL_ON" : "PANEL_OFF"});
            }
            else if (wParam == IDH_PASTE_BUFFERS) {
                StartPasteBuffers();
            }
            else if (wParam == IDH_REGISTRY_HUB) {
                StartRegistryHub();
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
            SetTimer(hwnd, IDT_UPDATE_COORDS, g_updateMs, nullptr);
            return 0;

        case WM_KEYDOWN:
            if (wParam == '1') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_PASTE_BUFFERS, 0);
                return 0;
            }
            if (wParam == '2') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_REGISTRY_HUB, 0);
                return 0;
            }
            if (wParam == '3' || wParam == 'C') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CIRCLE_PLACER, 0);
                return 0;
            }
            if (wParam == '4') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_LEFT, 0);
                return 0;
            }
            if (wParam == '5') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_RIGHT, 0);
                return 0;
            }
            if (wParam == '6') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_MIDDLE, 0);
                return 0;
            }
            if (wParam == '7') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_STOP_ALL_TASKET, 0);
                return 0;
            }
            if (wParam == '9') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_RECORD_KEYS, 0);
                return 0;
            }
            if (wParam == '0') {
                PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_SAVE_CURSOR, 0);
                return 0;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_TIMER:
            if (wParam == IDT_UPDATE_COORDS) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_INPUT: {
            UINT size = 0;
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size,
                                sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
                return 0;
            }

            std::vector<BYTE> buffer(size);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(), &size,
                                sizeof(RAWINPUTHEADER)) != size) {
                return 0;
            }

            RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
            if (raw->header.dwType != RIM_TYPEKEYBOARD) return 0;

            RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;
            if (vk == 255) return 0;

            bool ext = (kb.Flags & RI_KEY_E0) != 0;
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
                UINT sc = kb.MakeCode;
                if (ext) sc |= 0xE000;
                UINT mapped = MapVirtualKey(sc, MAPVK_VSC_TO_VK_EX);
                if (mapped) vk = mapped;
            }

            bool dialogHasFocus = (g_hwndSaveDlg && GetForegroundWindow() == g_hwndSaveDlg) ||
                                  (g_hwndRecordDlg && GetForegroundWindow() == g_hwndRecordDlg) ||
                                  (g_hwndCircleDlg && GetForegroundWindow() == g_hwndCircleDlg) ||
                                  (g_hwndClickDlg && GetForegroundWindow() == g_hwndClickDlg) ||
                                  (g_hwndViewTogglesDlg && GetForegroundWindow() == g_hwndViewTogglesDlg) ||
                                  (g_hwndPasteBuffersDlg && GetForegroundWindow() == g_hwndPasteBuffersDlg) ||
                                  (g_hwndRegistryHubDlg && GetForegroundWindow() == g_hwndRegistryHubDlg);
            UINT message = kb.Message;
            if (!dialogHasFocus && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
                TrackDisplayKeyDown(vk, ext);
            } else if (message == WM_KEYUP || message == WM_SYSKEYUP) {
                TrackKeyUp(vk, ext);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            FillRect(hdc, &rc, g_hbrColorkey);

            RECT rcPanel = {0, 0, CoordPanelW(), CoordPanelH()};
            HBRUSH hBg = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(hdc, &rcPanel, hBg);
            DeleteObject(hBg);

            // Accent bar
            RECT rcAcc = {rcPanel.left, rcPanel.top, rcPanel.right, rcPanel.top + UiPx(3)};
            HBRUSH hAcc = CreateSolidBrush(RGB(0, 150, 255));
            FillRect(hdc, &rcAcc, hAcc);
            DeleteObject(hAcc);

            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_hFontCoord);

            // Status / hotkey breadcrumb
            SetTextColor(hdc, RGB(205, 205, 210));
            std::wstring st = g_crosshairVisible ? L"Crosshair: active" : L"Crosshair: hidden";
            st += g_gridVisible ? L"  Grid: on" : L"  Grid: off";
            st += g_coordinatesOnlyMode ? L"  Coords-only: on" : L"  Coords-only: off";
            st += L"  Screen: ";
            st += std::to_wstring(g_screenW);
            st += L"x";
            st += std::to_wstring(g_screenH);
            st += L" @ ";
            st += std::to_wstring(g_displayRefreshHz);
            st += L"Hz / ";
            st += std::to_wstring(g_updateMs);
            st += L"ms";
            st += L"    ";
            st += GetKeyHudStatusText();
            TextOutW(hdc, rc.left + UiPx(15), rc.top + UiPx(16), st.c_str(), (int)st.length());

            SetTextColor(hdc, RGB(170, 190, 205));
            std::wstring hotkeys = L"Shift+Alt: 1 Paste  2 Hub  3 Circle  4 Left  5 Right  6 Middle  7 Stop  8 Panel  9 Record  0 Save";
            TextOutW(hdc, rc.left + UiPx(15), rc.top + UiPx(40), hotkeys.c_str(), (int)hotkeys.length());

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (y >= UiPx(34) && y <= UiPx(66)) {
                if (x < UiPx(85)) {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_PASTE_BUFFERS, 0);
                } else if (x < UiPx(190)) {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_REGISTRY_HUB, 0);
                } else if (x < UiPx(285)) {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CIRCLE_PLACER, 0);
                } else if (x < UiPx(380)) {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_LEFT, 0);
                } else if (x < UiPx(470)) {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_RIGHT, 0);
                } else if (x < UiPx(560)) {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_MIDDLE, 0);
                } else {
                    PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_STOP_ALL_TASKET, 0);
                }
                return 0;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
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
                AppendMenuW(hm, MF_STRING, IDM_PASTE_BUFFERS, L"Paste Buffers (Shift+Alt+1)");
                AppendMenuW(hm, MF_STRING, IDM_RECORD_KEYS, L"Record Keys (Shift+Alt+9)");
                AppendMenuW(hm, MF_STRING, IDM_CIRCLE_PLACER, L"Circle Placement (Shift+Alt+3)");
                AppendMenuW(hm, MF_STRING, IDM_CLICK_LEFT, L"Manual Left Click (Shift+Alt+4)");
                AppendMenuW(hm, MF_STRING, IDM_CLICK_RIGHT, L"Manual Right Click (Shift+Alt+5)");
                AppendMenuW(hm, MF_STRING, IDM_CLICK_MIDDLE, L"Manual Middle Click (Shift+Alt+6)");
                AppendMenuW(hm, MF_STRING, IDM_STOP_ALL_TASKET, L"Stop All Tasket Tasks (Shift+Alt+7)");
                AppendMenuW(hm, MF_STRING, IDM_VIEW_TOGGLES, L"Display Toggles (Hub -> Q)");
                AppendMenuW(hm, MF_STRING, IDM_REGISTRY_HUB, L"Registry Hub (Shift+Alt+2)");
                AppendMenuW(hm, MF_STRING, IDM_SAVE_CURSOR, L"Save Cursor (Shift+Alt+0)");
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
                    case IDM_PASTE_BUFFERS:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_PASTE_BUFFERS, 0);
                        break;
                    case IDM_RECORD_KEYS:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_RECORD_KEYS, 0);
                        break;
                    case IDM_CIRCLE_PLACER:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CIRCLE_PLACER, 0);
                        break;
                    case IDM_CLICK_LEFT:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_LEFT, 0);
                        break;
                    case IDM_CLICK_RIGHT:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_RIGHT, 0);
                        break;
                    case IDM_CLICK_MIDDLE:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_CLICK_MIDDLE, 0);
                        break;
                    case IDM_STOP_ALL_TASKET:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_STOP_ALL_TASKET, 0);
                        break;
                    case IDM_VIEW_TOGGLES:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_VIEW_TOGGLES, 0);
                        break;
                    case IDM_REGISTRY_HUB:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_REGISTRY_HUB, 0);
                        break;
                    case IDM_SAVE_CURSOR:
                        PostMessage(g_hwndCrosshair, WM_HOTKEY, IDH_SAVE_CURSOR, 0);
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
                PollKeyboardState();
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
// DIALOG: CIRCLE PLACEMENT (modeless)
// =============================================================================

static std::wstring CircleDialogInput(HWND hwnd) {
    wchar_t value[512] = {};
    GetDlgItemTextW(hwnd, IDC_EDIT_CIRCLE_VALUE, value, 512);
    return TrimWide(value);
}

static void ClearCircleInput(HWND hwnd) {
    SetDlgItemTextW(hwnd, IDC_EDIT_CIRCLE_VALUE, L"");
    SetFocus(GetDlgItem(hwnd, IDC_EDIT_CIRCLE_VALUE));
}

static void MoveCursorToCircleTarget() {
    if (!g_circlePlacement.targetValid) return;
    POINT target = g_circlePlacement.target;
    std::wstring status;
    if (!ScheduleTasketCursorMove(target, 220, 1, &status)) {
        SetCircleStatus(status.empty() ? L"Tasket move failed. Is tasket-httpd.exe running?" : status);
        return;
    }
    ResetCirclePlacement(true);
}

static void AppendCircleCoordinateAndClose(HWND hwnd) {
    if (!g_circlePlacement.targetValid) return;
    if (!AppendCircleCoordinate()) {
        MessageBoxW(hwnd, L"Failed to append the coordinate to clicksession.txt.",
                    L"Append Failed", MB_OK | MB_ICONERROR);
        return;
    }
    ResetCirclePlacement(true);
}

static bool ApplyCircleModeFromInput(HWND hwnd) {
    std::string mode = CompactLower(CircleDialogInput(hwnd));
    if (mode == "r" || mode == "rad" || mode == "radians") {
        g_circlePlacement.angleMode = CircleAngleMode::Radians;
        SetCircleStatus(L"Radians mode selected. Type an angle like 5pi/6.");
        SetCircleStage(CircleStage::Angle);
        return true;
    }
    if (mode == "d" || mode == "deg" || mode == "degrees") {
        g_circlePlacement.angleMode = CircleAngleMode::Degrees;
        SetCircleStatus(L"Degrees mode selected. Type an angle like 150.");
        SetCircleStage(CircleStage::Angle);
        return true;
    }
    SetCircleStatus(L"Type R for radians or D for degrees, then press Enter.");
    return false;
}

static void BeginCircleZoneMode() {
    if (!g_circlePlacement.targetValid) return;
    g_circlePlacement.zoneMode = true;
    g_circlePlacement.zoneHasStart = true;
    g_circlePlacement.zoneStart = g_circlePlacement.target;
    SetZonePreview(g_circlePlacement.zoneStart, g_circlePlacement.target);
    StartNextCircleFromTarget();
}

static void EndCircleZoneMode(HWND hwnd) {
    if (!g_circlePlacement.zoneMode || !g_circlePlacement.zoneHasStart || !g_circlePlacement.targetValid) {
        SetCircleStatus(L"Zone needs a start and end target first.");
        return;
    }
    g_circlePlacement.zoneEnd = g_circlePlacement.target;
    SetZonePreview(g_circlePlacement.zoneStart, g_circlePlacement.zoneEnd);
    SetCircleStatus(L"Zone ready. Choose Z, X, C, or V.");
    SetCircleStage(CircleStage::ZoneAction);
    ClearCircleInput(hwnd);
}

static bool ScheduleCircleZoneAction(HWND hwnd, int flowIndex) {
    std::wstring status;
    if (!ScheduleTasketZoneFlow(flowIndex, g_circlePlacement.zoneStart, g_circlePlacement.zoneEnd, &status)) {
        SetCircleStatus(status.empty() ? L"Zone Tasket schedule failed." : status);
        ClearCircleInput(hwnd);
        return false;
    }
    ResetCirclePlacement(true);
    return true;
}

static bool RunCircleZoneActionShortcut(HWND hwnd) {
    std::string command = CompactLower(CircleDialogInput(hwnd));
    int flowIndex = -1;
    if (command == "z" || command == "snip" || command == "snipcopy") flowIndex = 0;
    else if (command == "x" || command == "text") flowIndex = 1;
    else if (command == "c" || command == "single" || command == "singleline") flowIndex = 2;
    else if (command == "v" || command == "copy" || command == "zonecopy") flowIndex = 3;
    else if (command == "b" || command == "buffer" || command == "zonebuffer") {
        POINT start = g_circlePlacement.zoneStart;
        POINT end = g_circlePlacement.zoneEnd;
        ResetCirclePlacement(true);
        StartPasteBuffersZoneStore(start, end);
        return true;
    } else return false;

    ScheduleCircleZoneAction(hwnd, flowIndex);
    return true;
}

static bool RunCircleConfirmShortcut(HWND hwnd) {
    std::string command = CompactLower(CircleDialogInput(hwnd));
    if (command == "z" || command == "1" || command == "move") {
        MoveCursorToCircleTarget();
        return true;
    }
    if (command == "x" || command == "2" || command == "append") {
        AppendCircleCoordinateAndClose(hwnd);
        return true;
    }
    if (command == "c" || command == "circle") {
        StartNextCircleFromTarget();
        return true;
    }
    if (command == "v" || command == "zone" || command == "zonemake") {
        if (g_circlePlacement.zoneMode) {
            EndCircleZoneMode(hwnd);
        } else {
            BeginCircleZoneMode();
        }
        return true;
    }
    return false;
}

LRESULT CALLBACK CirclePlacerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            g_hwndCircleDlg = hwnd;
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            g_circlePlacement = CirclePlacementState{};
            g_circlePlacement.active = true;
            g_circlePlacement.stage = CircleStage::Distance;
            g_circlePlacement.openedTickMs = GetTickCount();
            GetCursorPos(&g_circlePlacement.origin);
            g_circlePlacement.target = g_circlePlacement.origin;
            g_circlePlacement.cancelOnMouseMoveFrom = g_circlePlacement.origin;
            g_circlePlacement.status = L"Enter distance in Px. You can also type a vector like -2i+2j.";
            HideCirclePreview();
            UpdateCircleDialogText(hwnd);
            ClearCircleInput(hwnd);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD notify = HIWORD(wParam);

            if (id == IDC_EDIT_CIRCLE_VALUE && notify == EN_CHANGE) {
                if (g_circlePlacement.stage == CircleStage::Distance ||
                    g_circlePlacement.stage == CircleStage::Angle) {
                    UpdatePreviewFromInput(hwnd);
                } else if (g_circlePlacement.stage == CircleStage::Mode) {
                    std::string mode = CompactLower(CircleDialogInput(hwnd));
                    if (mode == "r" || mode == "d") ApplyCircleModeFromInput(hwnd);
                } else if (g_circlePlacement.stage == CircleStage::Confirm) {
                    RunCircleConfirmShortcut(hwnd);
                } else if (g_circlePlacement.stage == CircleStage::ZoneAction) {
                    RunCircleZoneActionShortcut(hwnd);
                }
                return TRUE;
            }

            switch (id) {
                case IDC_BTN_CIRCLE_MOVE_NOW:
                    if (g_circlePlacement.stage == CircleStage::ZoneAction) {
                        ScheduleCircleZoneAction(hwnd, 0);
                        return TRUE;
                    }
                    MoveCursorToCircleTarget();
                    return TRUE;

                case IDC_BTN_CIRCLE_APPEND_COORD:
                    if (g_circlePlacement.stage == CircleStage::ZoneAction) {
                        ScheduleCircleZoneAction(hwnd, 1);
                        return TRUE;
                    }
                    AppendCircleCoordinateAndClose(hwnd);
                    return TRUE;

                case IDC_BTN_CIRCLE_NEXT_CIRCLE:
                    if (g_circlePlacement.stage == CircleStage::ZoneAction) {
                        ScheduleCircleZoneAction(hwnd, 2);
                        return TRUE;
                    }
                    if (g_circlePlacement.targetValid) StartNextCircleFromTarget();
                    return TRUE;

                case IDC_BTN_CIRCLE_ZONE_ACTION:
                    if (g_circlePlacement.stage == CircleStage::ZoneAction) {
                        ScheduleCircleZoneAction(hwnd, 3);
                    } else if (g_circlePlacement.targetValid) {
                        if (g_circlePlacement.zoneMode) EndCircleZoneMode(hwnd);
                        else BeginCircleZoneMode();
                    }
                    return TRUE;

                case IDOK:
                    if (g_circlePlacement.stage == CircleStage::Distance) {
                        if (!UpdatePreviewFromInput(hwnd)) return TRUE;
                        if (g_circlePlacement.targetValid) {
                            SetCircleStatus(L"Target ready. Move it, append it, or start another circle from here.");
                            SetCircleStage(CircleStage::Confirm);
                        } else {
                            SetCircleStatus(L"Choose angle mode: type R for radians or D for degrees.");
                            SetCircleStage(CircleStage::Mode);
                        }
                        return TRUE;
                    }
                    if (g_circlePlacement.stage == CircleStage::Mode) {
                        ApplyCircleModeFromInput(hwnd);
                        return TRUE;
                    }
                    if (g_circlePlacement.stage == CircleStage::Angle) {
                        if (UpdatePreviewFromInput(hwnd)) {
                            SetCircleStatus(L"Target ready. Move it, append it, or start another circle from here.");
                            SetCircleStage(CircleStage::Confirm);
                        }
                        return TRUE;
                    }
                    if (g_circlePlacement.stage == CircleStage::Confirm) {
                        if (!RunCircleConfirmShortcut(hwnd)) {
                            SetCircleStatus(L"Type Z to move, X to append, C for another circle, or V zone.");
                        }
                        return TRUE;
                    }
                    if (g_circlePlacement.stage == CircleStage::ZoneAction) {
                        if (!RunCircleZoneActionShortcut(hwnd)) {
                            SetCircleStatus(L"Use Z snip+copy, X text, C single-line text, V zone+copy, or B buffer zone.");
                        }
                        return TRUE;
                    }
                    return TRUE;

                case IDCANCEL:
                    ResetCirclePlacement(true);
                    return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            ResetCirclePlacement(true);
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: CLICK ACTION (modeless)
// =============================================================================

LRESULT CALLBACK ClickActionDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            g_hwndClickDlg = hwnd;
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            UpdateClickActionDialogText(hwnd);
            SetTimer(hwnd, CLICK_ACTION_WATCH_TIMER, 100, nullptr);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_TIMER:
            if (wParam == CLICK_ACTION_WATCH_TIMER &&
                !g_clickActionState.nativeHeld &&
                MouseMovedPastAnchor(g_clickActionState.cancelOnMouseMoveFrom, g_clickActionState.openedTickMs)) {
                KillTimer(hwnd, CLICK_ACTION_WATCH_TIMER);
                CloseClickActionDialog(hwnd);
                return TRUE;
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_CLICK_VALUE &&
                HIWORD(wParam) == EN_CHANGE &&
                g_clickActionState.stage == ClickActionStage::Command) {
                std::wstring text = DialogText(hwnd, IDC_EDIT_CLICK_VALUE);
                if (text.size() == 1 && wcschr(L"zZxXcCvV", text[0])) {
                    RunClickActionCommand(hwnd, text);
                }
                return TRUE;
            }

            switch (LOWORD(wParam)) {
                case IDOK: {
                    RunClickActionCommand(hwnd, DialogText(hwnd, IDC_EDIT_CLICK_VALUE));
                    return TRUE;
                }
                case IDCANCEL:
                    RunClickActionCommand(hwnd, L"v");
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            CloseClickActionDialog(hwnd);
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: VIEW TOGGLES (modeless)
// =============================================================================

LRESULT CALLBACK ViewTogglesDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            g_hwndViewTogglesDlg = hwnd;
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            UpdateViewTogglesDialogText(hwnd);
            SetTimer(hwnd, VIEW_TOGGLE_WATCH_TIMER, 100, nullptr);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_TIMER:
            if (wParam == VIEW_TOGGLE_WATCH_TIMER &&
                MouseMovedPastAnchor(g_viewToggleState.cancelOnMouseMoveFrom, g_viewToggleState.openedTickMs)) {
                KillTimer(hwnd, VIEW_TOGGLE_WATCH_TIMER);
                CloseViewTogglesDialog(hwnd);
                return TRUE;
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_VIEW_VALUE && HIWORD(wParam) == EN_CHANGE) {
                std::wstring text = DialogText(hwnd, IDC_EDIT_VIEW_VALUE);
                if (text.size() == 1 && wcschr(L"zZxXcCkKvV", text[0])) {
                    RunViewToggleCommand(hwnd, text);
                }
                return TRUE;
            }

            switch (LOWORD(wParam)) {
                case IDOK:
                    RunViewToggleCommand(hwnd, DialogText(hwnd, IDC_EDIT_VIEW_VALUE));
                    return TRUE;
                case IDCANCEL:
                    RunViewToggleCommand(hwnd, L"v");
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            CloseViewTogglesDialog(hwnd);
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: PASTE BUFFERS (modeless)
// =============================================================================

LRESULT CALLBACK PasteBuffersDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            g_hwndPasteBuffersDlg = hwnd;
            ApplyDialogUiFont(hwnd);
            if (HWND edit = GetDlgItem(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE)) {
                SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);
            }
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            UpdatePasteBuffersDialogText(hwnd);
            SetTimer(hwnd, PASTE_BUFFER_WATCH_TIMER, 100, nullptr);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_TIMER:
            if (wParam == PASTE_BUFFER_WATCH_TIMER &&
                MouseMovedPastAnchor(g_pasteBufferState.cancelOnMouseMoveFrom,
                                     g_pasteBufferState.openedTickMs)) {
                KillTimer(hwnd, PASTE_BUFFER_WATCH_TIMER);
                ClosePasteBuffersDialog(hwnd);
                return TRUE;
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_PASTE_BUFFER_VALUE && HIWORD(wParam) == EN_CHANGE) {
                bool commandStage =
                    g_pasteBufferState.stage == PasteBufferStage::Command ||
                    g_pasteBufferState.stage == PasteBufferStage::PasteSelect ||
                    g_pasteBufferState.stage == PasteBufferStage::ZoneCommand ||
                    g_pasteBufferState.stage == PasteBufferStage::ZonePlaySelect ||
                    g_pasteBufferState.stage == PasteBufferStage::ZonePlayFlow ||
                    g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureZoneSelect ||
                    g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureFlow ||
                    g_pasteBufferState.stage == PasteBufferStage::ZoneCaptureTarget;
                if (commandStage) {
                    std::wstring text = DialogText(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE);
                    if (text.size() == 1 && wcschr(L"zZxXcCvVbBnN", text[0])) {
                        RunPasteBufferCommand(hwnd, text);
                    }
                    return TRUE;
                }

                if (g_pasteBufferState.stage == PasteBufferStage::Edit ||
                    g_pasteBufferState.stage == PasteBufferStage::ZoneEdit) {
                    std::wstring text = DialogText(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE);
                    int lines = g_pasteBufferState.stage == PasteBufferStage::Edit
                        ? CountPasteBufferVisibleLines(text)
                        : CountTextLines(text);
                    bool deliberateLineBreak =
                        (GetKeyState(VK_SHIFT) & 0x8000) &&
                        (GetKeyState(VK_RETURN) & 0x8000);
                    bool shouldResize = lines < g_pasteBufferState.visibleLines ||
                        (lines > g_pasteBufferState.visibleLines && deliberateLineBreak);
                    if (shouldResize) {
                        g_pasteBufferState.visibleLines = lines;
                        LayoutPasteBuffersDialog(hwnd);
                        FocusPasteBufferEdit(hwnd, false);
                    }
                    return TRUE;
                }
            }

            switch (LOWORD(wParam)) {
                case IDOK:
                    RunPasteBufferCommand(hwnd, DialogText(hwnd, IDC_EDIT_PASTE_BUFFER_VALUE));
                    return TRUE;
                case IDCANCEL:
                    RunPasteBufferCommand(hwnd, L"esc");
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            ClosePasteBuffersDialog(hwnd);
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: REGISTRY HUB (modeless)
// =============================================================================

static std::wstring DefaultRegistryFilePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(path) + L"\\Desktop\\New Text Document.txt";
    }
    return L"";
}

static std::wstring ResolveRegistryHubTransferPath(const std::wstring& configured) {
    std::wstring value = TrimWide(configured).empty() ? DefaultRegistryHubEnvironmentPath() : configured;
    return ExpandMacrohelpPath(value);
}

static std::wstring CurrentRegistryHubExportPath() {
    return ResolveRegistryHubTransferPath(g_registryExportPath);
}

static std::wstring CurrentRegistryHubImportPath() {
    return ResolveRegistryHubTransferPath(g_registryImportPath);
}

static bool ParsePointCoordinates(const std::wstring& raw, POINT& point) {
    std::vector<int> values;
    const wchar_t* p = raw.c_str();
    while (*p) {
        wchar_t* next = nullptr;
        long value = wcstol(p, &next, 10);
        if (next != p) {
            values.push_back((int)value);
            p = next;
        } else {
            ++p;
        }
    }
    if (values.size() < 2) return false;
    point = {values[0], values[1]};
    return true;
}

static std::string RegistryHubStateToText() {
    std::ostringstream out;
    out << "---SHELL X---\n" << WStringToUtf8(g_registryPowerShellPath) << "\n";
    out << "---SHELL C---\n" << WStringToUtf8(g_registryCmdPath) << "\n";
    out << "---SHELL G---\n" << WStringToUtf8(g_regeditedExePath) << "\n";
    out << "---REGISTRY---\n" << WStringToUtf8(g_registryFilePath) << "\n";
    out << "---EXPORT PATH---\n" << WStringToUtf8(g_registryExportPath) << "\n";
    out << "---IMPORT PATH---\n" << WStringToUtf8(g_registryImportPath) << "\n";
    for (int i = 0; i < 4; ++i) {
        out << "---VAR " << (char)RegistryVarKey(i) << "---\n" << WStringToUtf8(g_registryVars[i]) << "\n";
    }
    for (int i = 0; i < REGISTRY_POINT_COUNT; ++i) {
        out << "---POINT " << (i + 1) << "---\n";
        if (g_registryPointSet[i]) out << g_registryPoints[i].x << "," << g_registryPoints[i].y;
        out << "\n";
    }
    for (int i = 0; i < 4; ++i) {
        out << "---PASTE " << (char)PasteBufferSlotKey(i) << "---\n" << WStringToUtf8(g_pasteBuffers[i]) << "\n";
    }
    for (int i = 0; i < 4; ++i) {
        out << "---ZONE " << (char)PasteBufferSlotKey(i) << "---\n" << ZoneValueUtf8(i) << "\n";
    }
    out << "---SCRIPT---\n" << WStringToUtf8(g_registryScript) << "\n";
    return out.str();
}

static std::string EscapeRegistryHubExportValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

static std::string UnescapeRegistryHubExportValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool escaped = false;
    for (char ch : value) {
        if (!escaped) {
            if (ch == '\\') {
                escaped = true;
            } else {
                out.push_back(ch);
            }
            continue;
        }
        switch (ch) {
            case 'r': out.push_back('\r'); break;
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            default:
                out.push_back('\\');
                out.push_back(ch);
                break;
        }
        escaped = false;
    }
    if (escaped) out.push_back('\\');
    return out;
}

static std::string RegistryHubStageName(RegistryHubStage stage) {
    switch (stage) {
        case RegistryHubStage::Command: return "Command";
        case RegistryHubStage::Script: return "Script";
        case RegistryHubStage::Help: return "Help";
        case RegistryHubStage::RegistryPath: return "RegistryPath";
        case RegistryHubStage::ExportPath: return "ExportPath";
        case RegistryHubStage::ImportPath: return "ImportPath";
        case RegistryHubStage::PowerShellPath: return "ShellAliasX";
        case RegistryHubStage::CmdPath: return "ShellAliasC";
        case RegistryHubStage::RegeditedPath: return "ShellAliasG";
        case RegistryHubStage::VarEdit: return "VarEdit";
        case RegistryHubStage::PasteBufferSelect: return "PasteBufferSelect";
        case RegistryHubStage::PasteBufferEdit: return "PasteBufferEdit";
        case RegistryHubStage::ZoneSelect: return "ZoneSelect";
        case RegistryHubStage::ZoneEdit: return "ZoneEdit";
        case RegistryHubStage::PointEdit: return "PointEdit";
    }
    return "Unknown";
}

static bool ParseRegistryHubBoolValue(const std::string& raw) {
    std::string value = LowerAscii(TrimAscii(raw));
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

static void WriteRegistryHubExportLine(std::ostringstream& out, const std::string& name, const std::string& value) {
    out << name << "=" << EscapeRegistryHubExportValue(value) << "\n";
}

static std::string RegistryHubEnvironmentToText() {
    std::ostringstream out;
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char stamp[64] = {};
    sprintf_s(stamp, "%04u-%02u-%02u %02u:%02u:%02u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    WriteRegistryHubExportLine(out, "MacrohelpRegistryHubExportVersion", "1");
    WriteRegistryHubExportLine(out, "GeneratedLocal", stamp);
    WriteRegistryHubExportLine(out, "ExportPath", WStringToUtf8(g_registryExportPath));
    WriteRegistryHubExportLine(out, "ImportPath", WStringToUtf8(g_registryImportPath));
    WriteRegistryHubExportLine(out, "ResolvedExportPath", WStringToUtf8(CurrentRegistryHubExportPath()));
    WriteRegistryHubExportLine(out, "ResolvedImportPath", WStringToUtf8(CurrentRegistryHubImportPath()));
    WriteRegistryHubExportLine(out, "StatePath", WStringToUtf8(GetMacrohelpRegistryHubStatePath()));
    WriteRegistryHubExportLine(out, "RegistryFilePath", WStringToUtf8(g_registryFilePath));
    WriteRegistryHubExportLine(out, "ShellAliasX", WStringToUtf8(g_registryPowerShellPath));
    WriteRegistryHubExportLine(out, "ShellAliasC", WStringToUtf8(g_registryCmdPath));
    WriteRegistryHubExportLine(out, "ShellAliasG", WStringToUtf8(g_regeditedExePath));
    WriteRegistryHubExportLine(out, "Script", WStringToUtf8(g_registryScript));
    WriteRegistryHubExportLine(out, "Status", WStringToUtf8(g_registryHubState.status));
    WriteRegistryHubExportLine(out, "LastTasketTaskNumber", std::to_string(g_lastTasketTaskNumber));
    WriteRegistryHubExportLine(out, "LastTasketTaskState", WStringToUtf8(g_lastTasketTaskState));
    WriteRegistryHubExportLine(out, "LastTasketTaskMessage", WStringToUtf8(g_lastTasketTaskMessage));
    WriteRegistryHubExportLine(out, "RegistryHubStage", RegistryHubStageName(g_registryHubState.stage));
    WriteRegistryHubExportLine(out, "ActiveVar", std::to_string(g_registryHubState.activeVar));
    WriteRegistryHubExportLine(out, "ActivePasteSlot", std::to_string(g_registryHubState.activePasteSlot));
    WriteRegistryHubExportLine(out, "ActiveZoneSlot", std::to_string(g_registryHubState.activeZoneSlot));
    WriteRegistryHubExportLine(out, "ActivePoint", std::to_string(g_registryHubState.activePoint));
    WriteRegistryHubExportLine(out, "VisibleLines", std::to_string(g_registryHubState.visibleLines));
    WriteRegistryHubExportLine(out, "CrosshairVisible", g_crosshairVisible ? "1" : "0");
    WriteRegistryHubExportLine(out, "CoordinatePanelVisible", g_coordPanelVisible ? "1" : "0");
    WriteRegistryHubExportLine(out, "KeyHudVisible", g_keyDisplayVisible ? "1" : "0");
    WriteRegistryHubExportLine(out, "GridVisible", g_gridVisible ? "1" : "0");
    WriteRegistryHubExportLine(out, "CoordinatesOnlyMode", g_coordinatesOnlyMode ? "1" : "0");

    for (int i = 0; i < 4; ++i) {
        char key = (char)RegistryVarKey(i);
        WriteRegistryHubExportLine(out, std::string("Var") + key, WStringToUtf8(g_registryVars[i]));
    }
    for (int i = 0; i < 4; ++i) {
        char key = (char)PasteBufferSlotKey(i);
        WriteRegistryHubExportLine(out, std::string("Paste") + key, WStringToUtf8(g_pasteBuffers[i]));
    }
    for (int i = 0; i < 4; ++i) {
        char key = (char)PasteBufferSlotKey(i);
        WriteRegistryHubExportLine(out, std::string("Zone") + key + "Set", g_zoneBuffers[i].set ? "1" : "0");
        WriteRegistryHubExportLine(out, std::string("Zone") + key, ZoneValueUtf8(i));
    }
    for (int i = 0; i < REGISTRY_POINT_COUNT; ++i) {
        int pointNumber = i + 1;
        WriteRegistryHubExportLine(out, "Point" + std::to_string(pointNumber) + "Set", g_registryPointSet[i] ? "1" : "0");
        WriteRegistryHubExportLine(out, "Point" + std::to_string(pointNumber), PointValueUtf8(i));
    }
    return out.str();
}

static bool ExportRegistryHubEnvironment(std::wstring* status) {
    std::wstring path = CurrentRegistryHubExportPath();
    if (!WriteUtf8File(path, RegistryHubEnvironmentToText())) {
        if (status) *status = L"Failed to write export file: " + path;
        return false;
    }
    if (status) *status = L"Exported Registry Hub environment: " + path;
    return true;
}

static bool ImportRegistryHubEnvironment(std::wstring* status) {
    std::wstring path = CurrentRegistryHubImportPath();
    std::string data = ReadUtf8File(path);
    if (data.empty()) {
        if (status) *status = L"Export file is missing or empty: " + path;
        return false;
    }

    std::istringstream in(data);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = TrimAscii(line.substr(0, eq));
        std::string value = UnescapeRegistryHubExportValue(line.substr(eq + 1));
        std::string key = LowerAscii(name);
        std::wstring wide = Utf8ToWString(value);

        if (key == "registryfilepath") g_registryFilePath = TrimWide(wide).empty() ? DefaultRegistryFilePath() : ExpandMacrohelpPath(wide);
        else if (key == "exportpath") g_registryExportPath = TrimWide(wide).empty() ? DefaultRegistryHubEnvironmentPath() : wide;
        else if (key == "importpath") g_registryImportPath = TrimWide(wide).empty() ? DefaultRegistryHubEnvironmentPath() : wide;
        else if (key == "shellaliasx") g_registryPowerShellPath = wide;
        else if (key == "shellaliasc") g_registryCmdPath = TrimWide(wide).empty() ? L"cmd" : wide;
        else if (key == "shellaliasg") g_regeditedExePath = wide;
        else if (key == "script") g_registryScript = wide;
        else if (key == "status") g_registryHubState.status = wide;
        else if (key == "lasttaskettasknumber") g_lastTasketTaskNumber = atoi(value.c_str());
        else if (key == "lasttaskettaskstate") g_lastTasketTaskState = wide;
        else if (key == "lasttaskettaskmessage") g_lastTasketTaskMessage = wide;
        else if (key == "crosshairvisible") g_crosshairVisible = ParseRegistryHubBoolValue(value);
        else if (key == "coordinatepanelvisible") g_coordPanelVisible = ParseRegistryHubBoolValue(value);
        else if (key == "keyhudvisible") g_keyDisplayVisible = ParseRegistryHubBoolValue(value);
        else if (key == "gridvisible") g_gridVisible = ParseRegistryHubBoolValue(value);
        else if (key == "coordinatesonlymode") g_coordinatesOnlyMode = ParseRegistryHubBoolValue(value);
        else if (key.size() == 4 && key.rfind("var", 0) == 0) {
            int slot = RegistryVarIndexFromCommand(std::string(1, key[3]));
            if (slot >= 0) g_registryVars[slot] = wide;
        } else if (key.size() == 6 && key.rfind("paste", 0) == 0) {
            int slot = PasteBufferSlotFromCommand(std::string(1, key[5]));
            if (slot >= 0) g_pasteBuffers[slot] = wide;
        } else if (key.size() == 8 && key.rfind("zone", 0) == 0 && key.substr(5) == "set") {
            int slot = PasteBufferSlotFromCommand(std::string(1, key[4]));
            if (slot >= 0) g_zoneBuffers[slot].set = ParseRegistryHubBoolValue(value);
        } else if (key.size() == 5 && key.rfind("zone", 0) == 0) {
            int slot = PasteBufferSlotFromCommand(std::string(1, key[4]));
            POINT start = {}, end = {};
            if (slot >= 0 && ParseZoneBufferCoordinates(wide, start, end)) {
                g_zoneBuffers[slot].set = true;
                g_zoneBuffers[slot].start = start;
                g_zoneBuffers[slot].end = end;
            }
        } else if (key.rfind("point", 0) == 0 && key.size() > 8 && key.substr(key.size() - 3) == "set") {
            int point = RegistryPointIndexFromCommand(key.substr(0, key.size() - 3));
            if (point >= 0 && point < REGISTRY_POINT_COUNT) g_registryPointSet[point] = ParseRegistryHubBoolValue(value);
        } else if (key.rfind("point", 0) == 0) {
            int point = RegistryPointIndexFromCommand(key);
            POINT parsed = {};
            if (point >= 0 && point < REGISTRY_POINT_COUNT && ParsePointCoordinates(wide, parsed)) {
                g_registryPointSet[point] = true;
                g_registryPoints[point] = parsed;
            }
        }
    }

    SaveRegistryHubState();
    if (g_hwndCrosshair && IsWindow(g_hwndCrosshair)) ShowWindow(g_hwndCrosshair, g_crosshairVisible ? SW_SHOW : SW_HIDE);
    if (g_hwndCoordPanel && IsWindow(g_hwndCoordPanel)) ShowWindow(g_hwndCoordPanel, g_coordPanelVisible ? SW_SHOW : SW_HIDE);
    if (g_hwndKeyDisplay && IsWindow(g_hwndKeyDisplay)) ShowWindow(g_hwndKeyDisplay, g_keyDisplayVisible ? SW_SHOW : SW_HIDE);
    if (status) *status = L"Imported Registry Hub environment: " + path;
    return true;
}

static void SaveRegistryHubState() {
    WriteUtf8File(GetMacrohelpRegistryHubStatePath(), RegistryHubStateToText());
}

static void LoadRegistryHubState() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    g_registryPowerShellPath.clear();
    g_registryCmdPath = L"cmd";
    g_regeditedExePath.clear();
    g_registryFilePath = DefaultRegistryFilePath();
    g_registryExportPath = DefaultRegistryHubEnvironmentPath();
    g_registryImportPath = DefaultRegistryHubEnvironmentPath();

    std::string data = ReadUtf8File(GetMacrohelpRegistryHubStatePath());
    if (data.empty()) return;
    auto section = [&](const std::string& marker) {
        return Utf8ToWString(SectionTextAfterMarker(data, marker));
    };

    std::wstring value = section("---SHELL X---");
    if (TrimWide(value).empty()) value = section("---POWERSHELL---");
    if (!TrimWide(value).empty()) {
        std::wstring trimmed = TrimWide(value);
        g_registryPowerShellPath = (_wcsicmp(trimmed.c_str(), L"pwsh.exe") == 0) ? L"" : trimmed;
    }
    value = section("---SHELL C---");
    if (TrimWide(value).empty()) value = section("---CMD---");
    if (!TrimWide(value).empty()) {
        std::wstring trimmed = TrimWide(value);
        g_registryCmdPath = (_wcsicmp(trimmed.c_str(), L"cmd.exe") == 0) ? L"cmd" : trimmed;
    }
    value = section("---SHELL G---");
    if (TrimWide(value).empty()) value = section("---REGEDITED---");
    if (!TrimWide(value).empty()) {
        std::wstring trimmed = TrimWide(value);
        g_regeditedExePath = (_wcsicmp(trimmed.c_str(), L"regedited.exe") == 0) ? L"" : trimmed;
    }
    value = section("---REGISTRY---");
    if (!TrimWide(value).empty()) g_registryFilePath = ExpandMacrohelpPath(value);
    value = section("---EXPORT PATH---");
    if (!TrimWide(value).empty()) g_registryExportPath = value;
    value = section("---IMPORT PATH---");
    if (!TrimWide(value).empty()) g_registryImportPath = value;

    for (int i = 0; i < 4; ++i) {
        std::string key(1, (char)RegistryVarKey(i));
        g_registryVars[i] = section("---VAR " + key + "---");
    }
    for (int i = 0; i < 4; ++i) {
        std::string key(1, (char)PasteBufferSlotKey(i));
        g_pasteBuffers[i] = section("---PASTE " + key + "---");
        POINT start = {}, end = {};
        if (ParseZoneBufferCoordinates(section("---ZONE " + key + "---"), start, end)) {
            g_zoneBuffers[i].set = true;
            g_zoneBuffers[i].start = start;
            g_zoneBuffers[i].end = end;
        }
    }
    for (int i = 0; i < REGISTRY_POINT_COUNT; ++i) {
        POINT pt = {};
        if (ParsePointCoordinates(section("---POINT " + std::to_string(i + 1) + "---"), pt)) {
            g_registryPointSet[i] = true;
            g_registryPoints[i] = pt;
        }
    }
    g_registryScript = section("---SCRIPT---");
}

static void ClearRegistryHubState() {
    g_registryScript.clear();
    for (auto& value : g_registryVars) value.clear();
    for (int i = 0; i < REGISTRY_POINT_COUNT; ++i) {
        g_registryPointSet[i] = false;
        g_registryPoints[i] = {0, 0};
    }
    g_registryPowerShellPath.clear();
    g_registryCmdPath = L"cmd";
    g_regeditedExePath.clear();
    g_registryFilePath = DefaultRegistryFilePath();
    g_registryExportPath = DefaultRegistryHubEnvironmentPath();
    g_registryImportPath = DefaultRegistryHubEnvironmentPath();
    SaveRegistryHubState();
}

static std::wstring RegistryHubHelpText() {
    return
        L"Registry Hub Field Manual\r\n"
        L"=========================\r\n\r\n"
        L"Purpose\r\n"
        L"-------\r\n"
        L"Registry Hub is a Macrohelp command surface that translates small brace tokens into native Tasket schedules.\r\n"
        L"It should stay lightweight: Macrohelp builds Tasket JSON, sends it to tasket-httpd, and lets Tasket perform the real keyboard, mouse, paste, and schedule work.\r\n"
        L"The normal verb is Play. Low-level .scht saving exists only for debugging and future-proofing against Tasket format changes.\r\n\r\n"
        L"Router Keys Before Opening The Script Editor\r\n"
        L"--------------------------------------------\r\n"
        L"Q  Open the visibility/display toggles. In that screen, Z toggles the target, X toggles gridlines, C leaves only cursor coordinates, and K toggles key visibility.\r\n"
        L"Z  Open the workflow script editor.\r\n"
        L"P  Play the current workflow immediately, then hide the Registry Hub window on success.\r\n"
        L"L  Clear Registry Hub script-local state. Paste buffers and zone buffers remain available.\r\n"
        L"H  Open this help/manual screen.\r\n"
        L"R  Set the registry file path used by scripts and exports.\r\n"
        L"E  Set the plaintext export path. Default is %USERPROFILE%\\Desktop\\temps\\macrohelp-registry-hub-environment.txt.\r\n"
        L"I  Set the plaintext import path. Default is the same Desktop\\temps file.\r\n"
        L"X  Set shell alias X. Example contents: bash\r\n"
        L"C  Set shell alias C. Blank resets to cmd.\r\n"
        L"G  Set shell alias G. Example contents: uv run python\r\n"
        L"A/S/D/F  Set script-local multiline variables. Shift+Enter adds lines; Enter saves.\r\n"
        L"N  Set zone buffers Z/X/C/V as x1,y1,x2,y2, as two point references such as 1,2, or by copying another zone slot.\r\n"
        L"1/2/3/4/5/6  Set visible important points as x,y. Type S while editing a point to save the current cursor coordinate.\r\n"
        L"Esc  Close and preserve state.\r\n\r\n"
        L"Script Editor Buttons\r\n"
        L"---------------------\r\n"
        L"Clear      Clears Registry Hub script-local state and returns to a clean script editor.\r\n"
        L"Export     Writes a plaintext environment file to the configured export path.\r\n"
        L"Import     Reads a plaintext environment file from the configured import path.\r\n"
        L"Save Schts Writes the compiled Tasket .scht file and manifest to Desktop\\temps for inspection.\r\n"
        L"Play       Schedules the compiled Tasket flow through tasket-httpd.\r\n\r\n"
        L"Visibility Toggles From Q\r\n"
        L"-------------------------\r\n"
        L"Q opens the visibility screen from the Registry Hub router. It is the only router key for display toggles.\r\n"
        L"Inside Q, Z toggles the target/crosshair window.\r\n"
        L"Inside Q, X toggles the cursor-centered gridlines.\r\n"
        L"Inside Q, C toggles coordinates-only mode, leaving the coordinate tag while hiding target/grid drawing.\r\n"
        L"Inside Q, K toggles the key-press HUD visibility. This mirrors the tray option without leaving the keyboard flow.\r\n\r\n"
        L"Text Buffers And Variables\r\n"
        L"--------------------------\r\n"
        L"A/S/D/F are Registry Hub script variables. They are edited from the Registry Hub router.\r\n"
        L"Z/X/C/V are the main paste buffers owned by Shift+Alt+1. Registry Hub can read or set them, but it does not replace that menu.\r\n"
        L"Zone buffers also use Z/X/C/V names, but they are separate physical coordinate ranges, not stored paste text.\r\n"
        L"$A, $S, $D, $F, $Z, $X, $C, and $V resolve to the current buffer text inside {set ...} and condition tokens.\r\n"
        L"{A} or {A,Z,C} pastes listed buffers with small Tasket waits. By default it does not click first, so focused shells stay focused.\r\n"
        L"{enter A,Z} pastes the listed buffers and presses Enter afterward. It also defaults to no-click.\r\n"
        L"{paste A} is an explicit no-click paste token for readability.\r\n"
        L"{click on} enables prepended focus-clicks for following bare buffer and {enter ...} tokens.\r\n"
        L"{click off} disables prepended focus-clicks again. Registry Hub starts in click-off mode every Play.\r\n\r\n"
        L"Setting Values\r\n"
        L"--------------\r\n"
        L"{set A literal text}\r\n"
        L"{set S $Z}\r\n"
        L"{set paste Z literal text}\r\n"
        L"{set Z $A}\r\n"
        L"{set zone Z 100,200,500,600}\r\n"
        L"{set zone Z 1,2} still sets zone slot Z; the two-value comma list means start at point 1 and end at point 2.\r\n"
        L"{set zone Z X} copies existing physical zone X into physical zone Z. Zone slots remain only Z/X/C/V.\r\n"
        L"{set point 3 1200,700}\r\n"
        L"{set point 4 3} copies important point 3 into important point 4.\r\n"
        L"{set point 5 $3} also works; $1 through $16 resolve to important-point coordinates.\r\n"
        L"{set registry %USERPROFILE%\\Desktop\\New Text Document.txt}\r\n"
        L"{set shell X bash}\r\n"
        L"{set shell C cmd}\r\n"
        L"{set shell G uv run python}\r\n\r\n"
        L"Branching\r\n"
        L"---------\r\n"
        L"{if A contains waterfront} ... {else} ... {endif}\r\n"
        L"{if A notcontains done} ... {endif}\r\n"
        L"{if A == $Z} ... {endif}\r\n"
        L"{if A != text} ... {endif}\r\n"
        L"{if A > 5} ... {endif}\r\n"
        L"{if A >= 5} ... {endif}\r\n"
        L"{if A < 5} ... {endif}\r\n"
        L"{if A <= 5} ... {endif}\r\n"
        L"{if A empty} ... {endif}\r\n"
        L"{if A notempty} ... {endif}\r\n\r\n"
        L"Daemon-Backed Branching\r\n"
        L"-----------------------\r\n"
        L"Tasket-httpd exposes task status and saved-task lists. Registry Hub can query those during compile-time branching.\r\n"
        L"These checks happen when you press Play or Save Schts, before the final Tasket schedule is emitted.\r\n"
        L"They are best for deciding whether to include or skip the next native Tasket actions based on a task that already exists in the daemon ledger.\r\n"
        L"They do not pause in the middle of the same schedule waiting for a child action to finish; use explicit waits and a later Play if you need a second-stage poll.\r\n"
        L"Macrohelp remembers the newest task_number returned by /temp-task. That is what task last points at.\r\n\r\n"
        L"{if task last is finished} ... {endif}\r\n"
        L"{if task 12 is running} ... {endif}\r\n"
        L"{if task 12 state == failed} ... {endif}\r\n"
        L"{if task 12 remaining <= 2} ... {endif}\r\n"
        L"{if task 12 message contains finished} ... {endif}\r\n"
        L"{if saved-task MyMacro exists} ... {endif}\r\n"
        L"{if saved-task MyMacro missing} ... {endif}\r\n"
        L"{if saved-tasks count > 10} ... {endif}\r\n"
        L"saved-task reads the live /tasks list, so new .scht files appear without restarting Macrohelp.\r\n"
        L"saved-tasks count is useful as a sanity gate before playing a workflow that expects a populated Tasket library.\r\n"
        L"Last task number, state, and message are included in Export/Import so a saved environment can remember the last known daemon branch point.\r\n\r\n"
        L"Pointer And Click Tokens\r\n"
        L"------------------------\r\n"
        L"{wait 200} waits 200 milliseconds.\r\n"
        L"{point 1} through {point 16} move through the canonical Tasket cursor-move template when that point is set.\r\n"
        L"{click} or {left} emits a left-click action.\r\n"
        L"{click on} / {click off} changes whether buffer paste tokens click before pasting.\r\n"
        L"{right} emits a right-click action.\r\n"
        L"{middle} emits a middle-click action.\r\n"
        L"{ALT_LEFT F4} and similar unknown brace tokens become Tasket key chords.\r\n"
        L"Chord aliases are normalized before JSON output: left/right/up/down become arrow keys; LEFT_SHIFT, left_shift, shift_left, ctrl, alt, win, esc, pgdn, and similar names become Tasket names.\r\n"
        L"{ctrlmod right 3} emits Ctrl+Right three times. {controlmod right 3} is the same alias.\r\n"
        L"{shiftmod up 3} emits Shift+Up three times. Counts are clamped to a sane maximum.\r\n\r\n"
        L"Shell Tokens\r\n"
        L"------------\r\n"
        L"{powershell} opens Windows Terminal / PowerShell Preview with Win+Alt+Space, waits, then presses Enter.\r\n"
        L"{shell X} pastes shell alias X and presses Enter.\r\n"
        L"{shell C} pastes shell alias C and presses Enter.\r\n"
        L"{shell G} pastes shell alias G and presses Enter.\r\n"
        L"{cmd}, {bash}, {python}, and {py} are shortcuts for the configured shell aliases.\r\n"
        L"{exec ...}, {system ...}, and {sys ...} are intentionally disabled here. Use visible shell primitives and paste buffers instead.\r\n\r\n"
        L"Task And Schedule Tokens\r\n"
        L"------------------------\r\n"
        L"{play SomeTask.scht} emits a Tasket running-other-task action.\r\n"
        L"{play SomeTask} also works; the .scht suffix is normalized away for Tasket.\r\n"
        L"Use Save Schts to inspect the exact generated schedule before relying on a complex workflow.\r\n\r\n"
        L"Zone Tokens\r\n"
        L"-----------\r\n"
        L"Zone buffers are physical coordinate ranges. A valid zone is either x1,y1,x2,y2, two point references, or a copy of another zone.\r\n"
        L"Zone slot names are Z/X/C/V. Uppercase and lowercase are accepted in script tokens.\r\n"
        L"Zone methods match the circle-zone menu:\r\n"
        L"Z = image snip/copy flow.\r\n"
        L"X = text extractor flow.\r\n"
        L"C = single-line text extractor flow.\r\n"
        L"V = drag-copy flow.\r\n"
        L"{zone Z,X} plays zone buffer Z using method X.\r\n"
        L"{zone Z,C} plays zone buffer Z using method C.\r\n"
        L"{zone V,V} plays zone buffer V using drag-copy.\r\n"
        L"{zonebuf Z,X,V} captures zone Z using method X, opens the paste-buffer HUD, and saves the clipboard into paste buffer V.\r\n"
        L"Image capture cannot be saved into a text paste buffer, so zonebuf supports X, C, and V only.\r\n\r\n"
        L"Export / Import File\r\n"
        L"--------------------\r\n"
        L"The plaintext environment export stores registry path, import/export paths, shell aliases, script text, A/S/D/F vars, Z/X/C/V paste buffers, zone buffers, points 1-16, Q visibility toggles, coordinates-only mode, and last status.\r\n"
        L"Blank values are intentional. They make the file stable and easy to diff or edit by hand.\r\n\r\n"
        L"Examples\r\n"
        L"--------\r\n"
        L"{powershell}{wait 700}{paste A}{ENTER}{wait 300}{ALT_LEFT F4}\r\n"
        L"{powershell}{wait 700}{paste A}{ctrlmod right 3}{shiftmod up 2}{ALT_LEFT F4}\r\n"
        L"{set A $Z}{if A contains waterfront}{enter A}{else}{enter D}{endif}\r\n"
        L"{set zone Z 100,100,600,400}{zonebuf Z,C,V}{if V contains approved}{play FollowupTask}{endif}\r\n"
        L"{if saved-task OpenDailyFocus exists}{play OpenDailyFocus}{endif}\r\n"
        L"{if saved-tasks count > 10}{play MorningStartup}{else}{enter D}{endif}\r\n"
        L"{if task last is finished}{enter S}{else}{enter D}{endif}\r\n";
}

static void LayoutRegistryHubDialog(HWND hwnd) {
    if (!hwnd) return;
    bool large = g_registryHubState.stage == RegistryHubStage::Script ||
                 g_registryHubState.stage == RegistryHubStage::Help;
    bool growEdit = RegistryHubStageUsesGrowEdit(g_registryHubState.stage);
    bool showRun = g_registryHubState.stage == RegistryHubStage::Script;
    bool showUtilityButtons = showRun;
    int width = large ? UiPx(780) : UiPx(680);
    int editHeight = large ? UiPx(390) : UiPx(26);
    if (!large && growEdit) {
        int lines = std::max(1, g_registryHubState.visibleLines);
        editHeight = UiPx(26) + (lines - 1) * UiPx(20);
        if (editHeight > UiPx(164)) editHeight = UiPx(164);
    }
    int height = large ? UiPx(510) : (UiPx(132) + (editHeight - UiPx(26)));
    const int margin = UiPx(12);
    int x = g_screenX + (g_screenW - width) / 2;
    int y = large ? (g_screenY + (g_screenH - height) / 2) : (g_screenY + g_screenH - height - UiPx(42));
    if (x < g_screenX + margin) x = g_screenX + margin;
    if (x + width > g_screenX + g_screenW - margin) x = g_screenX + g_screenW - width - margin;
    if (y < g_screenY + margin) y = g_screenY + margin;
    if (y + height > g_screenY + g_screenH - margin) y = g_screenY + g_screenH - height - margin;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT), UiPx(14), UiPx(10), width - UiPx(28), UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE), UiPx(14), UiPx(30), width - UiPx(28), editHeight, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_REGISTRY_HUB_HINT), UiPx(14), UiPx(40) + editHeight, width - UiPx(28), large ? UiPx(34) : UiPx(18), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS), UiPx(14), large ? (UiPx(78) + editHeight) : (UiPx(62) + editHeight), width - (showRun ? UiPx(128) : UiPx(28)), large ? UiPx(38) : UiPx(20), TRUE);
    ShowDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_CLEAR, showUtilityButtons);
    ShowDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_EXPORT, showUtilityButtons);
    ShowDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_IMPORT, showUtilityButtons);
    ShowDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_SAVE_SCHTS, showUtilityButtons);
    ShowDlgItem(hwnd, IDOK, showRun);
    if (showUtilityButtons) {
        int buttonY = height - UiPx(38);
        int buttonW = UiPx(96);
        int gap = UiPx(8);
        int totalW = buttonW * 5 + gap * 4;
        int startX = width - totalW - UiPx(14);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_CLEAR), startX, buttonY, buttonW, UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_EXPORT), startX + buttonW + gap, buttonY, buttonW, UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_IMPORT), startX + (buttonW + gap) * 2, buttonY, buttonW, UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_SAVE_SCHTS), startX + (buttonW + gap) * 3, buttonY, buttonW, UiPx(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDOK), startX + (buttonW + gap) * 4, buttonY, buttonW, UiPx(26), TRUE);
    } else {
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_CLEAR), -200, -200, 1, 1, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_EXPORT), -200, -200, 1, 1, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_IMPORT), -200, -200, 1, 1, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_REGISTRY_HUB_SAVE_SCHTS), -200, -200, 1, 1, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDOK), -200, -200, 1, 1, TRUE);
    }
    ShowDlgItem(hwnd, IDCANCEL, false);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), -200, -200, 1, 1, TRUE);
}

static void FocusRegistryHubEdit(HWND hwnd, bool selectAll = true) {
    HWND edit = GetDlgItem(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
    if (!edit) return;
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(edit);
    if (selectAll) SendMessageW(edit, EM_SETSEL, 0, -1);
    else SendMessageW(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
}

static void ResetRegistryHubCommandMouseAnchor() {
    GetCursorPos(&g_registryHubState.cancelOnMouseMoveFrom);
    g_registryHubState.openedTickMs = GetTickCount();
}

static void UpdateRegistryHubDialogText(HWND hwnd) {
    if (!hwnd) return;
    HWND edit = GetDlgItem(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
    if (edit) {
        SendMessageW(edit, EM_SETREADONLY, g_registryHubState.stage == RegistryHubStage::Help ? TRUE : FALSE, 0);
        SendMessageW(edit, WM_SETFONT, (WPARAM)(g_registryHubState.stage == RegistryHubStage::Help ? g_hFontMono : g_hFontCoord), TRUE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(UiPx(8), UiPx(8)));
    }

    switch (g_registryHubState.stage) {
        case RegistryHubStage::Command:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Registry Hub");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT,
                L"Q view | Z script | P play | E/I paths | L clear | H help | R registry | X/C/G shells | A/S/D/F vars | N zones | 1-6 pts");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS,
                g_registryHubState.status.empty() ? L"Type one command key. Escape closes and preserves state." : g_registryHubState.status.c_str());
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, L"");
            break;
        case RegistryHubStage::Script:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Workflow script");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Write brace tokens. Return saves to router. Shift+Enter inserts a line. Click Play to schedule through Tasket.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS,
                g_registryHubState.status.empty() ? L"Example: {powershell}{wait 700}{paste A}{ENTER}" : g_registryHubState.status.c_str());
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryScript.c_str());
            SetDlgItemTextW(hwnd, IDOK, L"&Play");
            break;
        case RegistryHubStage::Help:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Registry Hub help");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter returns to router. Escape closes and preserves state.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"All supported runtime pieces are listed here.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, RegistryHubHelpText().c_str());
            break;
        case RegistryHubStage::RegistryPath:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Registry file path");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Leave blank for default Desktop\\New Text Document.txt. Enter saves.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Supports ~, %, $USERPROFILE, $env:USERPROFILE.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryFilePath.c_str());
            break;
        case RegistryHubStage::ExportPath:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Export path");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter saves. Button Export writes plaintext environment here.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Default: %USERPROFILE%\\Desktop\\temps\\macrohelp-registry-hub-environment.txt");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryExportPath.c_str());
            break;
        case RegistryHubStage::ImportPath:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Import path");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter saves. Button Import reads plaintext environment from here.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Default: %USERPROFILE%\\Desktop\\temps\\macrohelp-registry-hub-environment.txt");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryImportPath.c_str());
            break;
        case RegistryHubStage::PowerShellPath:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Shell alias X");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Optional text pasted into PowerShell after {shell X}. Example: bash");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Leave blank for no nested shell. {bash} is shortcut for {shell X}.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryPowerShellPath.c_str());
            break;
        case RegistryHubStage::CmdPath:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Shell alias C");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Text pasted into PowerShell after {shell C}. Blank resets to cmd.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"{cmd} is shortcut for {shell C}.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryCmdPath.c_str());
            break;
        case RegistryHubStage::RegeditedPath:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Shell alias G");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Optional text pasted into PowerShell after {shell G}. Example: uv run python");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"{python} and {py} are shortcuts for {shell G}.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_regeditedExePath.c_str());
            break;
        case RegistryHubStage::VarEdit: {
            std::wstring prompt = L"Script var ";
            prompt.push_back(RegistryVarKey(g_registryHubState.activeVar));
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, prompt.c_str());
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter saves. These vars are script-local A/S/D/F and may alias copied data manually.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Use as {A}, {S}, {D}, {F}, or in comma lists.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_registryVars[g_registryHubState.activeVar].c_str());
            break;
        }
        case RegistryHubStage::PasteBufferSelect:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Paste buffer");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Choose Z, X, C, or V.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"These are the same paste buffers used by Shift+Alt+1.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, L"");
            break;
        case RegistryHubStage::PasteBufferEdit: {
            std::wstring prompt = PasteBufferSlotLabel(g_registryHubState.activePasteSlot);
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, prompt.c_str());
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter saves. Use Shift+Enter for line breaks if needed.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Use as {Z}, {X}, {C}, {V}, or comma lists.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, g_pasteBuffers[g_registryHubState.activePasteSlot].c_str());
            break;
        }
        case RegistryHubStage::ZoneSelect:
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, L"Zone buffer");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Choose Z, X, C, or V.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Stores x1,y1,x2,y2. These are shared with Shift+Alt+1 -> N.");
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, L"");
            break;
        case RegistryHubStage::ZoneEdit: {
            std::wstring prompt = ZoneBufferSlotLabel(g_registryHubState.activeZoneSlot);
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, prompt.c_str());
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter x1,y1,x2,y2, pointA,pointB, or another zone slot.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"A zone needs four coords; two items mean points, one Z/X/C/V means copy that zone.");
            std::wstring value = g_zoneBuffers[g_registryHubState.activeZoneSlot].set ?
                FormatZoneBufferCoordinates(g_zoneBuffers[g_registryHubState.activeZoneSlot].start, g_zoneBuffers[g_registryHubState.activeZoneSlot].end) : L"";
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, value.c_str());
            break;
        }
        case RegistryHubStage::PointEdit: {
            std::wstring prompt = L"Important point ";
            prompt += std::to_wstring(g_registryHubState.activePoint + 1);
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_PROMPT, prompt.c_str());
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_HINT, L"Enter x,y and press Enter. Type S to save current cursor position.");
            SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Use as {point N}, {set point N ...}, or {set zone Z 1,2}.");
            std::wstring value;
            if (g_registryPointSet[g_registryHubState.activePoint]) {
                value = Utf8ToWString(PointValueUtf8(g_registryHubState.activePoint));
            }
            SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, value.c_str());
            break;
        }
    }
    LayoutRegistryHubDialog(hwnd);
    FocusRegistryHubEdit(hwnd, g_registryHubState.stage != RegistryHubStage::Script &&
                                g_registryHubState.stage != RegistryHubStage::Help);
}

static bool SaveRegistryHubCurrentStage(HWND hwnd) {
    std::wstring value = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
    switch (g_registryHubState.stage) {
        case RegistryHubStage::Script:
            g_registryScript = value;
            g_registryHubState.stage = RegistryHubStage::Command;
            g_registryHubState.status = L"Script saved.";
            ResetRegistryHubCommandMouseAnchor();
            SaveRegistryHubState();
            UpdateRegistryHubDialogText(hwnd);
            return true;
        case RegistryHubStage::RegistryPath:
            g_registryFilePath = TrimWide(value).empty() ? DefaultRegistryFilePath() : ExpandMacrohelpPath(value);
            break;
        case RegistryHubStage::ExportPath:
            g_registryExportPath = TrimWide(value).empty() ? DefaultRegistryHubEnvironmentPath() : TrimWide(value);
            break;
        case RegistryHubStage::ImportPath:
            g_registryImportPath = TrimWide(value).empty() ? DefaultRegistryHubEnvironmentPath() : TrimWide(value);
            break;
        case RegistryHubStage::PowerShellPath:
            g_registryPowerShellPath = TrimWide(value);
            break;
        case RegistryHubStage::CmdPath:
            g_registryCmdPath = TrimWide(value).empty() ? L"cmd" : TrimWide(value);
            break;
        case RegistryHubStage::RegeditedPath:
            g_regeditedExePath = TrimWide(value);
            break;
        case RegistryHubStage::VarEdit:
            if (g_registryHubState.activeVar >= 0 && g_registryHubState.activeVar < 4) g_registryVars[g_registryHubState.activeVar] = value;
            break;
        case RegistryHubStage::PasteBufferEdit:
            if (g_registryHubState.activePasteSlot >= 0 && g_registryHubState.activePasteSlot < 4) g_pasteBuffers[g_registryHubState.activePasteSlot] = value;
            break;
        case RegistryHubStage::ZoneEdit: {
            POINT start = {}, end = {};
            if (!ParseZoneCoordinatesOrPointRefs(value, start, end)) {
                SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Expected x1,y1,x2,y2 or pointA,pointB.");
                return false;
            }
            int slot = g_registryHubState.activeZoneSlot;
            if (slot >= 0 && slot < 4) {
                g_zoneBuffers[slot].set = true;
                g_zoneBuffers[slot].start = start;
                g_zoneBuffers[slot].end = end;
            }
            break;
        }
        case RegistryHubStage::PointEdit: {
            POINT point = {};
            std::wstring trimmed = TrimWide(value);
            if (_wcsicmp(trimmed.c_str(), L"s") == 0 || _wcsicmp(trimmed.c_str(), L"save") == 0 || _wcsicmp(trimmed.c_str(), L"cursor") == 0) {
                GetCursorPos(&point);
            } else if (!ParsePointCoordinatesOrReference(value, point)) {
                SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Expected x,y.");
                return false;
            }
            int slot = g_registryHubState.activePoint;
            if (slot >= 0 && slot < REGISTRY_POINT_COUNT) {
                g_registryPointSet[slot] = true;
                g_registryPoints[slot] = point;
            }
            break;
        }
        default:
            return true;
    }
    g_registryHubState.stage = RegistryHubStage::Command;
    g_registryHubState.activeVar = -1;
    g_registryHubState.activePasteSlot = -1;
    g_registryHubState.activeZoneSlot = -1;
    g_registryHubState.activePoint = -1;
    g_registryHubState.visibleLines = 1;
    g_registryHubState.status = L"Saved.";
    ResetRegistryHubCommandMouseAnchor();
    SaveRegistryHubState();
    UpdateRegistryHubDialogText(hwnd);
    return true;
}

static bool RunRegistryHubRouterCommand(HWND hwnd, const std::wstring& raw) {
    std::string command = CompactLower(raw);
    if (command.empty()) return true;
    if (command == "esc" || command == "v") {
        CloseRegistryHubDialog(hwnd);
        return true;
    }
    if (command == "z" || command == "script") {
        g_registryHubState.stage = RegistryHubStage::Script;
        g_registryHubState.status.clear();
        UpdateRegistryHubDialogText(hwnd);
        return true;
    }
    if (command == "p" || command == "play") {
        std::wstring status;
        if (ScheduleTasketRegistryHubScript(&status)) {
            g_registryHubState.status = status.empty() ? L"Registry Hub flow scheduled through Tasket." : status;
            SaveRegistryHubState();
            CloseRegistryHubDialog(hwnd);
        } else {
            g_registryHubState.status = status.empty() ? L"Registry Hub flow failed." : status;
            SaveRegistryHubState();
            UpdateRegistryHubDialogText(hwnd);
        }
        return true;
    }
    if (command == "l" || command == "clear") {
        ClearRegistryHubState();
        g_registryHubState.status = L"Registry Hub state cleared. Paste/zone buffers in memory remain usable.";
        UpdateRegistryHubDialogText(hwnd);
        return true;
    }
    if (command == "h" || command == "?") {
        g_registryHubState.stage = RegistryHubStage::Help;
        UpdateRegistryHubDialogText(hwnd);
        return true;
    }
    if (command == "q" || command == "view" || command == "display") {
        CloseRegistryHubDialog(hwnd);
        StartViewToggles();
        return true;
    }
    if (command == "r") g_registryHubState.stage = RegistryHubStage::RegistryPath;
    else if (command == "e") {
        g_registryHubState.stage = RegistryHubStage::ExportPath;
        g_registryHubState.visibleLines = CountTextLines(g_registryExportPath);
    }
    else if (command == "i") {
        g_registryHubState.stage = RegistryHubStage::ImportPath;
        g_registryHubState.visibleLines = CountTextLines(g_registryImportPath);
    }
    else if (command == "x") {
        g_registryHubState.stage = RegistryHubStage::PowerShellPath;
        g_registryHubState.visibleLines = CountTextLines(g_registryPowerShellPath);
    }
    else if (command == "c") {
        g_registryHubState.stage = RegistryHubStage::CmdPath;
        g_registryHubState.visibleLines = CountTextLines(g_registryCmdPath);
    }
    else if (command == "g") {
        g_registryHubState.stage = RegistryHubStage::RegeditedPath;
        g_registryHubState.visibleLines = CountTextLines(g_regeditedExePath);
    }
    else if (command == "n") g_registryHubState.stage = RegistryHubStage::ZoneSelect;
    else {
        int varSlot = RegistryVarIndexFromCommand(command);
        int pointSlot = RegistryPointIndexFromCommand(command);
        if (pointSlot >= REGISTRY_ROUTER_POINT_COUNT) pointSlot = -1;
        if (varSlot >= 0) {
            g_registryHubState.stage = RegistryHubStage::VarEdit;
            g_registryHubState.activeVar = varSlot;
            g_registryHubState.visibleLines = CountTextLines(g_registryVars[varSlot]);
        } else if (pointSlot >= 0) {
            g_registryHubState.stage = RegistryHubStage::PointEdit;
            g_registryHubState.activePoint = pointSlot;
        } else {
            g_registryHubState.status = L"Unknown router key.";
        }
    }
    UpdateRegistryHubDialogText(hwnd);
    return true;
}

static bool RunRegistryHubCommand(HWND hwnd, const std::wstring& raw) {
    if (g_registryHubState.stage == RegistryHubStage::Command) return RunRegistryHubRouterCommand(hwnd, raw);
    if (g_registryHubState.stage == RegistryHubStage::Help) {
        g_registryHubState.stage = RegistryHubStage::Command;
        UpdateRegistryHubDialogText(hwnd);
        return true;
    }
    if (g_registryHubState.stage == RegistryHubStage::PasteBufferSelect) {
        int slot = PasteBufferSlotFromCommand(CompactLower(raw));
        if (slot >= 0) {
            g_registryHubState.stage = RegistryHubStage::PasteBufferEdit;
            g_registryHubState.activePasteSlot = slot;
            g_registryHubState.visibleLines = CountTextLines(g_pasteBuffers[slot]);
            UpdateRegistryHubDialogText(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Choose Z, X, C, or V.");
        SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, L"");
        FocusRegistryHubEdit(hwnd);
        return true;
    }
    if (g_registryHubState.stage == RegistryHubStage::ZoneSelect) {
        int slot = PasteBufferSlotFromCommand(CompactLower(raw));
        if (slot >= 0) {
            g_registryHubState.stage = RegistryHubStage::ZoneEdit;
            g_registryHubState.activeZoneSlot = slot;
            UpdateRegistryHubDialogText(hwnd);
            return true;
        }
        SetDlgItemTextW(hwnd, IDC_STATIC_REGISTRY_HUB_STATUS, L"Choose Z, X, C, or V.");
        SetDlgItemTextW(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE, L"");
        FocusRegistryHubEdit(hwnd);
        return true;
    }
    return SaveRegistryHubCurrentStage(hwnd);
}

static void CloseRegistryHubDialog(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, REGISTRY_HUB_WATCH_TIMER);
    if (hwnd && IsWindow(hwnd)) {
        if (g_registryHubState.stage == RegistryHubStage::Script) {
            g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
        }
        SaveRegistryHubState();
        DestroyWindow(hwnd);
    }
    g_hwndRegistryHubDlg = nullptr;
    g_hwndRegistryHubEdit = nullptr;
    g_registryHubState.stage = RegistryHubStage::Command;
}

void StartRegistryHub() {
    LoadRegistryHubState();
    if (g_hwndRegistryHubDlg && IsWindow(g_hwndRegistryHubDlg)) CloseRegistryHubDialog(g_hwndRegistryHubDlg);
    if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) CloseSaveCursorDialog(g_hwndSaveDlg);
    if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) CloseRecordKeysDialog(g_hwndRecordDlg);
    if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) ResetCirclePlacement(true);
    if (g_hwndClickDlg && IsWindow(g_hwndClickDlg)) CloseClickActionDialog(g_hwndClickDlg);
    if (g_hwndViewTogglesDlg && IsWindow(g_hwndViewTogglesDlg)) CloseViewTogglesDialog(g_hwndViewTogglesDlg);
    if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg)) ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);
    if (g_hwndRegistryHubDlg && IsWindow(g_hwndRegistryHubDlg)) CloseRegistryHubDialog(g_hwndRegistryHubDlg);

    g_registryHubState = RegistryHubState{};
    HWND owner = (g_hwndCoordPanel && IsWindow(g_hwndCoordPanel) && IsWindowVisible(g_hwndCoordPanel))
        ? g_hwndCoordPanel
        : nullptr;
    g_hwndRegistryHubDlg = CreateDialogParamW(g_hInst,
        MAKEINTRESOURCE(IDD_REGISTRY_HUB), owner,
        (DLGPROC)RegistryHubDlgProc, 0);
    if (g_hwndRegistryHubDlg) {
        ShowWindow(g_hwndRegistryHubDlg, SW_SHOW);
        SetWindowPos(g_hwndRegistryHubDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hwndRegistryHubDlg);
        UpdateRegistryHubDialogText(g_hwndRegistryHubDlg);
    }
}

LRESULT CALLBACK RegistryHubDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            g_hwndRegistryHubDlg = hwnd;
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            g_hwndRegistryHubEdit = GetDlgItem(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
            ResetRegistryHubCommandMouseAnchor();
            SetTimer(hwnd, REGISTRY_HUB_WATCH_TIMER, 250, nullptr);
            return FALSE;
        }
        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }
        case WM_TIMER:
            if (wParam == REGISTRY_HUB_WATCH_TIMER &&
                g_registryHubState.stage == RegistryHubStage::Command &&
                MouseMovedPastAnchor(g_registryHubState.cancelOnMouseMoveFrom,
                                     g_registryHubState.openedTickMs)) {
                CloseRegistryHubDialog(hwnd);
                return TRUE;
            }
            if (wParam == REGISTRY_HUB_WATCH_TIMER && g_registryHubState.stage == RegistryHubStage::Script) {
                std::wstring text = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                if (text != g_registryScript) {
                    g_registryScript = text;
                    SaveRegistryHubState();
                }
            }
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_REGISTRY_HUB_VALUE && HIWORD(wParam) == EN_CHANGE) {
                if (g_registryHubState.stage == RegistryHubStage::Command ||
                    g_registryHubState.stage == RegistryHubStage::PasteBufferSelect ||
                    g_registryHubState.stage == RegistryHubStage::ZoneSelect) {
                    std::wstring text = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                    if (text.size() == 1 && wcschr(L"zZpPqQeEiIlLhH?rRxXcCgGaAsSdDfFbBnN123456vV", text[0])) {
                        RunRegistryHubCommand(hwnd, text);
                    }
                    return TRUE;
                }
                if (g_registryHubState.stage == RegistryHubStage::PointEdit) {
                    std::wstring text = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                    if (text.size() == 1 && (text[0] == L's' || text[0] == L'S')) {
                        SaveRegistryHubCurrentStage(hwnd);
                    }
                    return TRUE;
                }
                if (g_registryHubState.stage == RegistryHubStage::Script) {
                    g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                    return TRUE;
                }
                if (RegistryHubStageUsesGrowEdit(g_registryHubState.stage)) {
                    std::wstring text = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                    int lines = CountTextLines(text);
                    bool deliberateLineBreak =
                        (GetKeyState(VK_SHIFT) & 0x8000) &&
                        (GetKeyState(VK_RETURN) & 0x8000);
                    bool shouldResize = lines < g_registryHubState.visibleLines ||
                        (lines > g_registryHubState.visibleLines && deliberateLineBreak);
                    if (shouldResize) {
                        g_registryHubState.visibleLines = lines;
                        LayoutRegistryHubDialog(hwnd);
                        FocusRegistryHubEdit(hwnd, false);
                    }
                    return TRUE;
                }
            }
            switch (LOWORD(wParam)) {
                case IDC_BTN_REGISTRY_HUB_CLEAR:
                    if (g_registryHubState.stage == RegistryHubStage::Script) {
                        g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                        SaveRegistryHubState();
                    }
                    RunRegistryHubRouterCommand(hwnd, L"l");
                    return TRUE;
                case IDC_BTN_REGISTRY_HUB_EXPORT:
                    if (g_registryHubState.stage == RegistryHubStage::Script) {
                        g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                        SaveRegistryHubState();
                    }
                    {
                        std::wstring status;
                        if (ExportRegistryHubEnvironment(&status)) {
                            g_registryHubState.status = status;
                            SaveRegistryHubState();
                            CloseRegistryHubDialog(hwnd);
                        } else {
                            g_registryHubState.status = status.empty() ? L"Registry Hub export failed." : status;
                            UpdateRegistryHubDialogText(hwnd);
                        }
                    }
                    return TRUE;
                case IDC_BTN_REGISTRY_HUB_IMPORT:
                    if (g_registryHubState.stage == RegistryHubStage::Script) {
                        g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                        SaveRegistryHubState();
                    }
                    {
                        std::wstring status;
                        if (ImportRegistryHubEnvironment(&status)) {
                            g_registryHubState.status = status;
                            SaveRegistryHubState();
                            CloseRegistryHubDialog(hwnd);
                        } else {
                            g_registryHubState.status = status.empty() ? L"Registry Hub import failed." : status;
                            UpdateRegistryHubDialogText(hwnd);
                        }
                    }
                    return TRUE;
                case IDC_BTN_REGISTRY_HUB_SAVE_SCHTS:
                    if (g_registryHubState.stage == RegistryHubStage::Script) {
                        g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                        SaveRegistryHubState();
                    }
                    {
                        std::wstring status;
                        if (SaveRegistryHubCompiledSchts(&status)) {
                            g_registryHubState.status = status;
                        } else {
                            g_registryHubState.status = status.empty() ? L"Save Schts failed." : status;
                        }
                        SaveRegistryHubState();
                        UpdateRegistryHubDialogText(hwnd);
                    }
                    return TRUE;
                case IDOK:
                    if (g_registryHubState.stage == RegistryHubStage::Script && lParam != 0) {
                        g_registryScript = DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE);
                        SaveRegistryHubState();
                        std::wstring status;
                        if (ScheduleTasketRegistryHubScript(&status)) {
                            g_registryHubState.status = status.empty() ? L"Registry Hub flow scheduled through Tasket." : status;
                            SaveRegistryHubState();
                            CloseRegistryHubDialog(hwnd);
                        } else {
                            g_registryHubState.status = status.empty() ? L"Registry Hub flow failed." : status;
                            SaveRegistryHubState();
                            UpdateRegistryHubDialogText(hwnd);
                        }
                    } else {
                        RunRegistryHubCommand(hwnd, DialogText(hwnd, IDC_EDIT_REGISTRY_HUB_VALUE));
                    }
                    return TRUE;
                case IDCANCEL:
                    CloseRegistryHubDialog(hwnd);
                    return TRUE;
            }
            break;
        case WM_CLOSE:
            CloseRegistryHubDialog(hwnd);
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: SAVE CURSOR (modeless)
// =============================================================================

LRESULT CALLBACK SaveCursorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            g_saveCursorState = SaveCursorState{};
            g_saveCursorState.stage = SaveCursorStage::Name;
            g_saveCursorState.coords.push_back({0, 1000, g_savedCursorPos.x, g_savedCursorPos.y});
            GetCursorPos(&g_saveCursorState.cancelOnMouseMoveFrom);
            g_saveCursorState.openedTickMs = GetTickCount();
            std::wstringstream ss;
            ss << L"X: " << g_savedCursorPos.x << L"   Y: " << g_savedCursorPos.y;
            SetDlgItemTextW(hwnd, IDC_STATIC_COORDS, ss.str().c_str());
            SetDlgItemTextW(hwnd, IDC_EDIT_NAME, L"");
            UpdateSaveCursorDialogText(hwnd);
            SetTimer(hwnd, SAVE_CURSOR_WATCH_TIMER, 100, nullptr);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_TIMER:
            if (wParam == SAVE_CURSOR_WATCH_TIMER &&
                g_saveCursorState.stage == SaveCursorStage::Name &&
                MouseMovedPastAnchor(g_saveCursorState.cancelOnMouseMoveFrom, g_saveCursorState.openedTickMs)) {
                KillTimer(hwnd, SAVE_CURSOR_WATCH_TIMER);
                CloseSaveCursorDialog(hwnd);
                return TRUE;
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_NAME &&
                HIWORD(wParam) == EN_CHANGE &&
                g_saveCursorState.stage == SaveCursorStage::Command) {
                std::wstring text = DialogText(hwnd, IDC_EDIT_NAME);
                if (text.size() == 1 && wcschr(L"zZxXcCvV", text[0])) {
                    RunSaveCursorCommand(hwnd, text);
                }
                return TRUE;
            }

            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_ANOTHER_CURSOR: {
                    RunSaveCursorCommand(hwnd, L"x");
                    return TRUE;
                }

                case IDOK: {
                    if (g_saveCursorState.stage == SaveCursorStage::Name) {
                        AdvanceSaveCursorToCommand(hwnd);
                    } else {
                        std::wstring command = DialogText(hwnd, IDC_EDIT_NAME);
                        if (!RunSaveCursorCommand(hwnd, command)) {
                            SetDlgItemTextW(hwnd, IDC_EDIT_NAME, L"");
                            SetDlgItemTextW(hwnd, IDC_STATIC_SAVE_HINT,
                                L"Use Z save, X add point, C save + click, or V cancel.");
                        }
                    }
                    return TRUE;
                }

                case IDC_BTN_ADD_CLICK_CURSOR: {
                    RunSaveCursorCommand(hwnd, L"c");
                    return TRUE;
                }

                case IDCANCEL:
                    RunSaveCursorCommand(hwnd, L"v");
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            KillTimer(hwnd, SAVE_CURSOR_WATCH_TIMER);
            CloseSaveCursorDialog(hwnd);
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
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            g_recordingKeys = true;
            g_waitingForKey = false;
            g_currentStepKeys.clear();
            g_recordExtraActions.clear();
            g_recordKeysState = RecordKeysState{};
            g_recordKeysState.stage = RecordKeysStage::Name;
            GetCursorPos(&g_recordKeysState.cancelOnMouseMoveFrom);
            g_recordKeysState.openedTickMs = GetTickCount();
            SetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, L"");
            SetDlgItemTextW(hwnd, IDC_EDIT_DELAY, L"200");
            SetDlgItemTextW(hwnd, IDC_EDIT_PASTE_TEXT, L"");
            HWND hList = GetDlgItem(hwnd, IDC_LIST_KEYS);
            if (hList) SendMessage(hList, LB_RESETCONTENT, 0, 0);
            UpdateRecordKeysDialogText(hwnd);
            SetTimer(hwnd, RECORD_KEYS_WATCH_TIMER, 100, nullptr);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_TIMER:
            if (wParam == RECORD_KEYS_WATCH_TIMER &&
                g_recordKeysState.stage == RecordKeysStage::Name &&
                MouseMovedPastAnchor(g_recordKeysState.cancelOnMouseMoveFrom, g_recordKeysState.openedTickMs)) {
                KillTimer(hwnd, RECORD_KEYS_WATCH_TIMER);
                CloseRecordKeysDialog(hwnd);
                return TRUE;
            }
            break;

        case WM_USER + 100: {
            if (!g_currentStepKeys.empty()) {
                bool isNew = true;
                if (!g_recordedSteps.empty()) {
                    if (g_recordedSteps.back() == g_currentStepKeys) isNew = false;
                }
                if (isNew) {
                    g_recordedSteps.push_back(g_currentStepKeys);
                    AddRecordStepToList(hwnd, g_currentStepKeys);
                }
                std::wstringstream ss;
                ss << L"Captured " << g_currentStepKeys.size() << L" key(s)."
                   << L" Z captures next, X opens manual, C queues paste, Enter writes.";
                SetDlgItemTextW(hwnd, IDC_STATIC_STATUS, ss.str().c_str());
                g_currentStepKeys.clear();
                g_recordKeysState.stage = RecordKeysStage::Command;
            }
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_SEQ_NAME &&
                HIWORD(wParam) == EN_CHANGE &&
                g_recordKeysState.stage == RecordKeysStage::Command) {
                std::wstring text = DialogText(hwnd, IDC_EDIT_SEQ_NAME);
                if (text.size() == 1 && wcschr(L"zZxXcCvV", text[0])) {
                    RunRecordKeysCommand(hwnd, text);
                }
                return TRUE;
            }

            switch (LOWORD(wParam)) {
                case IDC_BTN_CAPTURE: {
                    if (g_recordKeysState.stage == RecordKeysStage::Name) {
                        if (!AdvanceRecordKeysToCommand(hwnd)) return TRUE;
                    }
                    RunRecordKeysCommand(hwnd, L"z");
                    return TRUE;
                }

                case IDC_BTN_ADD_STEP: {
                    FlushCurrentRecordStep(hwnd);
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
                    if (g_recordKeysState.stage == RecordKeysStage::Name) {
                        if (!AdvanceRecordKeysToCommand(hwnd)) return TRUE;
                    }
                    RunRecordKeysCommand(hwnd, L"x");
                    return TRUE;
                }

                case IDC_BTN_ADD_PASTE: {
                    if (g_recordKeysState.stage == RecordKeysStage::Name) {
                        if (!AdvanceRecordKeysToCommand(hwnd)) return TRUE;
                    }
                    if (g_recordKeysState.stage == RecordKeysStage::Paste) {
                        QueueRecordPasteBlock(hwnd);
                    } else {
                        RunRecordKeysCommand(hwnd, L"c");
                    }
                    return TRUE;
                }

                case IDOK: {
                    if (g_recordKeysState.stage == RecordKeysStage::Name) {
                        AdvanceRecordKeysToCommand(hwnd);
                    } else if (g_recordKeysState.stage == RecordKeysStage::Paste) {
                        QueueRecordPasteBlock(hwnd);
                    } else {
                        std::wstring command = DialogText(hwnd, IDC_EDIT_SEQ_NAME);
                        if (!RunRecordKeysCommand(hwnd, command)) {
                            SetDlgItemTextW(hwnd, IDC_EDIT_SEQ_NAME, L"");
                            UpdateRecordKeysDialogText(hwnd);
                        }
                    }
                    return TRUE;
                }

                case IDCANCEL:
                    RunRecordKeysCommand(hwnd, L"v");
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            KillTimer(hwnd, RECORD_KEYS_WATCH_TIMER);
            CloseRecordKeysDialog(hwnd);
            return TRUE;
    }
    return FALSE;
}

// =============================================================================
// DIALOG: MANUAL KEYS (modal child)
// =============================================================================

struct ManualKeyCategory {
    const wchar_t* label;
    const wchar_t* const* keys;
};

static const wchar_t* const kManualModifiers[] = {
    L"CONTROL_LEFT", L"CONTROL_RIGHT", L"SHIFT_LEFT", L"SHIFT_RIGHT",
    L"ALT_LEFT", L"ALT_GR", L"WINDOWS", L"FN", nullptr
};

static const wchar_t* const kManualMouseButtons[] = {
    L"LEFT_MOUSE", L"RIGHT_MOUSE", L"MIDDLE_MOUSE", L"XBUTTON_1", L"XBUTTON_2", nullptr
};

static const wchar_t* const kManualCommons[] = {
    L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J",
    L"K", L"L", L"M", L"N", L"O", L"P", L"Q", L"R", L"S", L"T", L"U",
    L"V", L"W", L"X", L"Y", L"Z",
    L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9",
    L"SPACEBAR", L"ENTER", L"TAB", L"BACKSPACE", L"ESCAPE", nullptr
};

static const wchar_t* const kManualNumPad[] = {
    L"NUMPAD_0", L"NUMPAD_1", L"NUMPAD_2", L"NUMPAD_3", L"NUMPAD_4",
    L"NUMPAD_5", L"NUMPAD_6", L"NUMPAD_7", L"NUMPAD_8", L"NUMPAD_9",
    L"+", L"-", L"*", L"/", L".", L",", L"NUMPAD_ENTER", L"NUM_LOCK", nullptr
};

static const wchar_t* const kManualSpecials[] = {
    L"PRINT_SCREEN", L"PRINT", L"INSERT", L"DELETE", nullptr
};

static const wchar_t* const kManualNavigations[] = {
    L"UP_ARROW", L"DOWN_ARROW", L"LEFT_ARROW", L"RIGHT_ARROW",
    L"HOME", L"END", L"PAGE_UP", L"PAGE_DOWN", nullptr
};

static const wchar_t* const kManualFunctions[] = {
    L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8",
    L"F9", L"F10", L"F11", L"F12", nullptr
};

static const wchar_t* const kManualToggles[] = {
    L"CAPS_LOCK", L"NUM_LOCK", L"SCROLL_LOCK", nullptr
};

static const wchar_t* const kManualMiscChars[] = {
    L"+", L"-", L"*", L"/", L".", L",", nullptr
};

static const ManualKeyCategory kManualCategories[] = {
    {L"Commons", kManualCommons},
    {L"Modifiers", kManualModifiers},
    {L"Mouse buttons", kManualMouseButtons},
    {L"NumPad", kManualNumPad},
    {L"Specials", kManualSpecials},
    {L"Navigations", kManualNavigations},
    {L"Functions", kManualFunctions},
    {L"Toggles", kManualToggles},
    {L"Misc. chars. (qwerty keyboard ref.)", kManualMiscChars},
    {nullptr, nullptr}
};

static void PopulateManualKeyCombos(HWND hwnd, int categoryIndex) {
    if (categoryIndex < 0 || !kManualCategories[categoryIndex].label) categoryIndex = 0;
    HWND combos[] = {
        GetDlgItem(hwnd, IDC_COMBO_KEY1),
        GetDlgItem(hwnd, IDC_COMBO_KEY2),
        GetDlgItem(hwnd, IDC_COMBO_KEY3)
    };
    for (HWND combo : combos) {
        if (!combo) continue;
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"");
        for (int i = 0; kManualCategories[categoryIndex].keys[i]; i++) {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kManualCategories[categoryIndex].keys[i]);
        }
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

LRESULT CALLBACK ManualKeysDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::vector<std::string> s_curKeys;
    static int s_stepNum;
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            ApplyDialogUiFont(hwnd);
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
            SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);
            s_curKeys.clear();
            s_stepNum = 1;

            HWND category = GetDlgItem(hwnd, IDC_COMBO_KEY_CATEGORY);
            if (category) {
                for (int i = 0; kManualCategories[i].label; i++) {
                    SendMessageW(category, CB_ADDSTRING, 0, (LPARAM)kManualCategories[i].label);
                }
                SendMessageW(category, CB_SETCURSEL, 0, 0);
            }
            PopulateManualKeyCombos(hwnd, 0);

            std::wstringstream ss;
            ss << L"Step " << s_stepNum;
            SetDlgItemTextW(hwnd, IDC_STATIC_STEP_NUM, ss.str().c_str());
            ScaleResourceDialogFromCurrentLayout(hwnd);
            return FALSE;
        }

        case WM_CTLCOLORDLG:
            return (INT_PTR)g_hbrHudBg;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 238, 245));
            return (INT_PTR)g_hbrHudBg;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(10, 12, 16));
            SetTextColor(hdc, RGB(245, 250, 255));
            return (INT_PTR)g_hbrHudEdit;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_COMBO_KEY_CATEGORY && HIWORD(wParam) == CBN_SELCHANGE) {
                int index = (int)SendDlgItemMessageW(hwnd, IDC_COMBO_KEY_CATEGORY, CB_GETCURSEL, 0, 0);
                PopulateManualKeyCombos(hwnd, index);
                return TRUE;
            }

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

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_APPWINDOW | WS_EX_LAYERED;
    DWORD style = WS_POPUP;
    RECT wr = {0, 0, CoordPanelW(), CoordPanelH()};
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int windowW = wr.right - wr.left;
    int windowH = wr.bottom - wr.top;

    int x = g_screenX + g_screenW - windowW - UiPx(20);
    int y = g_screenY + UiPx(20);

    g_hwndCoordPanel = CreateWindowExW(
        exStyle,
        L"CursorOverlayCoordPanel", L"Macrohelp Control Panel",
        style,
        x, y, windowW, windowH,
        nullptr, nullptr, g_hInst, nullptr);

    if (g_hwndCoordPanel) {
        SetLayeredWindowAttributes(g_hwndCoordPanel, COLORKEY, 135, LWA_COLORKEY | LWA_ALPHA);
        RegisterKeyboardRawInput(g_hwndCoordPanel);
    }
}

void RegisterKeyboardRawInput(HWND hwnd) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;   // Generic desktop controls
    rid.usUsage = 0x06;       // Keyboard
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
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
        g_screenX, g_screenY, KeyDisplayW(), KeyDisplayH(),
        nullptr, nullptr, g_hInst, nullptr);

    if (!g_hwndKeyDisplay) return;

    // Create 32bpp DIB section for alpha rendering
    g_keyDibW = KeyDisplayW();
    g_keyDibH = KeyDisplayH();

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

void EnablePhysicalPixelDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetProcessDpiAwarenessContextFn = BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT);
        auto setProcessDpiAwarenessContext =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setProcessDpiAwarenessContext &&
            setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }

        using SetProcessDPIAwareFn = BOOL (WINAPI *)();
        auto setProcessDPIAware =
            reinterpret_cast<SetProcessDPIAwareFn>(
                GetProcAddress(user32, "SetProcessDPIAware"));
        if (setProcessDPIAware) {
            setProcessDPIAware();
        }
    }
}

void RefreshScreenMetrics() {
    g_screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

int DetectDisplayRefreshHz() {
    POINT pt = {};
    GetCursorPos(&pt);
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
            dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 1000) {
            return (int)dm.dmDisplayFrequency;
        }
    }

    HDC hdc = GetDC(nullptr);
    int hz = hdc ? GetDeviceCaps(hdc, VREFRESH) : 0;
    if (hdc) ReleaseDC(nullptr, hdc);
    if (hz >= 30 && hz <= 1000) return hz;
    return 60;
}

int DetectDisplayUpdateIntervalMs() {
    g_displayRefreshHz = DetectDisplayRefreshHz();
    wchar_t overrideValue[32] = {};
    DWORD len = GetEnvironmentVariableW(L"MACROHELP_UPDATE_MS", overrideValue, (DWORD)(sizeof(overrideValue) / sizeof(overrideValue[0])));
    if (len > 0 && len < (DWORD)(sizeof(overrideValue) / sizeof(overrideValue[0]))) {
        wchar_t* end = nullptr;
        long parsed = wcstol(overrideValue, &end, 10);
        if (end != overrideValue && parsed >= 1 && parsed <= 100) {
            return (int)parsed;
        }
    }

    double interval = 1000.0 / (double)std::max(1, g_displayRefreshHz);
    return std::max(MIN_CROSSHAIR_UPDATE_MS, std::min(DEFAULT_UPDATE_MS, (int)std::round(interval)));
}

bool InitApp() {
    // Screen dimensions and refresh cadence must be captured after DPI
    // awareness is enabled, otherwise Windows returns scaled coordinates.
    RefreshScreenMetrics();
    g_updateMs = DetectDisplayUpdateIntervalMs();
    g_uiScale = DetectUiScale();

    // Fonts
    g_hFontCoord = CreateFontW(UiFontPx(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontBold = CreateFontW(UiFontPx(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontMono = CreateFontW(UiFontPx(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    g_hbrColorkey = CreateSolidBrush(COLORKEY);
    g_hbrHudBg = CreateSolidBrush(RGB(18, 22, 27));
    g_hbrHudEdit = CreateSolidBrush(RGB(10, 12, 16));

    // GDI+
    GdiplusStartup(&g_gdiToken, &g_gdiInput, &g_gdiOutput);

    // PromptFont
    LoadPromptFont();

    return g_hFontCoord && g_hFontBold && g_hFontMono && g_hbrColorkey && g_hbrHudBg && g_hbrHudEdit;
}

void ShutdownApp() {
    // Cleanup dialogs
    if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg)) DestroyWindow(g_hwndSaveDlg);
    if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg)) DestroyWindow(g_hwndRecordDlg);
    if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg)) DestroyWindow(g_hwndCircleDlg);
    if (g_hwndClickDlg && IsWindow(g_hwndClickDlg)) CloseClickActionDialog(g_hwndClickDlg);
    if (g_hwndViewTogglesDlg && IsWindow(g_hwndViewTogglesDlg)) CloseViewTogglesDialog(g_hwndViewTogglesDlg);
    if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg)) ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);

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
    if (g_hFontMono) DeleteObject(g_hFontMono);
    if (g_hbrColorkey) DeleteObject(g_hbrColorkey);
    if (g_hbrHudBg) DeleteObject(g_hbrHudBg);
    if (g_hbrHudEdit) DeleteObject(g_hbrHudEdit);
    if (g_hPromptFont) DeleteObject(g_hPromptFont);
    if (g_hPromptFontSmall) DeleteObject(g_hPromptFontSmall);

    // Remove PromptFont from memory
    if (g_hFontResource) RemoveFontMemResourceEx(g_hFontResource);
    delete g_promptFontCollection;
    g_promptFontCollection = nullptr;

    // Shutdown GDI+
    if (g_gdiToken) GdiplusShutdown(g_gdiToken);
}

static bool HasDialogFocus(HWND dialog) {
    if (!dialog || !IsWindow(dialog)) return false;
    HWND focus = GetFocus();
    return focus == dialog || (focus && IsChild(dialog, focus));
}

static bool HandleKeyboardOnlyWriterDialog(MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    if (msg.wParam != VK_RETURN && msg.wParam != VK_ESCAPE) return false;

    if (g_hwndSaveDlg && HasDialogFocus(g_hwndSaveDlg)) {
        WPARAM command = (msg.wParam == VK_RETURN) ? IDOK : IDCANCEL;
        SendMessageW(g_hwndSaveDlg, WM_COMMAND, command, 0);
        return true;
    }

    if (g_hwndRecordDlg && HasDialogFocus(g_hwndRecordDlg)) {
        if (msg.wParam == VK_RETURN &&
            g_recordKeysState.stage == RecordKeysStage::Paste &&
            (GetKeyState(VK_SHIFT) & 0x8000)) {
            return false;
        }

        WPARAM command = (msg.wParam == VK_RETURN) ? IDOK : IDCANCEL;
        SendMessageW(g_hwndRecordDlg, WM_COMMAND, command, 0);
        return true;
    }

    if (g_hwndClickDlg && HasDialogFocus(g_hwndClickDlg)) {
        WPARAM command = (msg.wParam == VK_RETURN) ? IDOK : IDCANCEL;
        SendMessageW(g_hwndClickDlg, WM_COMMAND, command, 0);
        return true;
    }

    if (g_hwndViewTogglesDlg && HasDialogFocus(g_hwndViewTogglesDlg)) {
        WPARAM command = (msg.wParam == VK_RETURN) ? IDOK : IDCANCEL;
        SendMessageW(g_hwndViewTogglesDlg, WM_COMMAND, command, 0);
        return true;
    }

    if (g_hwndPasteBuffersDlg && HasDialogFocus(g_hwndPasteBuffersDlg)) {
        if (msg.wParam == VK_RETURN &&
            g_pasteBufferState.stage == PasteBufferStage::Edit &&
            (GetKeyState(VK_SHIFT) & 0x8000)) {
            return false;
        }

        WPARAM command = (msg.wParam == VK_RETURN) ? IDOK : IDCANCEL;
        SendMessageW(g_hwndPasteBuffersDlg, WM_COMMAND, command, 0);
        return true;
    }

    if (g_hwndRegistryHubDlg && HasDialogFocus(g_hwndRegistryHubDlg)) {
        if (msg.wParam == VK_RETURN &&
            (g_registryHubState.stage == RegistryHubStage::Script ||
             g_registryHubState.stage == RegistryHubStage::VarEdit ||
             g_registryHubState.stage == RegistryHubStage::PasteBufferEdit) &&
            (GetKeyState(VK_SHIFT) & 0x8000)) {
            return false;
        }

        WPARAM command = (msg.wParam == VK_RETURN) ? IDOK : IDCANCEL;
        SendMessageW(g_hwndRegistryHubDlg, WM_COMMAND, command, 0);
        return true;
    }

    return false;
}

// =============================================================================
// ENTRY POINT
// =============================================================================

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;
    EnablePhysicalPixelDpiAwareness();

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
    RegisterHotKey(g_hwndCrosshair, IDH_SAVE_CURSOR, MOD_SHIFT | MOD_ALT, '0');
    RegisterHotKey(g_hwndCrosshair, IDH_REGISTRY_HUB, MOD_SHIFT | MOD_ALT, '2');
    RegisterHotKey(g_hwndCrosshair, IDH_CIRCLE_PLACER, MOD_SHIFT | MOD_ALT, '3');
    RegisterHotKey(g_hwndCrosshair, IDH_CLICK_LEFT, MOD_SHIFT | MOD_ALT, '4');
    RegisterHotKey(g_hwndCrosshair, IDH_CLICK_RIGHT, MOD_SHIFT | MOD_ALT, '5');
    RegisterHotKey(g_hwndCrosshair, IDH_CLICK_MIDDLE, MOD_SHIFT | MOD_ALT, '6');
    RegisterHotKey(g_hwndCrosshair, IDH_STOP_ALL_TASKET, MOD_SHIFT | MOD_ALT, '7');
    RegisterHotKey(g_hwndCrosshair, IDH_VIEW_TOGGLES, MOD_SHIFT | MOD_ALT, '8');
    RegisterHotKey(g_hwndCrosshair, IDH_RECORD_KEYS, MOD_SHIFT | MOD_ALT, '9');
    RegisterHotKey(g_hwndCrosshair, IDH_PASTE_BUFFERS, MOD_SHIFT | MOD_ALT, '1');

    // Keyboard hook (always active for key display)
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    // Show windows
    ShowWindow(g_hwndCrosshair, SW_SHOW);
    ShowWindow(g_hwndCoordPanel, nCmdShow);
    if (g_hwndKeyDisplay) {
        ShowWindow(g_hwndKeyDisplay, SW_SHOW);
        SeedKeyHudStartupPulse();
        RenderKeyDisplay();
    }
    UpdateWindow(g_hwndCrosshair);
    UpdateWindow(g_hwndCoordPanel);

    // Timers
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_timerResolutionActive = true;
    }
    SetTimer(g_hwndCrosshair, IDT_UPDATE_CURSOR, g_updateMs, nullptr);
    if (g_hwndKeyDisplay) {
        SetTimer(g_hwndKeyDisplay, IDT_UPDATE_KEYS, g_updateMs, nullptr);
    }

    // Tray icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndCoordPanel;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_CURSOROVERLAY));
    wcscpy_s(nid.szTip, L"CursorOverlay - Shift+Alt+1/2/3/4/5/6/7/8/9/0");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Balloon notification
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"CursorOverlay");
    wcscpy_s(nid.szInfo, L"Running! 1=Paste 2=Hub 3=Circle 4/5/6=Clicks 7=Stop 8=Panel 9=Record 0=Save");
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    // Main message loop
    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0)) {
        bool dlgMsg = false;
        if (HandleKeyboardOnlyWriterDialog(msg)) {
            dlgMsg = true;
        }
        else if (g_hwndSaveDlg && IsWindow(g_hwndSaveDlg) && IsDialogMessage(g_hwndSaveDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndRecordDlg && IsWindow(g_hwndRecordDlg) && IsDialogMessage(g_hwndRecordDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndCircleDlg && IsWindow(g_hwndCircleDlg) && IsDialogMessage(g_hwndCircleDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndClickDlg && IsWindow(g_hwndClickDlg) && IsDialogMessage(g_hwndClickDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndViewTogglesDlg && IsWindow(g_hwndViewTogglesDlg) && IsDialogMessage(g_hwndViewTogglesDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndPasteBuffersDlg && IsWindow(g_hwndPasteBuffersDlg) && IsDialogMessage(g_hwndPasteBuffersDlg, &msg)) {
            dlgMsg = true;
        }
        else if (g_hwndRegistryHubDlg && IsWindow(g_hwndRegistryHubDlg) && IsDialogMessage(g_hwndRegistryHubDlg, &msg)) {
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
    UnregisterHotKey(g_hwndCrosshair, IDH_CIRCLE_PLACER);
    UnregisterHotKey(g_hwndCrosshair, IDH_CLICK_LEFT);
    UnregisterHotKey(g_hwndCrosshair, IDH_CLICK_RIGHT);
    UnregisterHotKey(g_hwndCrosshair, IDH_CLICK_MIDDLE);
    UnregisterHotKey(g_hwndCrosshair, IDH_STOP_ALL_TASKET);
    UnregisterHotKey(g_hwndCrosshair, IDH_VIEW_TOGGLES);
    UnregisterHotKey(g_hwndCrosshair, IDH_REGISTRY_HUB);
    UnregisterHotKey(g_hwndCrosshair, IDH_PASTE_BUFFERS);
    if (g_hKeyboardHook) { UnhookWindowsHookEx(g_hKeyboardHook); g_hKeyboardHook = nullptr; }
    KillTimer(g_hwndCrosshair, IDT_UPDATE_CURSOR);
    if (g_hwndKeyDisplay) KillTimer(g_hwndKeyDisplay, IDT_UPDATE_KEYS);
    if (g_timerResolutionActive) {
        timeEndPeriod(1);
        g_timerResolutionActive = false;
    }
    ShutdownApp();
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return (int)msg.wParam;
}
