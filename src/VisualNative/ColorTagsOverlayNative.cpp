#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <uiautomation.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <servprov.h>
#include <dwmapi.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// The single visual contract: tag ids, colors, user labels, display mode.
#include "../Shared/ColorTagsConfig.h"

namespace {

constexpr wchar_t kManagerClass[] = L"ColorTags.NativeOverlay.Manager";
constexpr wchar_t kOverlayClass[] = L"ColorTags.NativeOverlay.Window";
constexpr wchar_t kTrayClass[] = L"ColorTags.NativeOverlay.Tray";
constexpr UINT kScanReady = WM_APP + 1;
constexpr UINT kLocationChanged = WM_APP + 2;
constexpr UINT kHideForContentChange = WM_APP + 5;
constexpr UINT kForegroundChanged = WM_APP + 6;
constexpr UINT kTrayMessage = WM_APP + 7;
constexpr UINT kTrayIconId = 1;

// Tray menu commands.
constexpr UINT kCommandExit = 250;
constexpr UINT kCommandSettings = 260;
constexpr wchar_t kSettingsClass[] = L"ColorTags.NativeOverlay.Settings";
// Control ids inside the settings window.
constexpr UINT kSettingsEditBase = 1000;   // 1000..1006, one per tag
constexpr UINT kSettingsModeDot = 1100;    // the display toggle
constexpr UINT kSettingsScrollHide = 1102;  // the scrolling toggle
constexpr UINT kSettingsSave = 1200;
constexpr UINT kSettingsReset = 1201;
constexpr UINT kSettingsClose = 1202;
constexpr UINT kSettingsApply = 1203;
constexpr UINT kSettingsCheckUpdates = 1204;
constexpr UINT_PTR kSettingsHoverTimer = 1;
constexpr UINT kSettingsReleases = 1205;
// Posted to the settings window when the update check finishes.
constexpr UINT kUpdateReady = WM_APP + 8;

// The version is stamped in by the build script from the VERSION file, so the
// binary, the installer and the release tag cannot disagree. Stringified rather
// than quoted because quotes do not survive the trip through PowerShell.
#define COLORTAGS_STRINGIFY2(value) #value
#define COLORTAGS_STRINGIFY(value) COLORTAGS_STRINGIFY2(value)
#define COLORTAGS_WIDEN2(value) L##value
#define COLORTAGS_WIDEN(value) COLORTAGS_WIDEN2(value)
#ifndef COLORTAGS_VERSION
#define COLORTAGS_VERSION 0.0.0
#endif
constexpr const wchar_t* kVersionText =
    COLORTAGS_WIDEN(COLORTAGS_STRINGIFY(COLORTAGS_VERSION));
constexpr wchar_t kReleasesUrl[] =
    L"https://github.com/vldpotapov/ColorTags/releases";
constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] =
    L"/repos/vldpotapov/ColorTags/releases/latest";
constexpr UINT_PTR kExitTimer = 1;
constexpr UINT_PTR kStopPollTimer = 2;
constexpr UINT_PTR kWheelSettleTimer = 3;
constexpr wchar_t kStopEventName[] = L"Local\\ColorTags.NativeOverlay.Stop";

struct ComReleaser {
    template <typename T> void operator()(T* value) const {
        if (value) value->Release();
    }
};

template <typename T>
using ComPtr = std::unique_ptr<T, ComReleaser>;

// One drawn indicator for one row. In dot mode only `circle` is painted. In
// label mode the badge is laid out inside `cell` at render time, once the UI
// thread has measured the text with the actual font. `erase` is the bounding
// box the overlay window is sized from and is a superset of what is painted.
struct Indicator {
    RECT erase{};
    RECT circle{};
    RECT cell{};
    COLORREF color{};
    std::wstring label;  // empty in dot mode
};

struct WindowSnapshot {
    HWND explorer{};
    RECT explorerRect{};
    RECT overlayRect{};
    std::wstring folder;
    std::vector<Indicator> dots;
    double rowHeight{};
    LONG contentTopOffset{};
    LONG contentBottomOffset{};
    double verticalScrollPercent{-1.0};
    double verticalViewSize{};
};

struct ScanResult {
    std::vector<WindowSnapshot> windows;
    // Windows that were scanned successfully and hold no tagged row. A window
    // whose scan failed appears in neither list, so its overlay is left alone
    // instead of being destroyed and rebuilt on the next pass.
    std::vector<HWND> emptyWindows;
    bool fullRefresh{};
    uint64_t contentGeneration{};
};

struct CachedTaggedRow {
    ComPtr<IUIAutomationElement> element;
    std::wstring tag;
    COLORREF color{};
    RECT lastRect{};
};

struct WorkerWindowCache {
    std::wstring folder;
    std::wstring title;
    // The shell view window of the tab this cache describes. UI Automation
    // hands back the rows of every tab in the window, including the ones not on
    // screen, so the row search has to be confined to this.
    HWND viewWindow{};
    std::map<std::wstring, std::wstring> pathsByDisplayName;
    std::vector<std::wstring> orderedDisplayNames;
    std::map<std::wstring, LONG> viewYByDisplayName;
    double centerXOffset{};
    LONG columnLeftOffset{};
    LONG columnRightOffset{};
    LONG contentTopOffset{};
    LONG contentBottomOffset{};
    bool geometryValid{};
    std::vector<CachedTaggedRow> taggedRows;
    double rowHeight{};
    ComPtr<IUIAutomationElement> scrollElement;
    double verticalScrollPercent{-1.0};
    double verticalViewSize{};
};

struct OverlayWindow {
    HWND hwnd{};
    HWND explorer{};
    POINT offset{};
    SIZE size{};
    HDC memoryDc{};
    HBITMAP bitmap{};
    HGDIOBJ previousBitmap{};
    void* pixels{};
    ULONGLONG lastUpdate{};
    HFONT font{};
    int fontHeight{};
    bool visible{};
    double rowHeight{};
    LONG contentTopOffset{};
    LONG contentBottomOffset{};
    // The last clip applied, kept for the status file: an empty clip hides
    // everything while every other signal still reports a healthy layer.
    LONG clipTop{};
    LONG clipBottom{};
    bool clipped{};
    double verticalScrollPercent{-1.0};
    double verticalViewSize{};
    std::vector<Indicator> dots;
};

struct Options {
    std::wstring targetFolder;
    std::wstring statusPath;
    std::wstring columnName = L"Tags";
    int dotSize = 12;
    // -1 = follow HKCU\\Software\\ColorTags\\DisplayMode, 0 = dot, 1 = label.
    // The override exists so a bounded test can pin one mode regardless of the
    // user's current setting.
    int modeOverride = -1;
    // Affordable now that a full pass is one cached request rather than three
    // calls per row.
    int refreshMilliseconds = 300;
    int durationSeconds = 0;
    bool diagnosticFastStatus = false;
};

HWND g_manager = nullptr;
HWND g_tray = nullptr;
HWND g_settings = nullptr;
HICON g_trayIcon = nullptr;
bool g_trayAdded = false;
HANDLE g_stopEvent = nullptr;
HANDLE g_contentEvent = nullptr;
HANDLE g_scanConsumedEvent = nullptr;
HANDLE g_worker = nullptr;
HWINEVENTHOOK g_locationHook = nullptr;
HWINEVENTHOOK g_scrollHook = nullptr;
HWINEVENTHOOK g_valueHook = nullptr;
HWINEVENTHOOK g_foregroundHook = nullptr;
HWINEVENTHOOK g_selectionHook = nullptr;
HWINEVENTHOOK g_nameHook = nullptr;
HHOOK g_mouseHook = nullptr;
HHOOK g_keyboardHook = nullptr;
std::atomic_bool g_locationMessagePending = false;
std::atomic_bool g_scanInFlight = false;
std::atomic_uint64_t g_contentEventCount = 0;
std::atomic_uint64_t g_contentGeneration = 0;
std::atomic_uint64_t g_fastScanCount = 0;
std::atomic_uint64_t g_wheelEventCount = 0;
// Diagnostics for the status file. A window with no tagged rows produces no
// snapshot at all, which makes "nothing drawn" and "nothing found" look the
// same from outside. These counters separate the two.
std::atomic_int g_diagExplorerWindows = 0;
// One line per Explorer window seen by the last full scan: whether it produced
// a snapshot, and what it was carrying when it did. A window that vanishes from
// this list, or that turns up with rows=0, is the one to look at.
std::mutex g_diagWindowsMutex;
std::wstring g_diagWindowsDetail;
std::atomic_int g_diagColumnFound = 0;
std::atomic_int g_diagRowsSeen = 0;
std::atomic_int g_diagTaggedRows = 0;
std::atomic_int g_diagFolderMismatch = 0;
std::atomic_int g_diagLabelMode = 0;
// How the layer behaves while the content moves. Hiding it and restoring it
// once was the only affordable option when a pass cost three calls per row;
// now that a fast pass costs one call per tagged row, it can follow the rows
// instead.
//
// Both were tried side by side and neither reads well: following the rows
// leaves the indicators trailing a fraction of a second behind, hiding them
// takes them off the screen for the length of the scroll. Hide is the
// default - a trailing indicator is read as wrong, a missing one only as
// absent. This is the ceiling of drawing from outside the process, not a
// setting that wants tuning.
//
// ScrollBehavior under the ColorTags key: hide or follow.
std::atomic_bool g_followScroll = false;
// Which tabs GetShellWindowData saw, and what told them apart. Written by the
// scan thread and read by the UI thread when it writes the status file, so a
// torn read costs a garbled diagnostic and nothing more.
std::wstring g_diagTabs;
// Every TabItem the window exposes, with where it sits and whether it is
// selected. A window holds more than one tab strip.
std::wstring g_diagTabStrip;
std::atomic_uint64_t g_scrollTrackingUntil = 0;
std::atomic_uint64_t g_wheelTrackingUntil = 0;
// Set when something happened that changes which folder is on screen: a tab
// switch or a navigation. Both used to be noticed only by the periodic shell
// refresh, which runs at most every 1.5 seconds - the whole of the delay before
// indicators caught up with a new folder.
std::atomic_bool g_forceShellRefresh = false;
std::map<HWND, OverlayWindow> g_overlays;
Options g_options;

// Display mode and labels, reloaded once per scan pass. Only the scan thread
// touches this: the worker resolves every label into the snapshot, so the UI
// thread never reads the registry while painting.
struct RuntimeConfig {
    colortags::DisplayMode mode = colortags::DisplayMode::Dot;
    std::wstring labels[colortags::kTagCount];
};

RuntimeConfig g_workerConfig;

// A badge narrower than this cannot hold even a few characters, so the dot is
// kept instead of drawing an unreadable sliver.
// How long an overlay may keep its indicators without a fresh snapshot. Short
// enough that a layer never stays frozen over rows it no longer describes, long
// enough to ride out a single failed pass without blinking.
constexpr ULONGLONG kStaleOverlayMilliseconds = 1500;
constexpr LONG kMinimumLabelWidth = 40;
constexpr LONG kLabelColumnInset = 2;
constexpr int kLabelTextPadding = 6;

void RefreshWorkerConfig() {
    g_workerConfig.mode = g_options.modeOverride < 0
        ? colortags::CurrentDisplayMode()
        : (g_options.modeOverride == 1 ? colortags::DisplayMode::Label
                                       : colortags::DisplayMode::Dot);
    g_diagLabelMode = g_workerConfig.mode == colortags::DisplayMode::Label ? 1 : 0;
    {
        std::wstring behavior = colortags::ToLower(colortags::TrimSpace(
            colortags::ReadRegString(colortags::kConfigKey, L"ScrollBehavior")));
        g_followScroll = behavior == L"follow";
    }
    const colortags::TagInfo* tags = colortags::AllTags();
    for (size_t index = 0; index < colortags::kTagCount; ++index) {
        g_workerConfig.labels[index] = colortags::LabelOf(tags[index].id);
    }
}

std::wstring WorkerLabel(const std::wstring& tag) {
    const colortags::TagInfo* tags = colortags::AllTags();
    for (size_t index = 0; index < colortags::kTagCount; ++index) {
        if (tag == tags[index].id) return g_workerConfig.labels[index];
    }
    return {};
}

void ExtendScrollTracking(DWORD milliseconds) {
    const ULONGLONG target = GetTickCount64() + milliseconds;
    ULONGLONG current = g_scrollTrackingUntil.load();
    while (current < target &&
           !g_scrollTrackingUntil.compare_exchange_weak(current, target)) {
    }
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n\0");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n\0");
    return value.substr(first, last - first + 1);
}

bool SamePath(std::wstring left, std::wstring right) {
    while (!left.empty() && (left.back() == L'\\' || left.back() == L'/')) left.pop_back();
    while (!right.empty() && (right.back() == L'\\' || right.back() == L'/')) right.pop_back();
    return Lower(std::move(left)) == Lower(std::move(right));
}

// CLR_INVALID for an unknown or empty id: the caller treats that as "no
// indicator". The palette itself lives in the shared contract header.
COLORREF TagColor(const std::wstring& tag) {
    return colortags::ColorOf(Lower(Trim(tag)));
}

std::wstring ReadTag(const std::wstring& path) {
    const std::wstring stream = path + L":ColorTag";
    HANDLE file = CreateFileW(stream.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char bytes[64]{};
    DWORD read = 0;
    ReadFile(file, bytes, sizeof(bytes) - 1, &read, nullptr);
    CloseHandle(file);
    if (!read) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(read), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(read), result.data(), count);
    return Trim(std::move(result));
}

// Explorer tabs all report the same visibility and the same size, so neither
// tells them apart. Three things might: a tab that is not on screen may be
// cloaked by DWM, the tab on screen owns the window with keyboard focus, and
// the sibling z-order should put it on top. All three are recorded; the choice
// uses focus first and z-order second, and simply prefers the topmost when
// nothing else distinguishes them.
bool IsCloaked(HWND window) {
    BOOL cloaked = FALSE;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
                                           sizeof(cloaked))) &&
           cloaked != FALSE;
}

int SiblingZIndex(HWND window) {
    HWND parent = GetAncestor(window, GA_PARENT);
    if (!parent) return -1;
    int index = 0;
    for (HWND sibling = GetWindow(parent, GW_CHILD); sibling;
         sibling = GetWindow(sibling, GW_HWNDNEXT)) {
        if (sibling == window) return index;
        if (++index > 512) break;
    }
    return -1;
}

bool HoldsFocus(HWND window) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    const DWORD thread = GetWindowThreadProcessId(window, nullptr);
    if (!thread || !GetGUIThreadInfo(thread, &info) || !info.hwndFocus) return false;
    return info.hwndFocus == window || IsChild(window, info.hwndFocus);
}

std::wstring LeafName(const std::wstring& path) {
    std::wstring trimmed = path;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/')) {
        trimmed.pop_back();
    }
    const size_t slash = trimmed.find_last_of(L"\\/");
    return slash == std::wstring::npos ? trimmed : trimmed.substr(slash + 1);
}

bool GetShellWindowData(HWND hwnd, std::wstring& folderPath,
                        std::map<std::wstring, std::wstring>& pathsByDisplayName,
                        std::vector<std::wstring>& orderedDisplayNames,
                        std::map<std::wstring, LONG>& viewYByDisplayName,
                        HWND& chosenViewWindow,
                        const std::wstring& activeTabName) {
    chosenViewWindow = nullptr;
    IShellWindows* rawWindows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&rawWindows)))) return false;
    ComPtr<IShellWindows> windows(rawWindows);
    long count = 0;
    if (FAILED(windows->get_Count(&count))) return false;

    // With Explorer tabs, every tab registers its own entry here and they all
    // report the same top-level HWND. Matching on the HWND alone therefore
    // picked whichever tab came first in the list - normally the one opened
    // first, not the one on screen - so the folder, the file map and the item
    // positions all described a tab the user was not looking at. That is what
    // left indicators from another tab standing in place over the current
    // tab's rows, ignoring its scrolling.
    //
    // The tab actually being shown is the one whose shell view window is
    // visible; the other tabs' view windows are hidden. The first pass takes
    // only a visible view. The second pass drops that requirement, so a build
    // whose view windows cannot be read still behaves as before.
    // Log every matching entry before choosing one. The loop below returns on
    // the first acceptable candidate, so without this the diagnostic only ever
    // described the tab that was taken and never the ones it was taken over.
    std::wstring diagnostic;
    long tabMatchIndex = -1;
    long focusedIndex = -1;
    long firstLiveIndex = -1;
    for (long index = 0; index < count; ++index) {
        VARIANT itemIndex{};
        VariantInit(&itemIndex);
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;
        IDispatch* rawDispatch = nullptr;
        if (FAILED(windows->Item(itemIndex, &rawDispatch)) || !rawDispatch) continue;
        ComPtr<IDispatch> dispatch(rawDispatch);
        IWebBrowserApp* rawBrowser = nullptr;
        if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&rawBrowser)))) continue;
        ComPtr<IWebBrowserApp> browser(rawBrowser);
        SHANDLE_PTR browserHwnd = 0;
        if (FAILED(browser->get_HWND(&browserHwnd)) ||
            reinterpret_cast<HWND>(browserHwnd) != hwnd) continue;
        IServiceProvider* rawProvider = nullptr;
        if (FAILED(browser->QueryInterface(IID_PPV_ARGS(&rawProvider)))) continue;
        ComPtr<IServiceProvider> provider(rawProvider);
        IShellBrowser* rawShellBrowser = nullptr;
        if (FAILED(provider->QueryService(SID_STopLevelBrowser,
                                          IID_PPV_ARGS(&rawShellBrowser)))) continue;
        ComPtr<IShellBrowser> shellBrowser(rawShellBrowser);
        IShellView* rawView = nullptr;
        if (FAILED(shellBrowser->QueryActiveShellView(&rawView))) continue;
        ComPtr<IShellView> view(rawView);
        HWND candidateView = nullptr;
        const bool haveView =
            SUCCEEDED(view->GetWindow(&candidateView)) && candidateView;
        if (!haveView) continue;
        const bool cloaked = IsCloaked(candidateView);
        const bool focused = HoldsFocus(candidateView);

        // The candidate's folder, without enumerating its contents - that is
        // the expensive part and it is not needed to identify a tab.
        std::wstring candidateFolder;
        std::wstring candidateDisplay;
        IFolderView* rawCandidateFolderView = nullptr;
        if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&rawCandidateFolderView))) &&
            rawCandidateFolderView) {
            ComPtr<IFolderView> candidateFolderView(rawCandidateFolderView);
            IPersistFolder2* rawPersist = nullptr;
            if (SUCCEEDED(candidateFolderView->GetFolder(IID_PPV_ARGS(&rawPersist))) &&
                rawPersist) {
                ComPtr<IPersistFolder2> persist(rawPersist);
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(persist->GetCurFolder(&pidl)) && pidl) {
                    wchar_t buffer[MAX_PATH * 4]{};
                    if (SHGetPathFromIDListW(pidl, buffer)) candidateFolder = buffer;
                    // A tab is captioned with the folder's display name, which
                    // is not its path's last component: C:\Users\<user>\OneDrive
                    // shows as "Vladimir - Personal". Matching on the leaf name
                    // therefore missed exactly the tabs that are renamed, and
                    // the miss fell back to the first tab in the list.
                    PWSTR rawDisplay = nullptr;
                    if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY,
                                                      &rawDisplay)) && rawDisplay) {
                        candidateDisplay = rawDisplay;
                        CoTaskMemFree(rawDisplay);
                    }
                    CoTaskMemFree(pidl);
                }
            }
        }
        const std::wstring leaf = LeafName(candidateFolder);
        const bool matchesTab =
            !activeTabName.empty() &&
            ((!candidateDisplay.empty() &&
              _wcsicmp(candidateDisplay.c_str(), activeTabName.c_str()) == 0) ||
             (!leaf.empty() && _wcsicmp(leaf.c_str(), activeTabName.c_str()) == 0));

        diagnostic += L"[idx=" + std::to_wstring(index) +
                      L" cloaked=" + (cloaked ? L"yes" : L"no") +
                      L" focus=" + (focused ? L"yes" : L"no") +
                      L" name=" +
                      (candidateDisplay.empty() ? std::wstring(L"?") : candidateDisplay) +
                      L" leaf=" + (leaf.empty() ? std::wstring(L"?") : leaf) +
                      (matchesTab ? L" MATCH" : L"") + L"] ";
        if (cloaked) continue;
        if (matchesTab && tabMatchIndex < 0) tabMatchIndex = index;
        if (focused && focusedIndex < 0) focusedIndex = index;
        if (firstLiveIndex < 0) firstLiveIndex = index;
    }
    // The tab caption is the only thing measured so far that actually differs
    // between tabs: visibility, size, cloaking and sibling z-order are all
    // identical across them. Focus stays as a second opinion and the first
    // living candidate as a last resort.
    const bool identified = tabMatchIndex >= 0;
    const long preferredIndex = identified ? tabMatchIndex
                              : (focusedIndex >= 0 ? focusedIndex : firstLiveIndex);
    diagnostic += L"ACTIVE TAB=\"" + activeTabName + L"\" PREFER idx=" +
                  std::to_wstring(preferredIndex) +
                  (identified ? L" (identified)" : L" (guess)") + L" ";

    // Once the tab is identified it is that tab or nothing. Falling through to
    // any other candidate is what put one tab's indicators over another's rows,
    // and over the Home page, which has no folder of its own at all.
    const int passCount = identified ? 1 : 2;
    for (int pass = 0; pass < passCount; ++pass) {
        const bool requirePreferred = pass == 0;
        for (long index = 0; index < count; ++index) {
            VARIANT itemIndex{};
            VariantInit(&itemIndex);
            itemIndex.vt = VT_I4;
            itemIndex.lVal = index;
            IDispatch* rawDispatch = nullptr;
            if (FAILED(windows->Item(itemIndex, &rawDispatch)) || !rawDispatch) continue;
            ComPtr<IDispatch> dispatch(rawDispatch);

            IWebBrowserApp* rawBrowser = nullptr;
            if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&rawBrowser)))) continue;
            ComPtr<IWebBrowserApp> browser(rawBrowser);
            SHANDLE_PTR browserHwnd = 0;
            if (FAILED(browser->get_HWND(&browserHwnd)) ||
                reinterpret_cast<HWND>(browserHwnd) != hwnd) continue;

            // A tab that fails any of these steps must not abandon the search:
            // the tab on screen may be the next entry in the list.
            IServiceProvider* rawProvider = nullptr;
            if (FAILED(browser->QueryInterface(IID_PPV_ARGS(&rawProvider)))) continue;
            ComPtr<IServiceProvider> provider(rawProvider);
            IShellBrowser* rawShellBrowser = nullptr;
            if (FAILED(provider->QueryService(SID_STopLevelBrowser,
                                              IID_PPV_ARGS(&rawShellBrowser)))) continue;
            ComPtr<IShellBrowser> shellBrowser(rawShellBrowser);
            IShellView* rawView = nullptr;
            if (FAILED(shellBrowser->QueryActiveShellView(&rawView))) continue;
            ComPtr<IShellView> view(rawView);
            HWND viewWindow = nullptr;
            const bool haveViewWindow =
                SUCCEEDED(view->GetWindow(&viewWindow)) && viewWindow;
            const bool viewVisible = haveViewWindow && IsWindowVisible(viewWindow);
            RECT viewRect{};
            const bool haveRect =
                haveViewWindow && GetWindowRect(viewWindow, &viewRect);
            (void)haveRect;
            (void)viewRect;
            (void)viewVisible;
            if (requirePreferred && index != preferredIndex) continue;
            IFolderView* rawFolderView = nullptr;
            if (FAILED(view->QueryInterface(IID_PPV_ARGS(&rawFolderView)))) continue;
            ComPtr<IFolderView> folderView(rawFolderView);

            // Nothing is written to the caller's buffers until a candidate is
            // committed to, so a half-read tab cannot mix into the next one.
            folderPath.clear();
            pathsByDisplayName.clear();
            orderedDisplayNames.clear();
            viewYByDisplayName.clear();

            IPersistFolder2* rawPersist = nullptr;
            if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&rawPersist)))) continue;
            ComPtr<IPersistFolder2> persist(rawPersist);
            PIDLIST_ABSOLUTE folderPidl = nullptr;
            if (FAILED(persist->GetCurFolder(&folderPidl)) || !folderPidl) continue;
            wchar_t pathBuffer[MAX_PATH * 4]{};
            const bool filesystemFolder = SHGetPathFromIDListW(folderPidl, pathBuffer) != FALSE;
            if (!filesystemFolder) {
                CoTaskMemFree(folderPidl);
                continue;
            }
            folderPath = pathBuffer;

            IShellFolder* rawFolder = nullptr;
            if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&rawFolder)))) {
                CoTaskMemFree(folderPidl);
                continue;
            }
            ComPtr<IShellFolder> folder(rawFolder);
            IEnumIDList* rawItems = nullptr;
            if (SUCCEEDED(folderView->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&rawItems))) && rawItems) {
                ComPtr<IEnumIDList> items(rawItems);
                PITEMID_CHILD child = nullptr;
                while (items->Next(1, &child, nullptr) == S_OK) {
                    STRRET displayResult{};
                    wchar_t display[MAX_PATH * 4]{};
                    if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_NORMAL, &displayResult)) &&
                        SUCCEEDED(StrRetToBufW(&displayResult, child, display,
                                               static_cast<UINT>(std::size(display))))) {
                        const std::wstring lowerDisplay = Lower(display);
                        orderedDisplayNames.push_back(lowerDisplay);
                        POINT viewPosition{};
                        if (SUCCEEDED(folderView->GetItemPosition(child, &viewPosition))) {
                            viewYByDisplayName[lowerDisplay] = viewPosition.y;
                        }
                        PIDLIST_ABSOLUTE full = ILCombine(folderPidl, child);
                        wchar_t itemPath[MAX_PATH * 4]{};
                        if (full && SHGetPathFromIDListW(full, itemPath)) {
                            pathsByDisplayName[Lower(display)] = itemPath;
                        }
                        if (full) ILFree(full);
                    }
                    CoTaskMemFree(child);
                    child = nullptr;
                }
            }
            chosenViewWindow = viewWindow;
            diagnostic += L"CHOSEN pass=" + std::to_wstring(pass) +
                          L" idx=" + std::to_wstring(index) +
                          L" items=" + std::to_wstring(orderedDisplayNames.size()) +
                          L" folder=" + folderPath;
            g_diagTabs = diagnostic;
            CoTaskMemFree(folderPidl);
            return true;
        }
    }
    g_diagTabs = diagnostic + L"NONE CHOSEN";
    return false;
}

IUIAutomationCondition* MakeControlTypeCondition(IUIAutomation* automation, int type) {
    VARIANT value{};
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = type;
    IUIAutomationCondition* condition = nullptr;
    automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition);
    VariantClear(&value);
    return condition;
}

IUIAutomationCondition* MakeEitherControlType(IUIAutomation* automation, int first, int second) {
    ComPtr<IUIAutomationCondition> firstCondition(MakeControlTypeCondition(automation, first));
    ComPtr<IUIAutomationCondition> secondCondition(MakeControlTypeCondition(automation, second));
    if (!firstCondition || !secondCondition) return nullptr;
    IUIAutomationCondition* combined = nullptr;
    automation->CreateOrCondition(firstCondition.get(), secondCondition.get(), &combined);
    return combined;
}

void AppendIndicator(WindowSnapshot& snapshot, const RECT& rowRect, double centerX,
                     LONG columnLeft, LONG columnRight, const std::wstring& tag,
                     COLORREF color) {
    // Label mode falls back to the dot when the column cannot hold a readable
    // badge, so narrowing the column degrades instead of drawing a sliver.
    if (g_workerConfig.mode == colortags::DisplayMode::Label &&
        columnRight - columnLeft >= kMinimumLabelWidth) {
        std::wstring label = WorkerLabel(tag);
        if (!label.empty()) {
            const LONG rowHeight = rowRect.bottom - rowRect.top;
            const LONG height = std::clamp(rowHeight - 4, 12L, 22L);
            const LONG centerY = (rowRect.top + rowRect.bottom) / 2;
            Indicator badge{};
            badge.color = color;
            badge.label = std::move(label);
            badge.cell = {columnLeft + kLabelColumnInset,
                          centerY - height / 2,
                          columnRight - kLabelColumnInset,
                          centerY - height / 2 + height};
            // The painted badge only ever shrinks from the cell, so the cell is
            // a safe bounding box for sizing the overlay window.
            badge.erase = badge.cell;
            snapshot.dots.push_back(std::move(badge));
            return;
        }
    }
    const int coverSize = g_options.dotSize + 4;
    const double radius = coverSize / 2.0;
    const int eraseSize = std::max(18, g_options.dotSize + 4);
    const double eraseRadius = eraseSize / 2.0;
    const double centerY = (rowRect.top + rowRect.bottom) / 2.0;
    Indicator dot{};
    dot.circle = {
        static_cast<LONG>(std::lround(centerX - radius)),
        static_cast<LONG>(std::lround(centerY - radius)),
        static_cast<LONG>(std::lround(centerX - radius)) + coverSize,
        static_cast<LONG>(std::lround(centerY - radius)) + coverSize
    };
    dot.erase = {
        static_cast<LONG>(std::lround(centerX - eraseRadius)),
        static_cast<LONG>(std::lround(centerY - eraseRadius)),
        static_cast<LONG>(std::lround(centerX - eraseRadius)) + eraseSize,
        static_cast<LONG>(std::lround(centerY - eraseRadius)) + eraseSize
    };
    dot.color = color;
    snapshot.dots.push_back(std::move(dot));
}

bool FinishSnapshot(HWND hwnd, const RECT& rootRect, const WorkerWindowCache& cache,
                    WindowSnapshot& snapshot) {
    if (snapshot.dots.empty()) return false;
    RECT bounds = snapshot.dots.front().erase;
    for (const Indicator& dot : snapshot.dots) {
        bounds.left = std::min(bounds.left, dot.erase.left);
        bounds.top = std::min(bounds.top, dot.erase.top);
        bounds.right = std::max(bounds.right, dot.erase.right);
        bounds.bottom = std::max(bounds.bottom, dot.erase.bottom);
    }
    snapshot.explorer = hwnd;
    snapshot.explorerRect = rootRect;
    snapshot.overlayRect = bounds;
    snapshot.folder = cache.folder;
    snapshot.rowHeight = cache.rowHeight;
    snapshot.contentTopOffset = cache.contentTopOffset;
    snapshot.contentBottomOffset = cache.contentBottomOffset;
    snapshot.verticalScrollPercent = cache.verticalScrollPercent;
    snapshot.verticalViewSize = cache.verticalViewSize;
    return true;
}

bool RefreshScrollMetrics(WorkerWindowCache& cache) {
    if (!cache.scrollElement) return false;
    IUIAutomationScrollPattern* rawPattern = nullptr;
    if (FAILED(cache.scrollElement->GetCurrentPatternAs(
            UIA_ScrollPatternId, IID_PPV_ARGS(&rawPattern))) || !rawPattern) {
        cache.verticalScrollPercent = -1.0;
        cache.verticalViewSize = 0.0;
        return false;
    }
    ComPtr<IUIAutomationScrollPattern> pattern(rawPattern);
    BOOL scrollable = FALSE;
    double percent = -1.0;
    double viewSize = 0.0;
    if (FAILED(pattern->get_CurrentVerticallyScrollable(&scrollable)) || !scrollable ||
        FAILED(pattern->get_CurrentVerticalScrollPercent(&percent)) ||
        FAILED(pattern->get_CurrentVerticalViewSize(&viewSize)) ||
        percent < 0.0 || viewSize <= 0.0 || viewSize > 100.0) {
        cache.verticalScrollPercent = -1.0;
        cache.verticalViewSize = 0.0;
        return false;
    }
    cache.verticalScrollPercent = std::clamp(percent, 0.0, 100.0);
    cache.verticalViewSize = viewSize;
    return true;
}

void FindScrollElement(IUIAutomation* automation, IUIAutomationElement* root,
                       const RECT& headerRect, WorkerWindowCache& cache) {
    cache.scrollElement.reset();
    cache.verticalScrollPercent = -1.0;
    cache.verticalViewSize = 0.0;
    VARIANT value{};
    VariantInit(&value);
    value.vt = VT_BOOL;
    value.boolVal = VARIANT_TRUE;
    IUIAutomationCondition* rawCondition = nullptr;
    automation->CreatePropertyCondition(UIA_IsScrollPatternAvailablePropertyId,
                                        value, &rawCondition);
    VariantClear(&value);
    ComPtr<IUIAutomationCondition> condition(rawCondition);
    if (!condition) return;
    IUIAutomationElementArray* rawElements = nullptr;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.get(), &rawElements)) ||
        !rawElements) return;
    ComPtr<IUIAutomationElementArray> elements(rawElements);
    int count = 0;
    elements->get_Length(&count);
    for (int index = 0; index < count; ++index) {
        IUIAutomationElement* rawElement = nullptr;
        if (FAILED(elements->GetElement(index, &rawElement)) || !rawElement) continue;
        ComPtr<IUIAutomationElement> element(rawElement);
        RECT bounds{};
        if (FAILED(element->get_CurrentBoundingRectangle(&bounds))) continue;
        if (bounds.left > headerRect.left || bounds.right < headerRect.right ||
            bounds.top > headerRect.top || bounds.bottom <= headerRect.bottom) continue;
        element->AddRef();
        cache.scrollElement.reset(element.get());
        if (RefreshScrollMetrics(cache)) return;
        cache.scrollElement.reset();
    }
}

// Nothing the shell exposes about a tab's own window differs between tabs -
// visibility, size, cloaking and sibling z-order are identical across all of
// them, measured. The tab strip, though, marks exactly one item selected, and
// its caption is the folder's display name. Two tabs on folders whose names
// end the same way defeat this; nothing better has turned up.
//
// A window holds more than one set of tabs: the first selected TabItem found
// by walking the whole window was "Recent", which belongs to the Home page and
// not to the browser tabs at all. The browser tab strip lives in the title bar,
// so only items near the top edge count.
constexpr LONG kTabStripHeight = 60;

std::wstring FindSelectedTabName(IUIAutomation* automation,
                                 IUIAutomationElement* root,
                                 const RECT& rootRect) {
    ComPtr<IUIAutomationCondition> condition(
        MakeControlTypeCondition(automation, UIA_TabItemControlTypeId));
    if (!condition) return {};
    IUIAutomationElementArray* rawItems = nullptr;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.get(), &rawItems)) ||
        !rawItems) return {};
    ComPtr<IUIAutomationElementArray> items(rawItems);
    int count = 0;
    items->get_Length(&count);
    std::wstring log;
    std::wstring chosen;
    for (int index = 0; index < count; ++index) {
        IUIAutomationElement* rawItem = nullptr;
        if (FAILED(items->GetElement(index, &rawItem)) || !rawItem) continue;
        ComPtr<IUIAutomationElement> item(rawItem);

        RECT bounds{};
        const bool haveBounds =
            SUCCEEDED(item->get_CurrentBoundingRectangle(&bounds)) &&
            bounds.right > bounds.left;
        const LONG offsetTop = haveBounds ? bounds.top - rootRect.top : -1;

        VARIANT selected{};
        VariantInit(&selected);
        bool isSelected = false;
        if (SUCCEEDED(item->GetCurrentPropertyValue(
                UIA_SelectionItemIsSelectedPropertyId, &selected))) {
            isSelected = selected.vt == VT_BOOL && selected.boolVal == VARIANT_TRUE;
        }
        VariantClear(&selected);

        std::wstring name;
        BSTR rawName = nullptr;
        if (SUCCEEDED(item->get_CurrentName(&rawName)) && rawName) {
            name = Trim(std::wstring(rawName, SysStringLen(rawName)));
            SysFreeString(rawName);
        }

        const bool inTabStrip = haveBounds && offsetTop >= 0 &&
                                offsetTop < kTabStripHeight;
        log += L"[" + name + L" top=" + std::to_wstring(offsetTop) +
               (isSelected ? L" selected" : L"") +
               (inTabStrip ? L" strip" : L"") + L"] ";
        if (isSelected && inTabStrip && chosen.empty()) chosen = name;
    }
    g_diagTabStrip = log;
    return chosen;
}

bool ScanExplorerWindow(IUIAutomation* automation, HWND hwnd, WindowSnapshot& snapshot,
                        WorkerWindowCache& cache, bool refreshShell,
                        bool rebuildRows, bool& scannedCleanly) {
    // Explorer tabs share one top-level window, so switching tabs changes the
    // folder without changing the HWND. Everything cached for this window - the
    // file map, the row elements, their last rectangles - then describes what
    // was there before, and the cached path would keep drawing those indicators
    // over whatever the new tab shows, next to completely unrelated files.
    //
    // The title is the cheapest signal that this happened, and a safe one to
    // read from the worker: for a window owned by another process GetWindowText
    // returns the stored text instead of sending WM_GETTEXT, so it cannot block
    // on Explorer. Two tabs whose folders share a leaf name defeat it; the
    // periodic shell refresh stays as the backstop for that.
    wchar_t windowTitle[512]{};
    GetWindowTextW(hwnd, windowTitle, static_cast<int>(std::size(windowTitle)));
    if (cache.title != windowTitle) {
        const bool hadPreviousContent = !cache.title.empty();
        cache = {};
        cache.title = windowTitle;
        refreshShell = true;
        // Take the stale indicators off screen now; the rebuilt scan puts the
        // right ones back.
        if (hadPreviousContent && g_manager) {
            PostMessageW(g_manager, kHideForContentChange,
                         reinterpret_cast<WPARAM>(hwnd), 0);
        }
    }
    // The root is needed before the shell lookup now: the tab strip is what
    // says which tab that lookup should describe.
    IUIAutomationElement* rawRoot = nullptr;
    if (FAILED(automation->ElementFromHandle(hwnd, &rawRoot)) || !rawRoot) return false;
    ComPtr<IUIAutomationElement> root(rawRoot);
    RECT rootRect{};
    if (FAILED(root->get_CurrentBoundingRectangle(&rootRect)) ||
        rootRect.right <= rootRect.left || rootRect.bottom <= rootRect.top) return false;

    if (refreshShell || cache.pathsByDisplayName.empty()) {
        std::wstring folder;
        std::map<std::wstring, std::wstring> paths;
        std::vector<std::wstring> orderedNames;
        std::map<std::wstring, LONG> viewY;
        HWND viewWindow = nullptr;
        const std::wstring activeTabName =
            FindSelectedTabName(automation, root.get(), rootRect);
        if (!GetShellWindowData(hwnd, folder, paths, orderedNames, viewY, viewWindow,
                                activeTabName)) {
            cache = {};
            return false;
        }
        cache.viewWindow = viewWindow;
        cache.folder = std::move(folder);
        cache.pathsByDisplayName = std::move(paths);
        cache.orderedDisplayNames = std::move(orderedNames);
        cache.viewYByDisplayName = std::move(viewY);
        cache.geometryValid = false;
        cache.taggedRows.clear();
    }
    if (!g_options.targetFolder.empty() &&
        !SamePath(cache.folder, g_options.targetFolder)) {
        ++g_diagFolderMismatch;
        return false;
    }

    // Rows and the column header are searched inside the active tab's own view
    // window. The top-level element returns the rows of every open tab - a tab
    // that is not on screen keeps its list alive in the tree - so scanning from
    // there mixed several tabs together, measured a row pitch from the mixture
    // and laid indicators out over rows belonging to a folder the user was not
    // looking at. rootRect stays the top-level window: the overlay is
    // positioned against that, not against the view.
    ComPtr<IUIAutomationElement> viewRoot;
    if (cache.viewWindow && IsWindow(cache.viewWindow)) {
        IUIAutomationElement* rawViewRoot = nullptr;
        if (SUCCEEDED(automation->ElementFromHandle(cache.viewWindow, &rawViewRoot)) &&
            rawViewRoot) {
            viewRoot.reset(rawViewRoot);
        }
    }
    IUIAutomationElement* searchRoot = viewRoot ? viewRoot.get() : root.get();

    if (!rebuildRows) RefreshScrollMetrics(cache);

    if (!rebuildRows && cache.geometryValid && !cache.taggedRows.empty()) {
        const double centerX = rootRect.left + cache.centerXOffset;
        const LONG columnLeft = rootRect.left + cache.columnLeftOffset;
        const LONG columnRight = rootRect.left + cache.columnRightOffset;
        LONG deltaY = 0;
        bool haveAnchor = false;
        for (const CachedTaggedRow& cached : cache.taggedRows) {
            if (!cached.element) continue;
            RECT current{};
            if (SUCCEEDED(cached.element->get_CurrentBoundingRectangle(&current)) &&
                current.bottom > current.top) {
                deltaY = current.top - cached.lastRect.top;
                haveAnchor = true;
                break;
            }
        }
        if (haveAnchor) {
            const LONG contentTop = rootRect.top + cache.contentTopOffset;
            for (CachedTaggedRow& cached : cache.taggedRows) {
                OffsetRect(&cached.lastRect, 0, deltaY);
                if (cached.lastRect.bottom <= contentTop ||
                    cached.lastRect.top >= rootRect.bottom) continue;
                AppendIndicator(snapshot, cached.lastRect, centerX, columnLeft,
                                columnRight, cached.tag, cached.color);
            }
        } else {
            for (CachedTaggedRow& cached : cache.taggedRows) {
                if (!cached.element) continue;
                BOOL offscreen = TRUE;
                if (FAILED(cached.element->get_CurrentIsOffscreen(&offscreen)) || offscreen) continue;
                RECT rowRect{};
                if (FAILED(cached.element->get_CurrentBoundingRectangle(&rowRect))) continue;
                cached.lastRect = rowRect;
                AppendIndicator(snapshot, rowRect, centerX, columnLeft,
                                columnRight, cached.tag, cached.color);
            }
        }
        scannedCleanly = true;
        return FinishSnapshot(hwnd, rootRect, cache, snapshot);
    }

    ComPtr<IUIAutomationCondition> headerCondition(
        MakeEitherControlType(automation, UIA_HeaderItemControlTypeId,
                              UIA_SplitButtonControlTypeId));
    IUIAutomationElementArray* rawHeaders = nullptr;
    if (!headerCondition || FAILED(searchRoot->FindAll(TreeScope_Descendants,
                                                       headerCondition.get(), &rawHeaders)) ||
        !rawHeaders) return false;
    ComPtr<IUIAutomationElementArray> headers(rawHeaders);
    RECT headerRect{};
    bool foundHeader = false;
    int headerCount = 0;
    headers->get_Length(&headerCount);
    for (int i = 0; i < headerCount; ++i) {
        IUIAutomationElement* rawHeader = nullptr;
        if (FAILED(headers->GetElement(i, &rawHeader)) || !rawHeader) continue;
        ComPtr<IUIAutomationElement> header(rawHeader);
        BSTR rawName = nullptr;
        if (SUCCEEDED(header->get_CurrentName(&rawName)) && rawName) {
            const std::wstring name(rawName, SysStringLen(rawName));
            SysFreeString(rawName);
            if (_wcsicmp(name.c_str(), g_options.columnName.c_str()) == 0 &&
                SUCCEEDED(header->get_CurrentBoundingRectangle(&headerRect))) {
                foundHeader = true;
                break;
            }
        }
    }
    if (!foundHeader) return false;
    ++g_diagColumnFound;

    if (!RefreshScrollMetrics(cache)) {
        FindScrollElement(automation, searchRoot, headerRect, cache);
    }

    ComPtr<IUIAutomationCondition> rowCondition(
        MakeEitherControlType(automation, UIA_DataItemControlTypeId,
                              UIA_ListItemControlTypeId));
    // One request for the whole list instead of three calls per row. Reading
    // a name, a rectangle and an offscreen flag separately meant three trips
    // across the process boundary for every row, several times a second; asking
    // for all of it up front makes it one. It also measures every row at the
    // same instant, so a list that moves while it is being read can no longer
    // produce a snapshot stitched from two different positions.
    ComPtr<IUIAutomationCacheRequest> cacheRequest;
    {
        IUIAutomationCacheRequest* rawCacheRequest = nullptr;
        if (SUCCEEDED(automation->CreateCacheRequest(&rawCacheRequest)) &&
            rawCacheRequest) {
            cacheRequest.reset(rawCacheRequest);
            cacheRequest->AddProperty(UIA_NamePropertyId);
            cacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);
            cacheRequest->AddProperty(UIA_IsOffscreenPropertyId);
            // Full, not None: these elements are kept in the cache between
            // passes and asked for their live position later.
            cacheRequest->put_AutomationElementMode(AutomationElementMode_Full);
        }
    }

    IUIAutomationElementArray* rawRows = nullptr;
    const HRESULT rowsResult =
        cacheRequest
            ? searchRoot->FindAllBuildCache(TreeScope_Descendants, rowCondition.get(),
                                            cacheRequest.get(), &rawRows)
            : searchRoot->FindAll(TreeScope_Descendants, rowCondition.get(), &rawRows);
    if (!rowCondition || FAILED(rowsResult) || !rawRows) return false;
    ComPtr<IUIAutomationElementArray> rows(rawRows);
    const bool cached = cacheRequest != nullptr;

    const double radius = g_options.dotSize / 2.0;
    const double centerX = _wcsicmp(g_options.columnName.c_str(), L"Tags") == 0
        ? headerRect.left + std::max(radius + 2.0, (headerRect.bottom - headerRect.top) * 0.43)
        : (headerRect.left + headerRect.right) / 2.0;
    cache.centerXOffset = centerX - rootRect.left;
    cache.columnLeftOffset = headerRect.left - rootRect.left;
    cache.columnRightOffset = headerRect.right - rootRect.left;
    cache.contentTopOffset = headerRect.bottom - rootRect.top;
    cache.contentBottomOffset = rootRect.bottom - rootRect.top;
    cache.geometryValid = true;
    cache.taggedRows.clear();

    int rowCount = 0;
    rows->get_Length(&rowCount);
    g_diagRowsSeen += rowCount;
    std::vector<LONG> rowHeights;
    std::vector<LONG> rowTops;
    std::map<std::wstring, size_t> indicesByName;
    for (size_t index = 0; index < cache.orderedDisplayNames.size(); ++index) {
        indicesByName.emplace(cache.orderedDisplayNames[index], index);
    }
    size_t anchorIndex = static_cast<size_t>(-1);
    LONG anchorViewY = 0;
    RECT anchorRect{};
    std::vector<size_t> visibleTaggedIndices;
    for (int i = 0; i < rowCount; ++i) {
        IUIAutomationElement* rawRow = nullptr;
        if (FAILED(rows->GetElement(i, &rawRow)) || !rawRow) continue;
        ComPtr<IUIAutomationElement> row(rawRow);
        RECT rowRect{};
        if (FAILED(cached ? row->get_CachedBoundingRectangle(&rowRect)
                          : row->get_CurrentBoundingRectangle(&rowRect))) continue;
        if (rowRect.bottom <= rowRect.top) continue;
        rowHeights.push_back(rowRect.bottom - rowRect.top);
        rowTops.push_back(rowRect.top);
        BSTR rawName = nullptr;
        if (FAILED(cached ? row->get_CachedName(&rawName)
                          : row->get_CurrentName(&rawName)) || !rawName) continue;
        std::wstring name(rawName, SysStringLen(rawName));
        SysFreeString(rawName);
        const std::wstring lowerName = Lower(name);
        const auto ordered = indicesByName.find(lowerName);
        const auto viewY = cache.viewYByDisplayName.find(lowerName);
        if (anchorIndex == static_cast<size_t>(-1) &&
            ordered != indicesByName.end() && viewY != cache.viewYByDisplayName.end()) {
            anchorIndex = ordered->second;
            anchorViewY = viewY->second;
            anchorRect = rowRect;
        }
        BOOL offscreen = TRUE;
        if (FAILED(cached ? row->get_CachedIsOffscreen(&offscreen)
                          : row->get_CurrentIsOffscreen(&offscreen)) ||
            offscreen) continue;
        const auto path = cache.pathsByDisplayName.find(lowerName);
        if (path == cache.pathsByDisplayName.end()) continue;
        const std::wstring tag = ReadTag(path->second);
        const COLORREF color = TagColor(tag);
        if (color == CLR_INVALID) continue;
        ++g_diagTaggedRows;
        AppendIndicator(snapshot, rowRect, centerX, headerRect.left,
                        headerRect.right, tag, color);
        row->AddRef();
        CachedTaggedRow cached;
        cached.element.reset(row.get());
        cached.tag = tag;
        cached.color = color;
        cached.lastRect = rowRect;
        cache.taggedRows.push_back(std::move(cached));
        if (ordered != indicesByName.end()) visibleTaggedIndices.push_back(ordered->second);
    }
    std::vector<LONG> rowPitches;
    std::sort(rowTops.begin(), rowTops.end());
    for (size_t index = 1; index < rowTops.size(); ++index) {
        const LONG difference = rowTops[index] - rowTops[index - 1];
        if (difference >= 8 && difference <= 128) rowPitches.push_back(difference);
    }
    if (!rowPitches.empty()) {
        const auto middle = rowPitches.begin() + rowPitches.size() / 2;
        std::nth_element(rowPitches.begin(), middle, rowPitches.end());
        cache.rowHeight = static_cast<double>(*middle);
    } else if (!rowHeights.empty()) {
        const auto middle = rowHeights.begin() + rowHeights.size() / 2;
        std::nth_element(rowHeights.begin(), middle, rowHeights.end());
        cache.rowHeight = static_cast<double>(*middle);
    }
    snapshot.rowHeight = cache.rowHeight;

    if (anchorIndex != static_cast<size_t>(-1) && cache.rowHeight > 0.0) {
        const LONG viewportHeight = std::max(1L, rootRect.bottom - headerRect.bottom);
        const LONG bufferTop = headerRect.bottom - viewportHeight;
        const LONG bufferBottom = rootRect.bottom + viewportHeight;
        const LONG itemHeight = anchorRect.bottom - anchorRect.top;
        for (size_t index = 0; index < cache.orderedDisplayNames.size(); ++index) {
            if (std::find(visibleTaggedIndices.begin(), visibleTaggedIndices.end(), index) !=
                visibleTaggedIndices.end()) continue;
            const auto viewY = cache.viewYByDisplayName.find(
                cache.orderedDisplayNames[index]);
            if (viewY == cache.viewYByDisplayName.end()) continue;
            const LONG top = anchorRect.top + (viewY->second - anchorViewY);
            RECT predictedRect = anchorRect;
            predictedRect.top = top;
            predictedRect.bottom = top + itemHeight;
            if (predictedRect.bottom < bufferTop || predictedRect.top > bufferBottom) continue;
            const auto path = cache.pathsByDisplayName.find(cache.orderedDisplayNames[index]);
            if (path == cache.pathsByDisplayName.end()) continue;
            const std::wstring tag = ReadTag(path->second);
            const COLORREF color = TagColor(tag);
            if (color == CLR_INVALID) continue;
            AppendIndicator(snapshot, predictedRect, centerX, headerRect.left,
                            headerRect.right, tag, color);
            CachedTaggedRow cached;
            cached.tag = tag;
            cached.color = color;
            cached.lastRect = predictedRect;
            cache.taggedRows.push_back(std::move(cached));
        }
    }
    scannedCleanly = true;
    return FinishSnapshot(hwnd, rootRect, cache, snapshot);
}

BOOL CALLBACK CollectExplorerWindows(HWND hwnd, LPARAM value) {
    if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;
    wchar_t className[64]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (wcscmp(className, L"CabinetWClass") != 0 &&
        wcscmp(className, L"ExploreWClass") != 0) return TRUE;
    auto* windows = reinterpret_cast<std::vector<HWND>*>(value);
    windows->push_back(hwnd);
    return TRUE;
}

DWORD WINAPI ScanThread(void*) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IUIAutomation* rawAutomation = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&rawAutomation))) || !rawAutomation) {
        CoUninitialize();
        return 1;
    }
    ComPtr<IUIAutomation> automation(rawAutomation);
    std::map<HWND, WorkerWindowCache> caches;
    ULONGLONG lastFullRefresh = 0;
    ULONGLONG lastShellRefresh = 0;
    ULONGLONG lastVisualRefresh = 0;
    HANDLE waits[] = {g_stopEvent, g_contentEvent};
    while (true) {
        const ULONGLONG now = GetTickCount64();
        bool fullRefresh = g_forceShellRefresh.load() || lastFullRefresh == 0 ||
            now - lastFullRefresh >= static_cast<ULONGLONG>(g_options.refreshMilliseconds);
        DWORD waitResult = WAIT_TIMEOUT;
        if (!fullRefresh) {
            const DWORD fullDelay = static_cast<DWORD>(
                g_options.refreshMilliseconds - (now - lastFullRefresh));
            waitResult = WaitForMultipleObjects(2, waits, FALSE, fullDelay);
        } else if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult == WAIT_OBJECT_0) break;

        if (!fullRefresh && waitResult == WAIT_TIMEOUT) fullRefresh = true;
        if (!fullRefresh && waitResult == WAIT_OBJECT_0 + 1) {
            const ULONGLONG eventTime = GetTickCount64();
            constexpr DWORD kMinimumVisualInterval = 33;
            constexpr DWORD kEventSettleDelay = 8;
            const DWORD sinceLast = static_cast<DWORD>(eventTime - lastVisualRefresh);
            const DWORD delay = std::max(
                kEventSettleDelay,
                sinceLast < kMinimumVisualInterval ? kMinimumVisualInterval - sinceLast : 0u);
            if (WaitForSingleObject(g_stopEvent, delay) == WAIT_OBJECT_0) break;
        }

        if (g_scanInFlight.exchange(true)) {
            HANDLE completionWaits[] = {g_stopEvent, g_scanConsumedEvent};
            if (WaitForMultipleObjects(2, completionWaits, FALSE, 100) == WAIT_OBJECT_0) {
                break;
            }
            SetEvent(g_contentEvent);
            continue;
        }
        // Cheap enough to reload every pass, which is what makes a rename or a
        // mode switch show up without restarting the helper.
        RefreshWorkerConfig();
        // A tab switch or a navigation means a different folder, so the shell
        // data is re-read now instead of at the end of the refresh interval,
        // and the rows are rebuilt rather than reused from the cache.
        const bool forcedShell = g_forceShellRefresh.exchange(false);
        if (forcedShell) fullRefresh = true;
        const bool refreshShell = forcedShell ||
            (fullRefresh && (lastShellRefresh == 0 ||
                             GetTickCount64() - lastShellRefresh >= 1500));
        auto result = std::make_unique<ScanResult>();
        result->fullRefresh = fullRefresh;
        result->contentGeneration = g_contentGeneration.load();
        if (!fullRefresh) ++g_fastScanCount;
        std::vector<HWND> windows;
        EnumWindows(CollectExplorerWindows, reinterpret_cast<LPARAM>(&windows));
        if (fullRefresh) {
            g_diagExplorerWindows = static_cast<int>(windows.size());
            g_diagColumnFound = 0;
            g_diagRowsSeen = 0;
            g_diagTaggedRows = 0;
            g_diagFolderMismatch = 0;
        }
        std::wstring windowsDetail;
        for (HWND hwnd : windows) {
            WindowSnapshot snapshot;
            bool scannedCleanly = false;
            const bool scanned = ScanExplorerWindow(automation.get(), hwnd, snapshot,
                                                    caches[hwnd], refreshShell,
                                                    fullRefresh, scannedCleanly);
            if (fullRefresh) {
                wchar_t title[256]{};
                GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
                windowsDetail +=
                    L"[hwnd=" +
                    std::to_wstring(reinterpret_cast<uintptr_t>(hwnd)) +
                    L" title=\"" + title + L"\"" +
                    L" scanned=" + (scanned ? L"yes" : L"no") +
                    L" clean=" + (scannedCleanly ? L"yes" : L"no") +
                    L" indicators=" +
                    std::to_wstring(scanned ? snapshot.dots.size() : 0u) +
                    L" foreground=" +
                    (hwnd == GetForegroundWindow() ? L"yes" : L"no") + L"] ";
            }
            if (scanned) {
                result->windows.push_back(std::move(snapshot));
            } else if (scannedCleanly) {
                result->emptyWindows.push_back(hwnd);
            }
        }
        if (fullRefresh) {
            std::lock_guard<std::mutex> lock(g_diagWindowsMutex);
            g_diagWindowsDetail = std::move(windowsDetail);
        }
        if (fullRefresh) {
            std::vector<HWND> stale;
            for (const auto& [hwnd, cache] : caches) {
                if (std::find(windows.begin(), windows.end(), hwnd) == windows.end()) {
                    stale.push_back(hwnd);
                }
            }
            for (HWND hwnd : stale) caches.erase(hwnd);
        }
        lastVisualRefresh = GetTickCount64();
        if (fullRefresh) lastFullRefresh = lastVisualRefresh;
        if (refreshShell) lastShellRefresh = lastVisualRefresh;
        ScanResult* transferred = result.release();
        if (!PostMessageW(g_manager, kScanReady, 0, reinterpret_cast<LPARAM>(transferred))) {
            delete transferred;
            g_scanInFlight = false;
        }
    }
    automation.reset();
    CoUninitialize();
    return 0;
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Explorer's list uses the system UI font, so the badge takes its face from
// the same place and only sizes it to the row. Taking just the face name keeps
// this DPI-correct: the row rectangle already arrives in physical pixels.
void EnsureLabelFont(OverlayWindow& overlay, LONG badgeHeight) {
    const int target = std::clamp(static_cast<int>(badgeHeight * 0.62), 10, 18);
    if (overlay.font && overlay.fontHeight == target) return;
    if (overlay.font) DeleteObject(overlay.font);
    overlay.font = nullptr;
    overlay.fontHeight = target;
    LOGFONTW logFont{};
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        logFont = metrics.lfMessageFont;
    } else {
        lstrcpynW(logFont.lfFaceName, L"Segoe UI", LF_FACESIZE);
    }
    logFont.lfHeight = -target;
    logFont.lfWidth = 0;
    logFont.lfWeight = FW_NORMAL;
    logFont.lfQuality = CLEARTYPE_QUALITY;
    overlay.font = CreateFontIndirectW(&logFont);
}

void DestroySurface(OverlayWindow& overlay) {
    if (overlay.memoryDc && overlay.previousBitmap) {
        SelectObject(overlay.memoryDc, overlay.previousBitmap);
    }
    if (overlay.bitmap) DeleteObject(overlay.bitmap);
    if (overlay.memoryDc) DeleteDC(overlay.memoryDc);
    overlay.memoryDc = nullptr;
    overlay.bitmap = nullptr;
    overlay.previousBitmap = nullptr;
    overlay.pixels = nullptr;
}

bool CreateSurface(OverlayWindow& overlay, int width, int height) {
    DestroySurface(overlay);
    HDC screen = GetDC(nullptr);
    overlay.memoryDc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    overlay.bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                      &overlay.pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!overlay.memoryDc || !overlay.bitmap || !overlay.pixels) {
        DestroySurface(overlay);
        return false;
    }
    overlay.previousBitmap = SelectObject(overlay.memoryDc, overlay.bitmap);
    overlay.size = {width, height};
    return true;
}

void PutPixel(UINT32* pixels, int width, int height, int x, int y,
              BYTE red, BYTE green, BYTE blue, BYTE alpha = 255) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const BYTE premulRed = static_cast<BYTE>((red * alpha + 127) / 255);
    const BYTE premulGreen = static_cast<BYTE>((green * alpha + 127) / 255);
    const BYTE premulBlue = static_cast<BYTE>((blue * alpha + 127) / 255);
    pixels[y * width + x] = (static_cast<UINT32>(alpha) << 24) |
                            (static_cast<UINT32>(premulRed) << 16) |
                            (static_cast<UINT32>(premulGreen) << 8) |
                            premulBlue;
}

void RenderOverlayPixels(OverlayWindow& overlay) {
    if (!overlay.pixels || overlay.size.cx <= 0 || overlay.size.cy <= 0) return;
    const int width = overlay.size.cx;
    const int height = overlay.size.cy;
    auto* pixels = static_cast<UINT32*>(overlay.pixels);
    std::fill(pixels, pixels + static_cast<size_t>(width) * height, 0u);

    // Labels go through GDI, which writes nothing into the alpha channel: in a
    // layered window every pixel it touches would stay fully transparent. The
    // badge is opaque, so the fix is to force alpha to 255 over the painted
    // rectangles in one pass, after GdiFlush has let the batched calls land.
    std::vector<RECT> painted;
    for (const Indicator& item : overlay.dots) {
        if (item.label.empty()) continue;
        EnsureLabelFont(overlay, item.cell.bottom - item.cell.top);
        if (!overlay.font) continue;
        HGDIOBJ previousFont = SelectObject(overlay.memoryDc, overlay.font);
        // Measure first so the badge hugs its text instead of filling the whole
        // column. A label too long for the cell is ellipsised by DrawText.
        SIZE extent{};
        GetTextExtentPoint32W(overlay.memoryDc, item.label.c_str(),
                              static_cast<int>(item.label.size()), &extent);
        RECT badge = item.cell;
        badge.right = std::min(badge.right,
                               badge.left + extent.cx + 2 * kLabelTextPadding);
        HBRUSH brush = CreateSolidBrush(item.color);
        FillRect(overlay.memoryDc, &badge, brush);
        DeleteObject(brush);
        SetBkMode(overlay.memoryDc, TRANSPARENT);
        SetTextColor(overlay.memoryDc, colortags::TextColorFor(item.color));
        RECT text = badge;
        text.left += kLabelTextPadding;
        text.right -= kLabelTextPadding;
        DrawTextW(overlay.memoryDc, item.label.c_str(), -1, &text,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(overlay.memoryDc, previousFont);
        painted.push_back(badge);
    }
    if (!painted.empty()) {
        GdiFlush();
        const LONG limitX = static_cast<LONG>(width);
        const LONG limitY = static_cast<LONG>(height);
        for (const RECT& badge : painted) {
            const LONG top = std::clamp(badge.top, 0L, limitY);
            const LONG bottom = std::clamp(badge.bottom, top, limitY);
            const LONG left = std::clamp(badge.left, 0L, limitX);
            const LONG right = std::clamp(badge.right, left, limitX);
            for (LONG y = top; y < bottom; ++y) {
                for (LONG x = left; x < right; ++x) {
                    pixels[static_cast<size_t>(y) * width + x] |= 0xFF000000u;
                }
            }
        }
    }

    for (const Indicator& dot : overlay.dots) {
        if (!dot.label.empty()) continue;
        const RECT& circle = dot.circle;
        const double centerX = (circle.left + circle.right) / 2.0;
        const double centerY = (circle.top + circle.bottom) / 2.0;
        const double radius = (circle.right - circle.left) / 2.0;
        for (int y = circle.top; y < circle.bottom; ++y) {
            for (int x = circle.left; x < circle.right; ++x) {
                int inside = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const double dx = x + (sx + 0.5) / 4.0 - centerX;
                        const double dy = y + (sy + 0.5) / 4.0 - centerY;
                        if (dx * dx + dy * dy <= radius * radius) ++inside;
                    }
                }
                if (!inside) continue;
                const BYTE alpha = static_cast<BYTE>((inside * 255 + 8) / 16);
                PutPixel(pixels, width, height, x, y,
                         GetRValue(dot.color), GetGValue(dot.color),
                         GetBValue(dot.color), alpha);
            }
        }
    }
}

void RenderSnapshot(OverlayWindow& overlay, const WindowSnapshot& snapshot) {
    const int width = snapshot.overlayRect.right - snapshot.overlayRect.left;
    const int height = snapshot.overlayRect.bottom - snapshot.overlayRect.top;
    if (width <= 0 || height <= 0) return;
    if (!overlay.pixels || overlay.size.cx != width || overlay.size.cy != height) {
        if (!CreateSurface(overlay, width, height)) return;
    }
    overlay.dots = snapshot.dots;
    for (Indicator& dot : overlay.dots) {
        OffsetRect(&dot.erase, -snapshot.overlayRect.left, -snapshot.overlayRect.top);
        OffsetRect(&dot.circle, -snapshot.overlayRect.left, -snapshot.overlayRect.top);
        OffsetRect(&dot.cell, -snapshot.overlayRect.left, -snapshot.overlayRect.top);
    }
    RenderOverlayPixels(overlay);
}

void UpdateOverlayClip(OverlayWindow& overlay, const RECT& explorerRect,
                       const POINT& destination) {
    const LONG clipTop = std::clamp(
        explorerRect.top + overlay.contentTopOffset - destination.y,
        0L, overlay.size.cy);
    const LONG clipBottom = std::clamp(
        explorerRect.top + overlay.contentBottomOffset - destination.y,
        clipTop, overlay.size.cy);
    overlay.clipTop = clipTop;
    overlay.clipBottom = clipBottom;
    // A clip of zero height hides the whole layer. That is never what the
    // caller means - it means the content offsets did not describe this view -
    // so the window keeps its full shape and the indicators stay on screen,
    // rather than the layer silently disappearing.
    if (clipBottom <= clipTop) {
        overlay.clipped = false;
        SetWindowRgn(overlay.hwnd, nullptr, FALSE);
        return;
    }
    overlay.clipped = true;
    HRGN clip = CreateRectRgn(0, clipTop, overlay.size.cx, clipBottom);
    if (!SetWindowRgn(overlay.hwnd, clip, FALSE)) DeleteObject(clip);
}

// Whether the layer belongs on screen at all. It deliberately does not look at
// which application has focus: each overlay is an owned popup of its Explorer
// window, so the window manager already keeps it directly above that window and
// below anything the user stacks on top. Tying it to the foreground made every
// tag vanish the moment another window was clicked, and it was never the cause
// of the layer floating above everything — that was SW_SHOWNOACTIVATE being
// called on every scan pass.
bool OverlaysShouldBeVisible() { return true; }

// Put the layer immediately above the window it decorates.
//
// SetWindowPos's hWndInsertAfter names the window the positioned one goes
// AFTER, which in z-order means below it. Passing the Explorer window there
// therefore buried the layer behind the very window it belongs to: visible,
// full size, fully clipped in, and completely hidden. To end up above it, the
// layer has to be inserted after whatever currently sits above it instead.
void AnchorOverlayAbove(OverlayWindow& overlay) {
    if (!overlay.hwnd || !IsWindow(overlay.explorer)) return;
    HWND above = GetWindow(overlay.explorer, GW_HWNDPREV);
    if (above == overlay.hwnd) return;  // already directly above its owner
    SetWindowPos(overlay.hwnd, above ? above : HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void PresentOverlay(OverlayWindow& overlay) {
    // A cloaked window - one on another virtual desktop - still reports itself
    // as visible, and its indicators would be drawn onto the desktop the user
    // is actually looking at.
    if (!overlay.memoryDc || !overlay.explorer ||
        !IsWindowVisible(overlay.explorer) || IsIconic(overlay.explorer) ||
        IsCloaked(overlay.explorer) || !OverlaysShouldBeVisible()) {
        ShowWindow(overlay.hwnd, SW_HIDE);
        overlay.visible = false;
        return;
    }
    RECT explorerRect{};
    if (!GetWindowRect(overlay.explorer, &explorerRect)) return;
    POINT destination{explorerRect.left + overlay.offset.x,
                      explorerRect.top + overlay.offset.y};
    POINT source{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (UpdateLayeredWindow(overlay.hwnd, nullptr, &destination, &overlay.size,
                            overlay.memoryDc, &source, 0, &blend, ULW_ALPHA)) {
        UpdateOverlayClip(overlay, explorerRect, destination);
        // Only on the transition to visible. SW_SHOWNOACTIVATE raises the window
        // to the top of its z-order band, so calling it on every scan pass kept
        // lifting the layer back above whatever the user had switched to.
        // UpdateLayeredWindow has already repainted a window that is visible.
        if (!overlay.visible || !IsWindowVisible(overlay.hwnd)) {
            ShowWindow(overlay.hwnd, SW_SHOWNOACTIVATE);
            overlay.visible = true;
        }
        // Every pass, so the stacking cannot drift with history. Unlike
        // SW_SHOWNOACTIVATE this does not raise the layer to the top of the
        // band, so repeating it is safe.
        AnchorOverlayAbove(overlay);
    }
}

void MoveOverlay(OverlayWindow& overlay) {
    if (!overlay.visible || !overlay.hwnd || !overlay.explorer) return;
    RECT explorerRect{};
    if (!GetWindowRect(overlay.explorer, &explorerRect)) return;
    const POINT destination{explorerRect.left + overlay.offset.x,
                            explorerRect.top + overlay.offset.y};
    SetWindowPos(overlay.hwnd, nullptr, destination.x, destination.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    UpdateOverlayClip(overlay, explorerRect, destination);
}


void RemoveOverlay(HWND explorer) {
    const auto found = g_overlays.find(explorer);
    if (found == g_overlays.end()) return;
    if (found->second.font) DeleteObject(found->second.font);
    DestroySurface(found->second);
    if (found->second.hwnd) DestroyWindow(found->second.hwnd);
    g_overlays.erase(found);
}

void ApplyScan(std::unique_ptr<ScanResult> result) {
    std::vector<HWND> seen;
    for (const WindowSnapshot& snapshot : result->windows) {
        seen.push_back(snapshot.explorer);
        auto found = g_overlays.find(snapshot.explorer);
        if (found == g_overlays.end()) {
            OverlayWindow overlay;
            overlay.explorer = snapshot.explorer;
            overlay.hwnd = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                kOverlayClass, L"", WS_POPUP, 0, 0, 0, 0, snapshot.explorer,
                nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!overlay.hwnd) continue;
            found = g_overlays.emplace(snapshot.explorer, std::move(overlay)).first;
        }
        OverlayWindow& overlay = found->second;
        overlay.offset = {snapshot.overlayRect.left - snapshot.explorerRect.left,
                          snapshot.overlayRect.top - snapshot.explorerRect.top};
        overlay.rowHeight = snapshot.rowHeight;
        overlay.contentTopOffset = snapshot.contentTopOffset;
        overlay.contentBottomOffset = snapshot.contentBottomOffset;
        overlay.verticalScrollPercent = snapshot.verticalScrollPercent;
        overlay.verticalViewSize = snapshot.verticalViewSize;
        overlay.lastUpdate = GetTickCount64();
        RenderSnapshot(overlay, snapshot);
        PresentOverlay(overlay);
    }
    // An overlay goes away when its window is gone, or when a successful scan
    // found no tagged row in it. A window missing from this pass for any other
    // reason - a UI Automation hiccup, Explorer busy - keeps what it has, which
    // is what stops the indicators from blinking out and back.
    const ULONGLONG now = GetTickCount64();
    std::vector<HWND> stale;
    for (auto& [explorer, overlay] : g_overlays) {
        if (std::find(seen.begin(), seen.end(), explorer) != seen.end()) continue;
        const bool wasEmpty = std::find(result->emptyWindows.begin(),
                                        result->emptyWindows.end(),
                                        explorer) != result->emptyWindows.end();
        if (wasEmpty || !IsWindow(explorer)) {
            stale.push_back(explorer);
            continue;
        }
        // Missing from one pass is a hiccup and the indicators stay. Missing
        // for longer is not, and a layer left standing would describe rows that
        // have moved or been replaced, so it comes off the screen.
        if (overlay.visible && now - overlay.lastUpdate > kStaleOverlayMilliseconds) {
            ShowWindow(overlay.hwnd, SW_HIDE);
            overlay.visible = false;
        }
    }
    for (HWND explorer : stale) RemoveOverlay(explorer);
}

void RepositionAll() {
    for (auto& [explorer, overlay] : g_overlays) PresentOverlay(overlay);
}

// Called when the foreground application changes. The layer stays on screen;
// what it needs is to sit immediately above the window it belongs to, so that
// raising Explorer brings its tags with it and raising anything else covers
// them. SWP_NOACTIVATE keeps the focus where the user put it.
void UpdateOverlayVisibility() {
    for (auto& [explorer, overlay] : g_overlays) {
        if (!overlay.visible) continue;
        AnchorOverlayAbove(overlay);
    }
}

void HideOverlayFor(HWND explorer) {
    const auto found = g_overlays.find(explorer);
    if (found == g_overlays.end()) return;
    ShowWindow(found->second.hwnd, SW_HIDE);
    found->second.visible = false;
}

void HandleWheelScroll(HWND explorer, short wheelDelta) {
    const auto found = g_overlays.find(explorer);
    if (found == g_overlays.end() || found->second.rowHeight <= 0.0) return;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    const double viewportHeight = std::max(
        1L, found->second.contentBottomOffset - found->second.contentTopOffset);
    const double lineCount = lines == WHEEL_PAGESCROLL
        ? std::max(1.0, viewportHeight / found->second.rowHeight)
        : static_cast<double>(lines);
    double pixels = (static_cast<double>(wheelDelta) / WHEEL_DELTA) *
                    lineCount * found->second.rowHeight;
    if (found->second.verticalScrollPercent >= 0.0 &&
        found->second.verticalViewSize > 0.0) {
        if (found->second.verticalViewSize >= 100.0) {
            pixels = 0.0;
        } else {
            const double scrollRange = viewportHeight *
                (100.0 - found->second.verticalViewSize) /
                found->second.verticalViewSize;
            const double currentOffset = scrollRange *
                found->second.verticalScrollPercent / 100.0;
            const double requestedOffset = currentOffset - pixels;
            const double clampedOffset = std::clamp(requestedOffset, 0.0, scrollRange);
            pixels = currentOffset - clampedOffset;
            found->second.verticalScrollPercent = scrollRange > 0.0
                ? clampedOffset * 100.0 / scrollRange
                : 0.0;
        }
    }
    if (std::abs(pixels) > 0.5) {
        // In follow mode the layer stays up and the fast passes move it with
        // the rows; the settle timer still asks for one exact pass at the end,
        // which corrects whatever the fast passes approximated.
        if (!g_followScroll) HideOverlayFor(explorer);
        SetTimer(g_manager, kWheelSettleTimer, 90, nullptr);
    }
}

// Paths are full of backslashes and the status file is JSON.
std::wstring JsonEscape(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size() + 16);
    for (wchar_t ch : value) {
        if (ch == L'\r' || ch == L'\n') { out.push_back(L' '); continue; }
        if (ch == L'\\' || ch == L'"') out.push_back(L'\\');
        out.push_back(ch);
    }
    return out;
}

void WriteStatus(const ScanResult& result) {
    if (g_options.statusPath.empty()) return;
    size_t dotCount = 0;
    for (const WindowSnapshot& window : result.windows) dotCount += window.dots.size();
    const double scrollPercent = result.windows.empty()
        ? -1.0 : result.windows.front().verticalScrollPercent;
    const double rowPitch = result.windows.empty()
        ? 0.0 : result.windows.front().rowHeight;
    const std::wstring json = L"{\"windows\":" + std::to_wstring(result.windows.size()) +
                              L",\"dots\":" + std::to_wstring(dotCount) +
                              L",\"scrollPercent\":" + std::to_wstring(scrollPercent) +
                              L",\"rowPitch\":" + std::to_wstring(rowPitch) +
                              L",\"contentEvents\":" +
                              std::to_wstring(g_contentEventCount.load()) +
                              L",\"fastScans\":" +
                              std::to_wstring(g_fastScanCount.load()) +
                              L",\"wheelEvents\":" +
                              std::to_wstring(g_wheelEventCount.load()) +
                              L",\"explorerWindows\":" +
                              std::to_wstring(g_diagExplorerWindows.load()) +
                              L",\"overlays\":\"" + JsonEscape([] {
                                  std::wstring detail;
                                  for (const auto& [explorer, overlay] : g_overlays) {
                                      detail +=
                                          L"[owner=" +
                                          std::to_wstring(
                                              reinterpret_cast<uintptr_t>(explorer)) +
                                          L" flagged=" +
                                          (overlay.visible ? L"yes" : L"no") +
                                          L" onscreen=" +
                                          (overlay.hwnd && IsWindowVisible(overlay.hwnd)
                                               ? L"yes" : L"no") +
                                          L" ownerVisible=" +
                                          (IsWindowVisible(explorer) ? L"yes" : L"no") +
                                          L" ownerIconic=" +
                                          (IsIconic(explorer) ? L"yes" : L"no") +
                                          L" size=" +
                                          std::to_wstring(overlay.size.cx) + L"x" +
                                          std::to_wstring(overlay.size.cy) +
                                          L" clip=" +
                                          std::to_wstring(overlay.clipTop) + L".." +
                                          std::to_wstring(overlay.clipBottom) +
                                          (overlay.clipped ? L"" : L"(none)") +
                                          L" content=" +
                                          std::to_wstring(overlay.contentTopOffset) +
                                          L".." +
                                          std::to_wstring(overlay.contentBottomOffset) +
                                          L" dots=" +
                                          std::to_wstring(overlay.dots.size()) + L"] ";
                                  }
                                  return detail;
                              }()) + L"\"" +
                              L",\"windowDetail\":\"" +
                              JsonEscape([] {
                                  std::lock_guard<std::mutex> lock(g_diagWindowsMutex);
                                  return g_diagWindowsDetail;
                              }()) + L"\"" +
                              L",\"columnFound\":" +
                              std::to_wstring(g_diagColumnFound.load()) +
                              L",\"rowsSeen\":" +
                              std::to_wstring(g_diagRowsSeen.load()) +
                              L",\"taggedRows\":" +
                              std::to_wstring(g_diagTaggedRows.load()) +
                              L",\"folderMismatch\":" +
                              std::to_wstring(g_diagFolderMismatch.load()) +
                              L",\"columnName\":\"" + g_options.columnName +
                              L"\",\"tabItems\":\"" + JsonEscape(g_diagTabStrip) +
                              L"\",\"tabs\":\"" + JsonEscape(g_diagTabs) +
                              L"\",\"mode\":\"" +
                              (g_diagLabelMode.load() ? std::wstring(L"label")
                                                      : std::wstring(L"dot")) +
                              L"\"}";
    const std::wstring temporary = g_options.statusPath + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const BYTE bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    WriteFile(file, bom, sizeof(bom), &written, nullptr);
    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, json.c_str(),
                                               static_cast<int>(json.size()),
                                               nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, 0, json.c_str(), static_cast<int>(json.size()),
                        utf8.data(), byteCount, nullptr, nullptr);
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
    MoveFileExW(temporary.c_str(), g_options.statusPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void CALLBACK WinEventCallback(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG objectId, LONG, DWORD, DWORD) {
    if (!g_manager) return;
    if (hwnd) {
        HWND root = GetAncestor(hwnd, GA_ROOT);
        wchar_t className[64]{};
        GetClassNameW(root, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, L"CabinetWClass") != 0 &&
            wcscmp(className, L"ExploreWClass") != 0) return;
        if (event == EVENT_SYSTEM_SCROLLINGSTART) {
            ExtendScrollTracking(250);
            if (!g_followScroll && GetTickCount64() >= g_wheelTrackingUntil.load()) {
                PostMessageW(g_manager, kHideForContentChange,
                             reinterpret_cast<WPARAM>(root), 0);
            }
        } else if (event == EVENT_SYSTEM_SCROLLINGEND) {
            ExtendScrollTracking(150);
        } else if (event != EVENT_OBJECT_LOCATIONCHANGE || hwnd != root ||
                   objectId != OBJID_WINDOW) {
            ExtendScrollTracking(150);
        }
        if (event == EVENT_OBJECT_LOCATIONCHANGE && objectId == OBJID_WINDOW &&
            hwnd == root) {
            if (!g_locationMessagePending.exchange(true)) {
                PostMessageW(g_manager, kLocationChanged, 0, 0);
            }
        } else if (g_contentEvent) {
            ++g_contentEventCount;
            // Bumping the generation discards the scan that is in flight, and
            // that is only correct when the rows have moved under it. Explorer
            // also emits accessibility events for hover, focus, and an item
            // redrawn after SHChangeNotify - none of which move a row. Counting
            // those as staleness threw away one finished scan after another, so
            // a freshly assigned tag took several passes to appear and a layer
            // hidden for a scroll could stay hidden.
            const bool rowsMoved = event == EVENT_SYSTEM_SCROLLINGSTART ||
                                   event == EVENT_SYSTEM_SCROLLINGEND ||
                                   event == EVENT_OBJECT_VALUECHANGE;
            // Following the rows means accepting a pass that started a moment
            // before they moved: it re-reads their live positions anyway, so it
            // is a few milliseconds behind rather than wrong. Discarding it
            // would leave the layer frozen for the whole of a long scroll.
            // Waking the worker is also reserved for a real move: hover
            // events arrive in bursts, and turning each one into a scan would
            // put load on Explorer for no visible change. A newly assigned tag
            // is picked up by the next full refresh instead.
            if (rowsMoved) {
                if (!g_followScroll) ++g_contentGeneration;
                SetEvent(g_contentEvent);
            }
        }
    }
}

// WinEventCallback returns early for anything that is not an Explorer window,
// and a foreground switch is reported for the window taking over, which is
// usually another application. So this event needs a callback of its own.
void CALLBACK ForegroundEventCallback(HWINEVENTHOOK, DWORD, HWND, LONG, LONG,
                                      DWORD, DWORD) {
    if (g_manager) PostMessageW(g_manager, kForegroundChanged, 0, 0);
}

// A tab switch shows up as a selection change in the tab strip, and a
// navigation renames the window itself. Either means a different folder is on
// screen, so the shell data is re-read at once instead of at the end of the
// refresh interval, and the layer comes off until it has been.
//
// Both events are far too broad taken raw. Explorer renames objects constantly
// - on hover, on selection, whenever an item is refreshed - and a selection
// change fires every time the user clicks a file. Hiding the layer on all of
// them made it blink on any movement at all, which is worse than the delay it
// was meant to remove. So each is filtered down to the one case that means the
// folder changed:
//
//   NAMECHANGE   only the top-level window renaming itself, which is what a
//                navigation does. An item renaming itself is not that.
//   SELECTION    only from the title bar band, where the tab strip lives. A
//                selection in the file list is not a tab switch.
void CALLBACK NavigationEventCallback(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                      LONG objectId, LONG, DWORD, DWORD) {
    if (!g_manager || !hwnd) return;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    wchar_t className[64]{};
    GetClassNameW(root, className, static_cast<int>(std::size(className)));
    if (wcscmp(className, L"CabinetWClass") != 0 &&
        wcscmp(className, L"ExploreWClass") != 0) return;

    if (event == EVENT_OBJECT_NAMECHANGE) {
        if (hwnd != root || objectId != OBJID_WINDOW) return;
    } else {
        RECT rootRect{};
        RECT sourceRect{};
        if (!GetWindowRect(root, &rootRect) || !GetWindowRect(hwnd, &sourceRect)) return;
        if (sourceRect.top - rootRect.top >= kTabStripHeight) return;
    }

    g_forceShellRefresh = true;
    ++g_contentEventCount;
    ++g_contentGeneration;
    PostMessageW(g_manager, kHideForContentChange, reinterpret_cast<WPARAM>(root), 0);
    if (g_contentEvent) SetEvent(g_contentEvent);
}

LRESULT CALLBACK LowLevelMouseCallback(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && g_contentEvent) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        HWND target = WindowFromPoint(mouse->pt);
        wchar_t className[64]{};
        GetClassNameW(target, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, kOverlayClass) == 0) target = GetWindow(target, GW_OWNER);
        HWND root = GetAncestor(target, GA_ROOT);
        GetClassNameW(root, className, static_cast<int>(std::size(className)));
        const bool explorerTarget = wcscmp(className, L"CabinetWClass") == 0 ||
                                    wcscmp(className, L"ExploreWClass") == 0;
        if ((wParam == WM_MOUSEWHEEL || wParam == WM_MOUSEHWHEEL) && explorerTarget) {
            ExtendScrollTracking(180);
            const short delta = static_cast<short>(HIWORD(mouse->mouseData));
            g_wheelTrackingUntil = GetTickCount64() + (g_followScroll ? 0 : 120);
            ++g_wheelEventCount;
            ++g_contentEventCount;
            if (!g_followScroll) ++g_contentGeneration;
            if (wParam == WM_MOUSEWHEEL) {
                HandleWheelScroll(root, delta);
            }
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardCallback(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) &&
        g_contentEvent) {
        const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (keyboard->vkCode == VK_PRIOR || keyboard->vkCode == VK_NEXT ||
            keyboard->vkCode == VK_HOME || keyboard->vkCode == VK_END) {
            HWND root = GetAncestor(GetForegroundWindow(), GA_ROOT);
            wchar_t className[64]{};
            GetClassNameW(root, className, static_cast<int>(std::size(className)));
            if (wcscmp(className, L"CabinetWClass") == 0 ||
                wcscmp(className, L"ExploreWClass") == 0) {
                PostMessageW(g_manager, kHideForContentChange,
                             reinterpret_cast<WPARAM>(root), 0);
                ExtendScrollTracking(300);
                ++g_contentEventCount;
                ++g_contentGeneration;
                SetEvent(g_contentEvent);
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

LRESULT CALLBACK ManagerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kScanReady: {
            std::unique_ptr<ScanResult> result(reinterpret_cast<ScanResult*>(lParam));
            if (result->fullRefresh || g_options.diagnosticFastStatus) WriteStatus(*result);
            if (result->contentGeneration == g_contentGeneration.load() &&
                GetTickCount64() >= g_wheelTrackingUntil.load()) {
                ApplyScan(std::move(result));
            }
            g_scanInFlight = false;
            SetEvent(g_scanConsumedEvent);
            const ULONGLONG now = GetTickCount64();
            if (now < g_scrollTrackingUntil.load() &&
                now >= g_wheelTrackingUntil.load()) {
                SetEvent(g_contentEvent);
            }
            return 0;
        }
        case kForegroundChanged:
            UpdateOverlayVisibility();
            return 0;
        case kHideForContentChange:
            HideOverlayFor(reinterpret_cast<HWND>(wParam));
            return 0;
        case kLocationChanged:
            g_locationMessagePending = false;
            RepositionAll();
            return 0;
        case WM_TIMER:
            if (wParam == kExitTimer) DestroyWindow(hwnd);
            else if (wParam == kStopPollTimer &&
                      WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
                DestroyWindow(hwnd);
            } else if (wParam == kWheelSettleTimer) {
                KillTimer(hwnd, kWheelSettleTimer);
                g_wheelTrackingUntil = 0;
                SetEvent(g_contentEvent);
            }
            return 0;
        case WM_DESTROY:
            g_manager = nullptr;
            if (g_settings) {
                DestroyWindow(g_settings);
                g_settings = nullptr;
            }
            if (g_tray) {
                DestroyWindow(g_tray);
                g_tray = nullptr;
            }
            if (g_stopEvent) SetEvent(g_stopEvent);
            for (auto& [explorer, overlay] : g_overlays) {
                DestroySurface(overlay);
                if (overlay.hwnd) DestroyWindow(overlay.hwnd);
            }
            g_overlays.clear();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// A sixteen-pixel square quartered into four of the tag colours. Drawn here
// rather than shipped as a resource so the helper stays a single file with no
// build-time assets of its own.
HICON MakeTrayIcon() {
    constexpr int size = 16;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header),
                                     DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    auto* pixels = static_cast<UINT32*>(bits);
    const colortags::TagInfo* tags = colortags::AllTags();
    const size_t quadrant[4] = {0, 3, 4, 1};  // red, green, blue, orange
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int which = (y < size / 2 ? 0 : 2) + (x < size / 2 ? 0 : 1);
            const COLORREF value = tags[quadrant[which]].color;
            pixels[y * size + x] = 0xFF000000u |
                                   (static_cast<UINT32>(GetRValue(value)) << 16) |
                                   (static_cast<UINT32>(GetGValue(value)) << 8) |
                                   GetBValue(value);
        }
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}

void AddTrayIcon(HWND owner) {
    if (g_trayAdded) return;
    // The application icon as it was drawn, at the size the tray asks for; the
    // four-quadrant icon below is only the fallback for a binary built without
    // its resource.
    g_trayIcon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                   LR_DEFAULTCOLOR));
    if (!g_trayIcon) g_trayIcon = MakeTrayIcon();
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kTrayIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = g_trayIcon ? g_trayIcon : LoadIconW(nullptr, IDI_APPLICATION);
    const std::wstring tip = std::wstring(L"ColorTags ") + kVersionText;
    lstrcpynW(data.szTip, tip.c_str(), static_cast<int>(std::size(data.szTip)));
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void RemoveTrayIcon(HWND owner) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_trayAdded = false;
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
}

void ShowTrayMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    // A disabled first line: the menu is the only place some people will look to
    // find out what is running and which version it is.
    const std::wstring heading = std::wstring(L"ColorTags ") + kVersionText;
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, heading.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandSettings, L"Settings\u2026");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Quit ColorTags");
    SetMenuDefaultItem(menu, kCommandSettings, FALSE);

    POINT cursor{};
    GetCursorPos(&cursor);
    // Without this the menu does not close when the user clicks elsewhere.
    SetForegroundWindow(owner);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// Explorer asks a property handler for an item's column value once and keeps
// the answer, so switching the display mode left the old text in the Tags
// column: items shown before the switch kept their label, while anything tagged
// afterwards came back blank. This is the F5 the user would otherwise have to
// press, applied to every open view and tab.
void RefreshExplorerViews() {
    IShellWindows* rawWindows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&rawWindows))) ||
        !rawWindows) {
        return;
    }
    ComPtr<IShellWindows> windows(rawWindows);
    long count = 0;
    if (FAILED(windows->get_Count(&count))) return;
    for (long index = 0; index < count; ++index) {
        VARIANT itemIndex{};
        VariantInit(&itemIndex);
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;
        IDispatch* rawDispatch = nullptr;
        if (FAILED(windows->Item(itemIndex, &rawDispatch)) || !rawDispatch) continue;
        ComPtr<IDispatch> dispatch(rawDispatch);
        IServiceProvider* rawProvider = nullptr;
        if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&rawProvider))) ||
            !rawProvider) {
            continue;
        }
        ComPtr<IServiceProvider> provider(rawProvider);
        IShellBrowser* rawBrowser = nullptr;
        if (FAILED(provider->QueryService(SID_STopLevelBrowser,
                                          IID_PPV_ARGS(&rawBrowser))) ||
            !rawBrowser) {
            continue;
        }
        ComPtr<IShellBrowser> browser(rawBrowser);
        IShellView* rawView = nullptr;
        if (FAILED(browser->QueryActiveShellView(&rawView)) || !rawView) continue;
        ComPtr<IShellView> view(rawView);
        view->Refresh();
    }
}

// Applied after a settings change: the worker re-reads the registry on its
// next pass, so all that is needed is to wake it and invalidate what is drawn.
void ApplySettingsChange() {
    RefreshExplorerViews();
    g_forceShellRefresh = true;
    ++g_contentGeneration;
    if (g_contentEvent) SetEvent(g_contentEvent);
}

// ---------------------------------------------------------------------------
// Settings window. Dark, card-based, and painted by hand: the only real
// controls are the seven name fields and the buttons, everything else is drawn
// in WM_PAINT. Common Controls cannot be recoloured far enough for this look,
// and a dialog resource would give the helper a resource script it does not
// otherwise need.
// ---------------------------------------------------------------------------

constexpr COLORREF kBackColor = RGB(30, 30, 30);
constexpr COLORREF kCardColor = RGB(42, 42, 42);
constexpr COLORREF kCardEdge = RGB(58, 58, 58);
constexpr COLORREF kFieldColor = RGB(51, 51, 51);
constexpr COLORREF kTextColor = RGB(242, 242, 242);
constexpr COLORREF kMutedColor = RGB(154, 154, 154);
constexpr COLORREF kAccentColor = RGB(118, 118, 118);
constexpr COLORREF kAccentTextColor = RGB(246, 246, 246);
constexpr COLORREF kTrackColor = RGB(74, 74, 74);
constexpr COLORREF kKnobColor = RGB(240, 240, 240);   // knob on a lit track
constexpr COLORREF kKnobOffColor = RGB(150, 150, 150);

// ---------------------------------------------------------------------------
// Update check. One request to the GitHub releases API, made only when the
// settings window is open or its button is pressed - never in the background
// while the overlay is just running.
// ---------------------------------------------------------------------------

struct UpdateResult {
    bool reached = false;   // the API answered
    bool newer = false;     // and it names a version newer than this one
    std::wstring latest;
    DWORD error = 0;        // why it did not, so the window can say so
};

std::vector<int> ParseVersion(const std::wstring& text) {
    std::vector<int> parts;
    int current = 0;
    bool any = false;
    for (wchar_t c : text) {
        if (c >= L'0' && c <= L'9') {
            current = current * 10 + (c - L'0');
            any = true;
        } else if (c == L'.') {
            parts.push_back(any ? current : 0);
            current = 0;
            any = false;
        } else if (c == L'v' || c == L'V') {
            continue;  // the tag's leading "v"
        } else {
            break;  // a suffix such as -dev or -rc1 ends the number
        }
    }
    if (any) parts.push_back(current);
    return parts;
}

bool IsNewer(const std::wstring& candidate, const std::wstring& current) {
    const std::vector<int> left = ParseVersion(candidate);
    const std::vector<int> right = ParseVersion(current);
    if (left.empty()) return false;
    for (size_t index = 0; index < std::max(left.size(), right.size()); ++index) {
        const int a = index < left.size() ? left[index] : 0;
        const int b = index < right.size() ? right[index] : 0;
        if (a != b) return a > b;
    }
    return false;
}

bool HttpsGet(const wchar_t* host, const wchar_t* path, std::string& body,
              DWORD accessType, DWORD& error) {
    error = 0;
    HINTERNET session = WinHttpOpen(L"ColorTags-Explorer", accessType,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = GetLastError();
        return false;
    }
    // Short timeouts: this is a courtesy check, not something worth waiting on.
    WinHttpSetTimeouts(session, 4000, 4000, 6000, 6000);
    HINTERNET connection =
        WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        error = GetLastError();
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path, nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    bool ok = false;
    if (request) {
        // The GitHub API rejects requests without a user agent.
        const wchar_t* headers =
            L"User-Agent: ColorTags-Explorer\r\nAccept: application/vnd.github+json";
        WinHttpAddRequestHeaders(request, headers, static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD);
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            error = GetLastError();
        } else {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(request,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX);
            if (status != 200) {
                error = status;  // an HTTP status, not a Windows error
            } else {
                DWORD available = 0;
                while (WinHttpQueryDataAvailable(request, &available) && available) {
                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
                    body.append(chunk.data(), read);
                    if (body.size() > 1u << 20) break;  // the answer is ~2 KB
                }
                ok = !body.empty();
            }
        }
        WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// Pulls "tag_name" out of the release JSON. A full parser would be a dependency
// for one string in a document this program does not otherwise read.
std::wstring ParseTagName(const std::string& json) {
    const std::string key = "\"tag_name\"";
    const size_t keyAt = json.find(key);
    if (keyAt == std::string::npos) return {};
    const size_t colon = json.find(':', keyAt + key.size());
    if (colon == std::string::npos) return {};
    const size_t open = json.find('"', colon);
    if (open == std::string::npos) return {};
    const size_t close = json.find('"', open + 1);
    if (close == std::string::npos) return {};
    const std::string value = json.substr(open + 1, close - open - 1);
    std::wstring wide(value.begin(), value.end());  // tags are ASCII
    return wide;
}

DWORD WINAPI UpdateCheckThread(LPVOID parameter) {
    HWND target = reinterpret_cast<HWND>(parameter);
    auto* result = new UpdateResult();
    std::string json;
    DWORD error = 0;
    // The automatic path honours a PAC script or WPAD; the default one uses the
    // proxy configured for the system. Machines exist where only one works.
    bool fetched = HttpsGet(kApiHost, kApiPath, json, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            error);
    if (!fetched) {
        json.clear();
        DWORD fallbackError = 0;
        fetched = HttpsGet(kApiHost, kApiPath, json,
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, fallbackError);
        if (!fetched) error = fallbackError ? fallbackError : error;
    }
    result->error = fetched ? 0 : error;
    if (fetched) {
        const std::wstring tag = ParseTagName(json);
        if (!tag.empty()) {
            result->reached = true;
            result->latest = tag;
            result->newer = IsNewer(tag, kVersionText);
        }
    }
    if (!IsWindow(target) ||
        !PostMessageW(target, kUpdateReady, 0, reinterpret_cast<LPARAM>(result))) {
        delete result;
    }
    return 0;
}

void StartUpdateCheck(HWND window) {
    HANDLE thread = CreateThread(nullptr, 0, UpdateCheckThread, window, 0, nullptr);
    if (thread) CloseHandle(thread);
}

struct SettingsControls {
    // Which of our buttons the cursor is over, so the owner-drawn ones can show
    // a hover state: a child button's mouse messages never reach this window,
    // and a timer costs less than subclassing four controls.
    HWND hotButton = nullptr;
    HWND edits[colortags::kTagCount]{};
    HWND modeToggle = nullptr;    // on: show the tag's name, off: show a dot
    HWND scrollToggle = nullptr;  // on: hide while scrolling, off: follow rows
    HWND okButton = nullptr;
    HWND applyButton = nullptr;
    HWND resetButton = nullptr;
    HWND cancelButton = nullptr;
    HWND checkButton = nullptr;
    HWND releasesButton = nullptr;
    std::wstring updateStatus;
    bool updateAvailable = false;
    bool modeOn = false;
    bool scrollOn = true;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT smallFont = nullptr;
    HFONT iconFont = nullptr;
    HBRUSH fieldBrush = nullptr;
    HBRUSH backBrush = nullptr;
    int dpi = 96;
    // Card rectangles, filled in during layout and reused by WM_PAINT so the
    // painting code never recomputes geometry the controls were placed with.
    RECT namesCard{};
    RECT behaviorCard{};
    RECT aboutCard{};
    int aboutTextRight = 0;
    int rowTop = 0;
    int rowHeight = 0;
    int fieldLeft = 0;
    int fieldWidth = 0;
    int fieldHeight = 0;
    int fieldRadius = 0;
    int swatchLeft = 0;
    int labelLeft = 0;
};

int ScaleFor(int value, int dpi) { return MulDiv(value, dpi, 96); }

HFONT MakeFont(int pointSize, int weight, int dpi, const wchar_t* face) {
    return CreateFontW(-MulDiv(pointSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

enum class Shape { RoundRect, Ellipse };

// GDI has no antialiasing, and the pill and circles in this window are small
// enough that the stepped edges are the first thing the eye lands on. Each
// shape is therefore drawn several times larger into an offscreen bitmap and
// averaged down, which is what a hardware renderer would do for us.
int SupersampleFactor(int width, int height) {
    return (width * height > 40000) ? 2 : 4;
}

struct Supersampler {
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    DWORD* pixels = nullptr;
    int width = 0;
    int height = 0;
    int factor = 1;

    bool Begin(HDC dc, int targetWidth, int targetHeight, COLORREF back) {
        factor = SupersampleFactor(targetWidth, targetHeight);
        width = targetWidth * factor;
        height = targetHeight * factor;
        memory = CreateCompatibleDC(dc);
        if (!memory) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;  // top-down, so rows map directly
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) {
            if (bitmap) DeleteObject(bitmap);
            DeleteDC(memory);
            memory = nullptr;
            bitmap = nullptr;
            return false;
        }
        pixels = static_cast<DWORD*>(bits);
        oldBitmap = SelectObject(memory, bitmap);
        RECT full{0, 0, width, height};
        HBRUSH brush = CreateSolidBrush(back);
        FillRect(memory, &full, brush);
        DeleteObject(brush);
        return true;
    }

    void Fill(Shape shape, RECT box, COLORREF fill, COLORREF edge, int radius) {
        box.left *= factor;
        box.top *= factor;
        box.right *= factor;
        box.bottom *= factor;
        // A geometric pen is centred on the path: without this inset half its
        // width falls outside the bitmap and the curve comes back with flat
        // sides, which is exactly what a circle must not have.
        const int half = std::max(1, factor / 2);
        box.left += half;
        box.top += half;
        box.right -= half;
        box.bottom -= half;
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, factor, edge);
        HGDIOBJ oldBrush = SelectObject(memory, brush);
        HGDIOBJ oldPen = SelectObject(memory, pen);
        if (shape == Shape::Ellipse) {
            Ellipse(memory, box.left, box.top, box.right, box.bottom);
        } else {
            RoundRect(memory, box.left, box.top, box.right, box.bottom,
                      radius * factor, radius * factor);
        }
        SelectObject(memory, oldBrush);
        SelectObject(memory, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    void End(HDC dc, int x, int y) {
        if (!memory) return;
        GdiFlush();
        const int outWidth = width / factor;
        const int outHeight = height / factor;
        std::vector<DWORD> resolved(static_cast<size_t>(outWidth) * outHeight);
        const int samples = factor * factor;
        for (int row = 0; row < outHeight; ++row) {
            for (int column = 0; column < outWidth; ++column) {
                unsigned int blue = 0, green = 0, red = 0;
                for (int sy = 0; sy < factor; ++sy) {
                    const DWORD* source =
                        pixels + static_cast<size_t>(row * factor + sy) * width +
                        column * factor;
                    for (int sx = 0; sx < factor; ++sx) {
                        const DWORD value = source[sx];
                        blue += value & 0xFF;
                        green += (value >> 8) & 0xFF;
                        red += (value >> 16) & 0xFF;
                    }
                }
                resolved[static_cast<size_t>(row) * outWidth + column] =
                    (red / samples << 16) | (green / samples << 8) | (blue / samples);
            }
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = outWidth;
        info.bmiHeader.biHeight = -outHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, x, y, outWidth, outHeight, 0, 0, 0, outHeight,
                          resolved.data(), &info, DIB_RGB_COLORS);
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        memory = nullptr;
        bitmap = nullptr;
        pixels = nullptr;
    }
};

void DrawSmoothShape(HDC dc, const RECT& box, COLORREF fill, COLORREF edge,
                     COLORREF back, Shape shape, int radius) {
    const int width = box.right - box.left;
    const int height = box.bottom - box.top;
    if (width <= 0 || height <= 0) return;
    Supersampler sampler;
    if (!sampler.Begin(dc, width, height, back)) return;
    sampler.Fill(shape, RECT{0, 0, width, height}, fill, edge, radius);
    sampler.End(dc, box.left, box.top);
}

// Hovering lifts a surface by a fixed step rather than by a ratio, so a dark
// button and a lit one move by the same visible amount.
COLORREF Lighten(COLORREF color, int step) {
    const int red = std::min(255, GetRValue(color) + step);
    const int green = std::min(255, GetGValue(color) + step);
    const int blue = std::min(255, GetBValue(color) + step);
    return RGB(red, green, blue);
}

void DrawCard(HDC dc, const RECT& box, int radius) {
    DrawSmoothShape(dc, box, kCardColor, kCardEdge, kBackColor, Shape::RoundRect,
                    radius);
}

void DrawLabel(HDC dc, const wchar_t* text, RECT box, HFONT font, COLORREF color,
               UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &box, format);
    SelectObject(dc, oldFont);
}

// Track and knob share one offscreen pass so the knob's edge is averaged
// against the track underneath it rather than against a flat fill.
void DrawToggle(HDC dc, const RECT& box, bool on) {
    const int width = box.right - box.left;
    const int height = box.bottom - box.top;
    if (width <= 0 || height <= 0) return;
    Supersampler sampler;
    if (!sampler.Begin(dc, width, height, kCardColor)) return;
    const COLORREF track = on ? kAccentColor : kTrackColor;
    sampler.Fill(Shape::RoundRect, RECT{0, 0, width, height}, track, track,
                 height);
    const int inset = std::max(2, height / 8);
    const int knob = height - inset * 2;
    const int left = on ? width - inset - knob : inset;
    const COLORREF knobColor = on ? kKnobColor : kKnobOffColor;
    sampler.Fill(Shape::Ellipse, RECT{left, inset, left + knob, inset + knob},
                 knobColor, knobColor, 0);
    sampler.End(dc, box.left, box.top);
}

void DrawPushButton(HDC dc, const RECT& box, const wchar_t* text, HFONT font,
                    bool accent, bool focused, bool hot, int radius, COLORREF back) {
    COLORREF fill = accent ? kAccentColor : kFieldColor;
    if (hot) fill = Lighten(fill, accent ? 26 : 22);
    const COLORREF edge = focused ? RGB(150, 150, 150) : (accent ? fill : kCardEdge);
    DrawSmoothShape(dc, box, fill, edge, back, Shape::RoundRect, radius);
    DrawLabel(dc, text, box, font, accent ? kAccentTextColor : kTextColor,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void FillSettingsFromRegistry(SettingsControls* controls) {
    if (!controls) return;
    const colortags::TagInfo* tags = colortags::AllTags();
    for (size_t index = 0; index < colortags::kTagCount; ++index) {
        if (controls->edits[index]) {
            SetWindowTextW(controls->edits[index],
                           colortags::LabelOf(tags[index].id).c_str());
        }
    }
    controls->modeOn =
        colortags::CurrentDisplayMode() == colortags::DisplayMode::Label;
    const std::wstring behavior = colortags::ToLower(colortags::TrimSpace(
        colortags::ReadRegString(colortags::kConfigKey, L"ScrollBehavior")));
    controls->scrollOn = behavior != L"follow";
    if (controls->modeToggle) InvalidateRect(controls->modeToggle, nullptr, TRUE);
    if (controls->scrollToggle) InvalidateRect(controls->scrollToggle, nullptr, TRUE);
}

void SaveSettings(SettingsControls* controls) {
    if (!controls) return;
    const colortags::TagInfo* tags = colortags::AllTags();
    for (size_t index = 0; index < colortags::kTagCount; ++index) {
        if (!controls->edits[index]) continue;
        wchar_t buffer[128]{};
        GetWindowTextW(controls->edits[index], buffer, 128);
        // An emptied field means "go back to the built-in name" rather than a
        // nameless tag, which would leave an empty badge on screen.
        std::wstring chosen = colortags::SanitizeLabel(buffer);
        if (chosen.empty()) chosen = tags[index].defaultLabel;
        colortags::SetLabel(tags[index].id, chosen);
    }
    colortags::SetDisplayMode(controls->modeOn ? colortags::DisplayMode::Label
                                               : colortags::DisplayMode::Dot);
    colortags::WriteRegString(colortags::kConfigKey, L"ScrollBehavior",
                              controls->scrollOn ? L"hide" : L"follow");
    ApplySettingsChange();
}

void PaintSettings(HWND hwnd, SettingsControls* controls) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, controls->backBrush);

    const int dpi = controls->dpi;
    auto scale = [dpi](int value) { return ScaleFor(value, dpi); };
    const int radius = scale(10);

    // Header
    RECT title{scale(20), scale(16), client.right - scale(20), scale(40)};
    const std::wstring heading = std::wstring(L"ColorTags ") + kVersionText;
    DrawLabel(dc, heading.c_str(), title, controls->titleFont, kTextColor,
              DT_LEFT | DT_SINGLELINE);
    RECT subtitle{scale(20), scale(40), client.right - scale(20), scale(60)};
    DrawLabel(dc, L"Colour tags for files and folders", subtitle,
             controls->smallFont, kMutedColor, DT_LEFT | DT_SINGLELINE);

    DrawCard(dc, controls->namesCard, radius);
    DrawCard(dc, controls->behaviorCard, radius);
    DrawCard(dc, controls->aboutCard, radius);

    const int iconLeft = controls->namesCard.left + scale(18);
    RECT namesIcon{iconLeft, controls->namesCard.top + scale(16),
                   iconLeft + scale(26), controls->namesCard.top + scale(42)};
    DrawLabel(dc, L"\uE790", namesIcon, controls->iconFont, kMutedColor,
             DT_LEFT | DT_SINGLELINE);
    RECT namesTitle{iconLeft, controls->namesCard.top + scale(44),
                    iconLeft + scale(120), controls->namesCard.top + scale(64)};
    DrawLabel(dc, L"Tag names", namesTitle, controls->bodyFont, kTextColor,
             DT_LEFT | DT_SINGLELINE);
    RECT namesHint{iconLeft, controls->namesCard.top + scale(64),
                   iconLeft + scale(130), controls->namesCard.top + scale(120)};
    DrawLabel(dc, L"Shown in the menu, the Tags column and the badge",
             namesHint, controls->smallFont, kMutedColor, DT_LEFT | DT_WORDBREAK);

    const colortags::TagInfo* tags = colortags::AllTags();
    for (size_t index = 0; index < colortags::kTagCount; ++index) {
        const int top = controls->rowTop + static_cast<int>(index) * controls->rowHeight;
        const int size = scale(14);
        const int centre = top + scale(13);
        RECT swatch{controls->swatchLeft, centre - size / 2,
                    controls->swatchLeft + size, centre + size / 2};
        DrawSmoothShape(dc, swatch, tags[index].color, tags[index].color,
                        kCardColor, Shape::Ellipse, 0);

        RECT field{controls->fieldLeft, top, controls->fieldLeft + controls->fieldWidth,
                   top + controls->fieldHeight};
        DrawSmoothShape(dc, field, kFieldColor, kFieldColor, kCardColor,
                        Shape::RoundRect, controls->fieldRadius);

        RECT name{controls->labelLeft, top, controls->labelLeft + scale(64),
                  top + scale(26)};
        DrawLabel(dc, tags[index].defaultLabel, name, controls->smallFont,
                 kMutedColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const int behaviorIcon = controls->behaviorCard.left + scale(18);
    RECT gearIcon{behaviorIcon, controls->behaviorCard.top + scale(16),
                  behaviorIcon + scale(26), controls->behaviorCard.top + scale(42)};
    DrawLabel(dc, L"\uE713", gearIcon, controls->iconFont, kMutedColor,
             DT_LEFT | DT_SINGLELINE);
    RECT behaviorTitle{behaviorIcon, controls->behaviorCard.top + scale(44),
                       behaviorIcon + scale(120),
                       controls->behaviorCard.top + scale(64)};
    DrawLabel(dc, L"Behaviour", behaviorTitle, controls->bodyFont, kTextColor,
             DT_LEFT | DT_SINGLELINE);

    // The two toggle captions sit to the left of their switches; the switches
    // themselves are owner-drawn buttons and paint in WM_DRAWITEM.
    struct Caption {
        const wchar_t* title;
        const wchar_t* hint;
        HWND toggle;
    } captions[] = {
        {L"Show the tag's name", L"Off shows a coloured dot instead",
         controls->modeToggle},
        {L"Hide while scrolling", L"Off keeps them following the rows",
         controls->scrollToggle},
    };
    for (const Caption& caption : captions) {
        if (!caption.toggle) continue;
        RECT toggleRect{};
        GetWindowRect(caption.toggle, &toggleRect);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&toggleRect), 2);
        RECT line{controls->behaviorCard.left + scale(140), toggleRect.top - scale(4),
                  toggleRect.left - scale(12), toggleRect.top + scale(12)};
        DrawLabel(dc, caption.title, line, controls->bodyFont, kTextColor,
                 DT_LEFT | DT_SINGLELINE);
        RECT hint{line.left, toggleRect.top + scale(12), line.right,
                  toggleRect.top + scale(30)};
        DrawLabel(dc, caption.hint, hint, controls->smallFont, kMutedColor,
                 DT_LEFT | DT_SINGLELINE);
    }

    const int aboutIcon = controls->aboutCard.left + scale(18);
    RECT infoIcon{aboutIcon, controls->aboutCard.top + scale(16),
                  aboutIcon + scale(26), controls->aboutCard.top + scale(42)};
    DrawLabel(dc, L"\uE946", infoIcon, controls->iconFont, kMutedColor,
              DT_LEFT | DT_SINGLELINE);
    RECT aboutTitle{aboutIcon, controls->aboutCard.top + scale(44),
                    aboutIcon + scale(120), controls->aboutCard.top + scale(64)};
    DrawLabel(dc, L"About", aboutTitle, controls->bodyFont, kTextColor,
              DT_LEFT | DT_SINGLELINE);

    const int aboutTextLeft = controls->aboutCard.left + scale(140);
    RECT versionLine{aboutTextLeft, controls->aboutCard.top + scale(22),
                     controls->aboutTextRight,
                     controls->aboutCard.top + scale(44)};
    DrawLabel(dc, (std::wstring(L"Version ") + kVersionText).c_str(), versionLine,
              controls->bodyFont, kTextColor, DT_LEFT | DT_SINGLELINE);
    RECT statusLine{aboutTextLeft, controls->aboutCard.top + scale(44),
                    controls->aboutTextRight,
                    controls->aboutCard.top + scale(68)};
    DrawLabel(dc, controls->updateStatus.c_str(), statusLine, controls->smallFont,
              controls->updateAvailable ? kTextColor : kMutedColor,
              DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    RECT hint{controls->behaviorCard.left + scale(4),
              controls->behaviorCard.bottom + scale(8),
              controls->behaviorCard.right,
              controls->behaviorCard.bottom + scale(28)};
    DrawLabel(dc, L"Renamed tags reach the Tags column after F5", hint,
              controls->smallFont, kMutedColor, DT_LEFT | DT_SINGLELINE);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam,
                              LPARAM lParam) {
    auto* controls =
        reinterpret_cast<SettingsControls*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (controls) {
                PaintSettings(hwnd, controls);
                return 0;
            }
            break;
        case WM_CTLCOLOREDIT: {
            if (!controls) break;
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kTextColor);
            SetBkColor(dc, kFieldColor);
            return reinterpret_cast<LRESULT>(controls->fieldBrush);
        }
        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!controls || !item) break;
            if (item->hwndItem == controls->modeToggle ||
                item->hwndItem == controls->scrollToggle) {
                HBRUSH cardBack = CreateSolidBrush(kCardColor);
                FillRect(item->hDC, &item->rcItem, cardBack);
                DeleteObject(cardBack);
                DrawToggle(item->hDC, item->rcItem,
                           item->hwndItem == controls->modeToggle
                               ? controls->modeOn
                               : controls->scrollOn);
                return TRUE;
            }
            const bool accent = item->hwndItem == controls->okButton;
            const COLORREF behind =
                item->hwndItem == controls->resetButton ? kCardColor : kBackColor;
            wchar_t text[64]{};
            GetWindowTextW(item->hwndItem, text, 64);
            HBRUSH back = CreateSolidBrush(behind);
            FillRect(item->hDC, &item->rcItem, back);
            DeleteObject(back);
            DrawPushButton(item->hDC, item->rcItem, text, controls->bodyFont,
                           accent, (item->itemState & ODS_FOCUS) != 0,
                           item->hwndItem == controls->hotButton,
                           ScaleFor(8, controls->dpi), behind);
            return TRUE;
        }
        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            if (!controls) break;
            if (id == kSettingsModeDot) {  // the display toggle
                controls->modeOn = !controls->modeOn;
                InvalidateRect(controls->modeToggle, nullptr, TRUE);
                return 0;
            }
            if (id == kSettingsScrollHide) {  // the scrolling toggle
                controls->scrollOn = !controls->scrollOn;
                InvalidateRect(controls->scrollToggle, nullptr, TRUE);
                return 0;
            }
            if (id == kSettingsSave || id == IDOK) {
                SaveSettings(controls);
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == kSettingsApply) {
                SaveSettings(controls);
                // Re-read so the fields show exactly what was stored, including
                // any name that came back as the built-in one.
                FillSettingsFromRegistry(controls);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == kSettingsReset) {
                // Only refills the fields; nothing is stored until Save, so a
                // mistaken click here is still recoverable with Cancel.
                const colortags::TagInfo* tags = colortags::AllTags();
                for (size_t index = 0; index < colortags::kTagCount; ++index) {
                    if (controls->edits[index]) {
                        SetWindowTextW(controls->edits[index],
                                       tags[index].defaultLabel);
                    }
                }
                return 0;
            }
            if (id == kSettingsCheckUpdates) {
                controls->updateStatus = L"Checking\u2026";
                controls->updateAvailable = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                StartUpdateCheck(hwnd);
                return 0;
            }
            if (id == kSettingsReleases) {
                ShellExecuteW(hwnd, L"open", kReleasesUrl, nullptr, nullptr,
                              SW_SHOWNORMAL);
                return 0;
            }
            if (id == kSettingsClose || id == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_TIMER: {
            if (wParam != kSettingsHoverTimer || !controls) break;
            POINT cursor{};
            GetCursorPos(&cursor);
            HWND under = WindowFromPoint(cursor);
            const HWND buttons[] = {controls->okButton, controls->applyButton,
                                    controls->cancelButton, controls->resetButton,
                                    controls->checkButton, controls->releasesButton};
            HWND hot = nullptr;
            for (HWND button : buttons) {
                if (button && button == under) hot = button;
            }
            if (hot != controls->hotButton) {
                const HWND previous = controls->hotButton;
                controls->hotButton = hot;
                if (previous) InvalidateRect(previous, nullptr, TRUE);
                if (hot) InvalidateRect(hot, nullptr, TRUE);
            }
            return 0;
        }
        case kUpdateReady: {
            std::unique_ptr<UpdateResult> update(
                reinterpret_cast<UpdateResult*>(lParam));
            if (!controls) return 0;
            if (!update->reached) {
                // 404 is what a private repository answers an anonymous
                // request, and what one with no releases answers too: nothing
                // is broken, there is simply nothing public to compare against.
                if (update->error == 404) {
                    controls->updateStatus = L"No public releases to check";
                } else {
                    // Otherwise the number tells no network from a refusal.
                    controls->updateStatus = L"Could not reach GitHub";
                    if (update->error) {
                        controls->updateStatus +=
                            L" (" + std::to_wstring(update->error) + L")";
                    }
                }
                controls->updateAvailable = false;
            } else if (update->newer) {
                controls->updateStatus = L"Update available: " + update->latest;
                controls->updateAvailable = true;
            } else {
                controls->updateStatus = L"You are up to date";
                controls->updateAvailable = false;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kSettingsHoverTimer);
            if (controls) {
                if (controls->titleFont) DeleteObject(controls->titleFont);
                if (controls->bodyFont) DeleteObject(controls->bodyFont);
                if (controls->smallFont) DeleteObject(controls->smallFont);
                if (controls->iconFont) DeleteObject(controls->iconFont);
                if (controls->fieldBrush) DeleteObject(controls->fieldBrush);
                if (controls->backBrush) DeleteObject(controls->backBrush);
                delete controls;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            if (g_settings == hwnd) g_settings = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Windows only lets the process that owns the foreground hand it over. The
// helper usually owns nothing on screen, so a plain SetForegroundWindow is
// ignored and the window opens behind everything — which looks exactly like the
// click doing nothing. Borrowing the current foreground thread's input queue
// for the length of the call is the documented way around it.
void ForceForeground(HWND window) {
    HWND foreground = GetForegroundWindow();
    const DWORD thisThread = GetCurrentThreadId();
    const DWORD otherThread =
        foreground ? GetWindowThreadProcessId(foreground, nullptr) : thisThread;
    const bool attached =
        otherThread != thisThread && AttachThreadInput(thisThread, otherThread, TRUE);
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(window);
    BringWindowToTop(window);
    SetActiveWindow(window);
    if (attached) AttachThreadInput(thisThread, otherThread, FALSE);
}

void ShowSettingsWindow() {
    if (g_settings && IsWindow(g_settings)) {
        ShowWindow(g_settings, SW_RESTORE);
        ForceForeground(g_settings);
        return;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSW settingsClass{};
        settingsClass.lpfnWndProc = SettingsProc;
        settingsClass.hInstance = instance;
        settingsClass.lpszClassName = kSettingsClass;
        settingsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        settingsClass.hIcon = static_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                       LR_DEFAULTSIZE | LR_SHARED));
        if (!RegisterClassW(&settingsClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        registered = true;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    HWND window = CreateWindowExW(0, kSettingsClass, L"ColorTags", style,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                                  nullptr, instance, nullptr);
    if (!window) return;

    // The caption follows the window: without this the frame stays light while
    // the client area is dark.
    const BOOL darkTitleBar = TRUE;
    DwmSetWindowAttribute(window, 20, &darkTitleBar, sizeof(darkTitleBar));

    auto* controls = new SettingsControls();
    controls->dpi = static_cast<int>(GetDpiForWindow(window));
    if (controls->dpi <= 0) controls->dpi = 96;
    const int dpi = controls->dpi;
    auto scale = [dpi](int value) { return ScaleFor(value, dpi); };

    controls->titleFont = MakeFont(14, 600, dpi, L"Segoe UI");
    controls->bodyFont = MakeFont(10, FW_NORMAL, dpi, L"Segoe UI");
    controls->smallFont = MakeFont(8, FW_NORMAL, dpi, L"Segoe UI");
    controls->iconFont = MakeFont(13, FW_NORMAL, dpi, L"Segoe MDL2 Assets");
    controls->fieldBrush = CreateSolidBrush(kFieldColor);
    controls->backBrush = CreateSolidBrush(kBackColor);

    const int clientWidth = scale(520);
    const int cardLeft = scale(20);
    const int cardRight = clientWidth - scale(20);
    const int rowHeight = scale(30);
    const int namesTop = scale(70);
    const int namesHeight = scale(20) + static_cast<int>(colortags::kTagCount) * rowHeight +
                            scale(14);
    controls->namesCard = RECT{cardLeft, namesTop, cardRight, namesTop + namesHeight};
    const int behaviorTop = namesTop + namesHeight + scale(12);
    const int behaviorHeight = scale(110);
    controls->behaviorCard =
        RECT{cardLeft, behaviorTop, cardRight, behaviorTop + behaviorHeight};
    // Room for the line about F5, which explains the cards above it.
    const int aboutTop = behaviorTop + behaviorHeight + scale(36);
    const int aboutHeight = scale(90);
    controls->aboutCard = RECT{cardLeft, aboutTop, cardRight, aboutTop + aboutHeight};
    const int clientHeight = aboutTop + aboutHeight + scale(78);

    controls->rowTop = namesTop + scale(16);
    controls->rowHeight = rowHeight;
    controls->swatchLeft = cardLeft + scale(150);
    controls->labelLeft = cardLeft + scale(172);

    RECT frame{0, 0, clientWidth, clientHeight};
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
    SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left,
                 frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER);

    controls->fieldLeft = cardLeft + scale(244);
    controls->fieldWidth = cardRight - scale(18) - controls->fieldLeft;
    controls->fieldHeight = scale(26);
    controls->fieldRadius = scale(6);

    // One line of text is all the edit control covers; the rounded field behind
    // it is painted by the window, so the text is centred by construction.
    HDC measure = GetDC(window);
    HGDIOBJ oldFont = SelectObject(measure, controls->bodyFont);
    TEXTMETRICW metrics{};
    GetTextMetricsW(measure, &metrics);
    SelectObject(measure, oldFont);
    ReleaseDC(window, measure);
    const int editHeight = metrics.tmHeight + scale(2);
    const int editInset = scale(10);

    const colortags::TagInfo* tags = colortags::AllTags();
    for (size_t index = 0; index < colortags::kTagCount; ++index) {
        const int top = controls->rowTop + static_cast<int>(index) * rowHeight;
        controls->edits[index] = CreateWindowExW(
            0, L"EDIT", colortags::LabelOf(tags[index].id).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            controls->fieldLeft + editInset,
            top + (controls->fieldHeight - editHeight) / 2,
            controls->fieldWidth - editInset * 2, editHeight, window,
            reinterpret_cast<HMENU>(
                static_cast<UINT_PTR>(kSettingsEditBase + index)),
            instance, nullptr);
    }

    const int toggleWidth = scale(44);
    const int toggleHeight = scale(24);
    const int toggleLeft = cardRight - scale(18) - toggleWidth;
    controls->modeToggle = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        toggleLeft, behaviorTop + scale(22), toggleWidth, toggleHeight, window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsModeDot)), instance,
        nullptr);
    controls->scrollToggle = CreateWindowExW(
        0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        toggleLeft, behaviorTop + scale(64), toggleWidth, toggleHeight, window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsScrollHide)),
        instance, nullptr);

    // Reset belongs with the names it resets, so it sits in the tag card
    // under that card's own heading rather than in the window's footer.
    controls->resetButton = CreateWindowExW(
        0, L"BUTTON", L"Reset names",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, cardLeft + scale(18),
        namesTop + scale(134), scale(112), scale(28), window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsReset)), instance,
        nullptr);

    // Stacked rather than side by side: two buttons in a row left the status
    // line too narrow for a sentence like "Could not reach GitHub".
    const int aboutButtonWidth = scale(110);
    const int aboutButtonHeight = scale(26);
    const int aboutButtonLeft = cardRight - scale(18) - aboutButtonWidth;
    controls->checkButton = CreateWindowExW(
        0, L"BUTTON", L"Check now", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        aboutButtonLeft, aboutTop + scale(16), aboutButtonWidth, aboutButtonHeight,
        window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsCheckUpdates)),
        instance, nullptr);
    controls->releasesButton = CreateWindowExW(
        0, L"BUTTON", L"Releases", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        aboutButtonLeft, aboutTop + scale(48), aboutButtonWidth, aboutButtonHeight,
        window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsReleases)), instance,
        nullptr);
    controls->aboutTextRight = aboutButtonLeft - scale(14);

    const int buttonTop = clientHeight - scale(46);
    const int buttonHeight = scale(32);
    const int buttonWidth = scale(88);
    const int buttonGap = scale(10);
    controls->okButton = CreateWindowExW(
        0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        cardRight - buttonWidth, buttonTop, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsSave)), instance,
        nullptr);
    controls->applyButton = CreateWindowExW(
        0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        cardRight - buttonWidth * 2 - buttonGap, buttonTop, buttonWidth,
        buttonHeight, window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsApply)), instance,
        nullptr);
    controls->cancelButton = CreateWindowExW(
        0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        cardRight - buttonWidth * 3 - buttonGap * 2, buttonTop, buttonWidth,
        buttonHeight, window,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsClose)), instance,
        nullptr);

    for (HWND edit : controls->edits) {
        if (!edit) continue;
        SendMessageW(edit, WM_SETFONT,
                     reinterpret_cast<WPARAM>(controls->bodyFont), TRUE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }

    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controls));
    FillSettingsFromRegistry(controls);
    g_settings = window;

    // Centre on the monitor the cursor is on, which is where the tray icon was
    // just clicked.
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const int windowWidth = bounds.right - bounds.left;
        const int windowHeight = bounds.bottom - bounds.top;
        const RECT& work = monitorInfo.rcWork;
        SetWindowPos(window, nullptr,
                     work.left + (work.right - work.left - windowWidth) / 2,
                     work.top + (work.bottom - work.top - windowHeight) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    }

    ShowWindow(window, SW_SHOW);
    ForceForeground(window);
    if (controls->edits[0]) SetFocus(controls->edits[0]);

    // Checked once per opening of the window, never while the overlay is merely
    // running: the program should not talk to the network on its own.
    controls->updateStatus = L"Checking\u2026";
    StartUpdateCheck(window);
    SetTimer(window, kSettingsHoverTimer, 80, nullptr);
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kTrayMessage:
            if (LOWORD(lParam) == WM_LBUTTONUP) {
                ShowSettingsWindow();
            } else if (LOWORD(lParam) == WM_RBUTTONUP ||
                       LOWORD(lParam) == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            if (id == kCommandSettings) {
                ShowSettingsWindow();
                return 0;
            }
            if (id == kCommandExit) {
                if (g_manager) DestroyWindow(g_manager);
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            RemoveTrayIcon(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ParseArguments() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return;
    for (int i = 1; i < count; ++i) {
        const std::wstring name = arguments[i];
        if (name == L"--target-folder" && i + 1 < count) g_options.targetFolder = arguments[++i];
        else if (name == L"--status" && i + 1 < count) g_options.statusPath = arguments[++i];
        else if (name == L"--column" && i + 1 < count) g_options.columnName = arguments[++i];
        else if (name == L"--dot-size" && i + 1 < count) g_options.dotSize = _wtoi(arguments[++i]);
        else if (name == L"--mode" && i + 1 < count) {
            const std::wstring value = arguments[++i];
            if (value == L"label") g_options.modeOverride = 1;
            else if (value == L"dot") g_options.modeOverride = 0;
        }
        else if (name == L"--refresh-ms" && i + 1 < count) g_options.refreshMilliseconds = _wtoi(arguments[++i]);
        else if (name == L"--duration-seconds" && i + 1 < count) g_options.durationSeconds = _wtoi(arguments[++i]);
        else if (name == L"--diagnostic-fast-status") g_options.diagnosticFastStatus = true;
    }
    LocalFree(arguments);
    g_options.dotSize = std::clamp(g_options.dotSize, 6, 32);
    g_options.refreshMilliseconds = std::clamp(g_options.refreshMilliseconds, 200, 5000);
    g_options.durationSeconds = std::max(0, g_options.durationSeconds);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ParseArguments();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;

    WNDCLASSW managerClass{};
    managerClass.lpfnWndProc = ManagerProc;
    managerClass.hInstance = instance;
    managerClass.lpszClassName = kManagerClass;
    if (!RegisterClassW(&managerClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 3;
    WNDCLASSW overlayClass{};
    overlayClass.lpfnWndProc = OverlayProc;
    overlayClass.hInstance = instance;
    overlayClass.lpszClassName = kOverlayClass;
    overlayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&overlayClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 4;

    g_manager = CreateWindowExW(0, kManagerClass, L"ColorTags native overlay manager",
                                0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_manager) return 5;

    // The tray icon needs an owner that can take the foreground, which a
    // message-only window cannot; without that a popup menu never closes.
    WNDCLASSW trayClass{};
    trayClass.lpfnWndProc = TrayProc;
    trayClass.hInstance = instance;
    trayClass.lpszClassName = kTrayClass;
    if (RegisterClassW(&trayClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        g_tray = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, L"ColorTags",
                                 WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
                                 nullptr);
        if (g_tray) AddTrayIcon(g_tray);
    }
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
    if (!g_stopEvent) return 6;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_stopEvent);
        CoUninitialize();
        return 9;
    }
    g_contentEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_contentEvent) {
        CloseHandle(g_stopEvent);
        CoUninitialize();
        return 10;
    }
    g_scanConsumedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_scanConsumedEvent) {
        CloseHandle(g_contentEvent);
        CloseHandle(g_stopEvent);
        CoUninitialize();
        return 11;
    }

    g_locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,
                                     EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                     WinEventCallback, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_scrollHook = SetWinEventHook(EVENT_SYSTEM_SCROLLINGSTART,
                                   EVENT_SYSTEM_SCROLLINGEND, nullptr,
                                   WinEventCallback, 0, 0,
                                   WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_valueHook = SetWinEventHook(EVENT_OBJECT_VALUECHANGE,
                                  EVENT_OBJECT_VALUECHANGE, nullptr,
                                  WinEventCallback, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                                      EVENT_SYSTEM_FOREGROUND, nullptr,
                                      ForegroundEventCallback, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_selectionHook = SetWinEventHook(EVENT_OBJECT_SELECTION,
                                      EVENT_OBJECT_SELECTIONWITHIN, nullptr,
                                      NavigationEventCallback, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_nameHook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE,
                                 EVENT_OBJECT_NAMECHANGE, nullptr,
                                 NavigationEventCallback, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseCallback,
                                    GetModuleHandleW(nullptr), 0);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardCallback,
                                       GetModuleHandleW(nullptr), 0);
    if (!g_locationHook || !g_scrollHook || !g_valueHook || !g_foregroundHook ||
        !g_selectionHook || !g_nameHook || !g_mouseHook || !g_keyboardHook) {
        if (g_locationHook) UnhookWinEvent(g_locationHook);
        if (g_scrollHook) UnhookWinEvent(g_scrollHook);
        if (g_valueHook) UnhookWinEvent(g_valueHook);
        if (g_foregroundHook) UnhookWinEvent(g_foregroundHook);
        if (g_selectionHook) UnhookWinEvent(g_selectionHook);
        if (g_nameHook) UnhookWinEvent(g_nameHook);
        if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
        if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
        CloseHandle(g_scanConsumedEvent);
        CloseHandle(g_contentEvent);
        CloseHandle(g_stopEvent);
        CoUninitialize();
        return 7;
    }

    g_worker = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    if (!g_worker) {
        UnhookWinEvent(g_locationHook);
        UnhookWinEvent(g_scrollHook);
        UnhookWinEvent(g_valueHook);
        UnhookWinEvent(g_foregroundHook);
    UnhookWinEvent(g_selectionHook);
    UnhookWinEvent(g_nameHook);
        UnhookWinEvent(g_selectionHook);
        UnhookWinEvent(g_nameHook);
        UnhookWindowsHookEx(g_mouseHook);
        UnhookWindowsHookEx(g_keyboardHook);
        CloseHandle(g_scanConsumedEvent);
        CloseHandle(g_contentEvent);
        CloseHandle(g_stopEvent);
        CoUninitialize();
        return 8;
    }
    if (g_options.durationSeconds > 0) {
        SetTimer(g_manager, kExitTimer,
                 static_cast<UINT>(g_options.durationSeconds * 1000), nullptr);
    }
    SetTimer(g_manager, kStopPollTimer, 250, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (g_settings && IsWindow(g_settings) &&
            IsDialogMessageW(g_settings, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    SetEvent(g_stopEvent);
    WaitForSingleObject(g_worker, 3000);
    CloseHandle(g_worker);
    UnhookWinEvent(g_locationHook);
    UnhookWinEvent(g_scrollHook);
    UnhookWinEvent(g_valueHook);
    UnhookWinEvent(g_foregroundHook);
    UnhookWindowsHookEx(g_mouseHook);
    UnhookWindowsHookEx(g_keyboardHook);
    CloseHandle(g_scanConsumedEvent);
    CloseHandle(g_contentEvent);
    CloseHandle(g_stopEvent);
    CoUninitialize();
    return 0;
}
