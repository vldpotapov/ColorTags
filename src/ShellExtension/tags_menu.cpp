// tags_menu.cpp — ColorTags Explorer context menu (Phase 4).
//
// IExplorerCommand "Tags" submenu: 7 colors + "Remove tag".
// Invoke shells out to the Python CLI (single source of truth for tag logic):
//   python -m src.Cli.colortag set-many <color> <paths...>
//   python -m src.Cli.colortag remove-many <paths...>
//
// Config (HKCU\Software\ColorTags, written by register.ps1):
//   PythonPath  REG_SZ  full path to python.exe
//   ProjectRoot REG_SZ  project root (cwd for the CLI)
//
// Output of the CLI is appended to %LOCALAPPDATA%\Colortags\menu.log.

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cwctype>

// The single visual contract: tag ids, colors, user labels, display mode.
#include "../Shared/ColorTagsConfig.h"

// Command CLSID — also used as the canonical name of the parent command.
static const CLSID CLSID_TagsMenu = {
    0x111B2250, 0x529E, 0x4194, {0xAE, 0xE7, 0x5D, 0xE3, 0xE7, 0xEA, 0xAC, 0x5C}};

// Unique canonical names for the 8 subcommands — the menu host keys commands
// by canonical name, so every command must return its own GUID.
static const CLSID CLSID_TagsRed = {
    0x6BE25266, 0xA15D, 0x49BF, {0xB5, 0x56, 0xAF, 0xA1, 0xB7, 0xE6, 0x49, 0xFB}};
static const CLSID CLSID_TagsOrange = {
    0x55006B72, 0x0130, 0x410D, {0xB1, 0x1F, 0x7B, 0x15, 0xFA, 0x57, 0xCF, 0x36}};
static const CLSID CLSID_TagsYellow = {
    0x9CCC2B0A, 0xF680, 0x4D80, {0x99, 0xCD, 0x62, 0x7E, 0x2E, 0x47, 0x33, 0x8D}};
static const CLSID CLSID_TagsGreen = {
    0x868EDA94, 0xF969, 0x4FBA, {0xB2, 0xE6, 0xF7, 0x5B, 0xE5, 0x5A, 0x38, 0xD1}};
static const CLSID CLSID_TagsBlue = {
    0x5927D28A, 0xFDD1, 0x452A, {0x9D, 0xEB, 0xCD, 0xE1, 0x99, 0x5D, 0x0F, 0x1E}};
static const CLSID CLSID_TagsPurple = {
    0xB744C38F, 0x69E7, 0x4DBE, {0x8C, 0xD8, 0x98, 0xA4, 0x54, 0x68, 0x9C, 0xB3}};
static const CLSID CLSID_TagsGray = {
    0x1C4543CC, 0x192B, 0x400E, {0x83, 0xDA, 0x11, 0xC6, 0xD2, 0x5C, 0xC7, 0xF9}};
static const CLSID CLSID_TagsRemove = {
    0x93415D06, 0x995E, 0x41F9, {0x97, 0x71, 0xCC, 0x45, 0xA1, 0xB2, 0x13, 0xCA}};

// Icon overlay handlers, one COM class per color. Explorer asks every
// registered handler whether a given item is one of its own and composites
// that handler's icon onto the item's own icon. Unlike a column value or an
// overlay window, the result is drawn by Explorer as part of the item, so it
// cannot lag behind a scroll, land on the wrong row or sit above another
// window - and it shows in every view mode, not only Details.
struct OverlayClass {
    CLSID clsid;
    const wchar_t *colorId;
    const wchar_t *iconFile;
    const wchar_t *displayName;
};

static const OverlayClass kOverlayClasses[] = {
    {{0xCC46952F, 0x1C86, 0x46B4, {0x8C, 0x49, 0xAA, 0x90, 0xF8, 0xDB, 0xDA, 0x56}},
     L"red", L"red.ico", L"ColorTags Red"},
    {{0x580D6C3D, 0x7C45, 0x4EEF, {0x8C, 0x80, 0x08, 0x5B, 0x85, 0x2B, 0x5C, 0x74}},
     L"orange", L"orange.ico", L"ColorTags Orange"},
    {{0xD9D4BE22, 0x2CF8, 0x4F7E, {0xB9, 0xE7, 0x4F, 0x75, 0x00, 0xDD, 0x43, 0x65}},
     L"yellow", L"yellow.ico", L"ColorTags Yellow"},
    {{0x523F13B4, 0xB99A, 0x46B8, {0x82, 0x9A, 0xFB, 0x9E, 0x0E, 0x6A, 0x6C, 0x0D}},
     L"green", L"green.ico", L"ColorTags Green"},
    {{0xE8889757, 0x2B0D, 0x4C63, {0xB8, 0x67, 0x9F, 0xC5, 0xC9, 0xAD, 0x94, 0x6E}},
     L"blue", L"blue.ico", L"ColorTags Blue"},
    {{0xC9A70174, 0x6FDB, 0x4E4A, {0xAF, 0xCA, 0x1F, 0x37, 0x33, 0x32, 0x5C, 0xBD}},
     L"purple", L"purple.ico", L"ColorTags Purple"},
    {{0x3E8FF435, 0x6341, 0x43BD, {0xA9, 0x62, 0x0D, 0xA4, 0x54, 0xE1, 0xB9, 0xCD}},
     L"gray", L"gray.ico", L"ColorTags Gray"},
};

static const OverlayClass *FindOverlayClass(REFCLSID clsid) {
    for (const OverlayClass &candidate : kOverlayClasses) {
        if (IsEqualCLSID(clsid, candidate.clsid)) return &candidate;
    }
    return nullptr;
}

// Property handler CLSID — IPropertyStore bridge (System.Keywords <-> :ColorTag).
// Registered per-user by register.ps1 for file types without a native handler.
static const CLSID CLSID_TagsProperty = {
    0xC9E3056C, 0x1843, 0x4196, {0xA7, 0x16, 0x83, 0xD5, 0xB7, 0x7F, 0x83, 0x12}};

// Isolated CLSID used only by the one-extension Tags-column spike. Keeping it
// separate prevents the experiment from changing existing ColorTags handlers.
static const CLSID CLSID_TagsPropertyProbe = {
    0x3C3E8ABA, 0x8422, 0x4B8D, {0x81, 0x06, 0x1A, 0xB0, 0x84, 0x33, 0xDD, 0x64}};

// Minimal custom property used to validate schema registration before the
// Enumeration/Image/IconList layers are introduced.
static const PROPERTYKEY PKEY_ColorTags_ColorString = {
    {0x828A77DC, 0x07B7, 0x4624, {0x9B, 0xE0, 0x25, 0x32, 0x54, 0xF6, 0x8A, 0x76}},
    2};
static const PROPERTYKEY PKEY_ColorTags_ColorEnum = {
    {0x6BE2AA1A, 0x59B2, 0x423D, {0xBA, 0xCF, 0x21, 0xF5, 0x77, 0x02, 0x8C, 0xCD}},
    2};
static const PROPERTYKEY PKEY_ColorTags_ColorIcon = {
    {0xEB41E2DC, 0xD0F2, 0x40D3, {0xA4, 0x98, 0x01, 0x6A, 0x61, 0x14, 0x7F, 0xE5}},
    2};

// Stage 2 was re-opened because its conclusion rested on an experiment that
// changed five things at once between the enumeration schema that rendered and
// the IconList schema that did not. These two probes change one thing each.
//
// DrawControlProbe answers the prior question - whether Details view honours a
// drawControl on a custom property at all. It carries a constant number and
// asks for Rating, which is visibly not text, so a column of stars means yes
// and a column of digits means no. Nothing about it depends on tags.
static const PROPERTYKEY PKEY_ColorTags_DrawControlProbe = {
    {0xEA3D398E, 0x9508, 0x41B7, {0xB1, 0x12, 0x76, 0xBD, 0x3A, 0xBE, 0x8B, 0x78}}, 2};
// IconProbe is the enumeration schema that already rendered text, plus images
// and drawControl and nothing else: no alignment, no editControl, no
// filterControl, no index attributes. If it renders icons, the original
// verdict was wrong and one of those extras was the cause.
static const PROPERTYKEY PKEY_ColorTags_IconProbe = {
    {0xBC5AEA6B, 0xD71F, 0x4473, {0x9F, 0xFC, 0xC2, 0x99, 0x82, 0x4B, 0x51, 0x5F}}, 2};

// The stage 2 schema under an identity that has never been registered. Same
// enumeration, same images, same drawControl, same searchInfo.
static const PROPERTYKEY PKEY_ColorTags_ColorIconFresh = {
    {0x93473427, 0xF903, 0x4006, {0xA6, 0xB1, 0x5A, 0x80, 0x31, 0x4A, 0xE4, 0x94}}, 2};

static bool IsColorTagsProperty(REFPROPERTYKEY key) {
    return IsEqualPropertyKey(key, PKEY_ColorTags_ColorString) ||
           IsEqualPropertyKey(key, PKEY_ColorTags_ColorEnum) ||
           IsEqualPropertyKey(key, PKEY_ColorTags_IconProbe) ||
           IsEqualPropertyKey(key, PKEY_ColorTags_ColorIconFresh) ||
           IsEqualPropertyKey(key, PKEY_ColorTags_ColorIcon);
}

static HINSTANCE g_hInst = nullptr;
static LONG g_refCount = 0;

static void ModuleAddRef() { InterlockedIncrement(&g_refCount); }
static void ModuleRelease() { InterlockedDecrement(&g_refCount); }

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::wstring RegGetString(HKEY root, const wchar_t *subkey,
                                 const wchar_t *name) {
    std::wstring result;
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD size = 0;
        if (RegQueryValueExW(key, name, nullptr, nullptr, nullptr, &size) ==
                ERROR_SUCCESS &&
            size > 0) {
            result.resize(size / sizeof(wchar_t));
            DWORD actual = size;
            if (RegQueryValueExW(key, name, nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(&result[0]),
                                 &actual) != ERROR_SUCCESS)
                result.clear();
            while (!result.empty() && result.back() == L'\0') result.pop_back();
        }
        RegCloseKey(key);
    }
    return result;
}

static DWORD RegGetDword(HKEY root, const wchar_t *subkey,
                         const wchar_t *name, DWORD fallback) {
    DWORD value = fallback;
    DWORD type = 0;
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, name, nullptr, &type,
                             reinterpret_cast<LPBYTE>(&value), &size) !=
                ERROR_SUCCESS ||
            type != REG_DWORD || size != sizeof(value)) {
            value = fallback;
        }
        RegCloseKey(key);
    }
    return value;
}

static std::wstring QuoteArg(const std::wstring &s) {
    std::wstring q = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            backslashes++;
        } else if (c == L'"') {
            q.append(backslashes * 2 + 1, L'\\');
            q += c;
            backslashes = 0;
        } else {
            q.append(backslashes, L'\\');
            backslashes = 0;
            q += c;
        }
    }
    // Backslashes immediately before the closing quote must be doubled for
    // CommandLineToArgvW / the Python CRT (notably for roots such as C:\\).
    q.append(backslashes * 2, L'\\');
    q += L"\"";
    return q;
}

static LPWSTR StrDup(const std::wstring &s) {
    LPWSTR out = static_cast<LPWSTR>(
        CoTaskMemAlloc((s.size() + 1) * sizeof(wchar_t)));
    if (out) wcscpy(out, s.c_str());
    return out;
}

static std::vector<std::wstring> CollectPaths(IShellItemArray *items) {
    std::vector<std::wstring> paths;
    if (!items) return paths;
    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return paths;
    for (DWORD i = 0; i < count; i++) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
            LPWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) &&
                name) {
                paths.push_back(name);
                CoTaskMemFree(name);
            }
            item->Release();
        }
    }
    return paths;
}

// Read the ":ColorTag" ADS of a path; returns the tag id ("red", ...) or "".
// Same format as AdsTagStore: UTF-8, no BOM, no trailing newline.
static std::wstring ReadTag(const std::wstring &path) {
    std::wstring ads = path + L":ColorTag";
    HANDLE h = CreateFileW(ads.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    char buf[64] = {};
    DWORD read = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &read, nullptr);
    CloseHandle(h);
    std::string s(buf, read);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                          s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return std::wstring(s.begin(), s.end());
}

// Write the ":ColorTag" ADS of a path. Same format AdsTagStore writes: the
// stable id, UTF-8, no BOM, no trailing newline. FILE_FLAG_BACKUP_SEMANTICS is
// what allows a folder to carry the stream as well as a file.
//
// Returns false when the item cannot hold a stream at all - a volume without
// named streams, a cloud placeholder, a read-only file - which is the caller's
// signal to hand that path to the CLI and its SQLite fallback.
// Writing or deleting an alternate data stream counts as writing the item, so
// NTFS moves its last-write time: tagging a file would show up as an edit in
// "Date modified", in backup tools and in sync clients. The item's own three
// times are captured before the stream is touched and put back after the stream
// handle is closed — restoring them earlier would be undone by the close.
//
// The restore is best effort: a read-only volume or a denied handle leaves the
// tag written and the timestamp moved, which is the lesser of the two failures.
struct PreservedTimes {
    HANDLE handle = INVALID_HANDLE_VALUE;
    FILETIME creation{};
    FILETIME access{};
    FILETIME write{};
    bool captured = false;

    explicit PreservedTimes(const std::wstring &path) {
        handle = CreateFileW(path.c_str(),
                             FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                             nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            captured = GetFileTime(handle, &creation, &access, &write) != FALSE;
        }
    }
    ~PreservedTimes() {
        if (handle == INVALID_HANDLE_VALUE) return;
        if (captured) SetFileTime(handle, &creation, &access, &write);
        CloseHandle(handle);
    }
    PreservedTimes(const PreservedTimes &) = delete;
    PreservedTimes &operator=(const PreservedTimes &) = delete;
};

static bool WriteTagNative(const std::wstring &path, const std::wstring &id) {
    int count = WideCharToMultiByte(CP_UTF8, 0, id.c_str(),
                                    static_cast<int>(id.size()), nullptr, 0,
                                    nullptr, nullptr);
    if (count < 0) return false;
    std::string utf8(static_cast<size_t>(count), '\0');
    if (count) {
        WideCharToMultiByte(CP_UTF8, 0, id.c_str(), static_cast<int>(id.size()),
                            utf8.data(), count, nullptr, nullptr);
    }
    const PreservedTimes times(path);
    const std::wstring stream = path + L":ColorTag";
    HANDLE file = CreateFileW(stream.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, CREATE_ALWAYS, FILE_FLAG_BACKUP_SEMANTICS,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = TRUE;
    if (!utf8.empty()) {
        ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()),
                       &written, nullptr);
    }
    CloseHandle(file);
    return ok && written == static_cast<DWORD>(utf8.size());
}

// Remove the ":ColorTag" ADS. A missing stream counts as success: the item is
// untagged either way.
static bool RemoveTagNative(const std::wstring &path) {
    const PreservedTimes times(path);
    const std::wstring stream = path + L":ColorTag";
    if (DeleteFileW(stream.c_str())) return true;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static std::wstring LowerString(std::wstring value) {
    for (wchar_t &c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}

// Explorer calls IsMemberOf once per registered handler for every item it
// shows, so seven handlers over a folder of N files would mean 7*N stream
// opens. All seven live in this one DLL, so a single short-lived cache serves
// them: the first handler to ask about a path pays for the read, the other six
// do not.
//
// Entries expire in well under a second, which is what bounds how long a tag
// change can be held back. Invoke also clears the cache, but only within its
// own module instance: the menu runs from the packaged DLL and the overlay
// handlers from the local copy, so that clear does not reach across. The short
// lifetime, not the clear, is what makes this correct.
//
// Explorer calls these handlers from several threads at once.
static std::mutex g_tagCacheMutex;
static std::map<std::wstring, std::pair<std::wstring, ULONGLONG>> g_tagCache;
static const ULONGLONG kTagCacheMilliseconds = 800;
static const size_t kTagCacheLimit = 4096;

static std::wstring ReadTagCached(const std::wstring &path) {
    const std::wstring key = LowerString(path);
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> guard(g_tagCacheMutex);
        auto found = g_tagCache.find(key);
        if (found != g_tagCache.end() &&
            now - found->second.second < kTagCacheMilliseconds) {
            return found->second.first;
        }
    }
    std::wstring tag = ReadTag(path);
    {
        std::lock_guard<std::mutex> guard(g_tagCacheMutex);
        // A flat cap with a wholesale clear: browsing a very large folder must
        // not let this grow without bound inside Explorer's process.
        if (g_tagCache.size() > kTagCacheLimit) g_tagCache.clear();
        g_tagCache[key] = {tag, now};
    }
    return tag;
}

static void InvalidateTagCache() {
    std::lock_guard<std::mutex> guard(g_tagCacheMutex);
    g_tagCache.clear();
}

// Where this DLL sits. The overlay icons are installed beside it.
static std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, path, MAX_PATH)) return {};
    std::wstring full = path;
    const size_t slash = full.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : full.substr(0, slash + 1);
}

static void DebugLog(const wchar_t *fmt, ...) {
#ifdef _DEBUG
    wchar_t buf[1024] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1024, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
#else
    (void)fmt;
#endif
}

// Off unless HKCU\Software\ColorTags\TraceFile holds a path, and read once per
// process, so Explorer has to be restarted after setting it. DebugLog compiles
// away in a release build, which left no way to tell an empty column caused by
// a value that never arrives from one caused by a draw control that refuses to
// paint. This does.
static void TraceLine(const std::wstring &text) {
    static const std::wstring path =
        RegGetString(HKEY_CURRENT_USER, L"Software\\ColorTags", L"TraceFile");
    if (path.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const std::wstring line = text + L"\r\n";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                                          static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                            static_cast<int>(line.size()), utf8.data(), bytes,
                            nullptr, nullptr);
        DWORD written = 0;
        SetFilePointer(file, 0, nullptr, FILE_END);
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                  nullptr);
    }
    CloseHandle(file);
}

static std::wstring KeyText(REFPROPERTYKEY key) {
    wchar_t guid[64] = {};
    StringFromGUID2(key.fmtid, guid, 64);
    return std::wstring(guid) + L"/" + std::to_wstring(key.pid);
}

static bool EqIgnoreCase(const std::wstring &a, const std::wstring &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}

// "red" -> the user's label for that tag ("Red" until renamed). Shown in the
// context menu, the Details pane and the Tags column. The persisted ADS value
// stays the stable id; see src/Shared/ColorTagsConfig.h.
static std::wstring TagDisplayName(const std::wstring &id) {
    if (id.empty()) return id;
    std::wstring label = colortags::LabelOf(id);
    if (!label.empty()) return label;
    std::wstring d = id;
    d[0] = static_cast<wchar_t>(std::towupper(d[0]));
    return d;
}

// Presentation-only value for Explorer's Tags column. The persisted ADS value
// remains the stable ASCII id ("red", "green", ...).
static const wchar_t *TagEmoji(const std::wstring &id) {
    if (id == L"red") return L"🔴";
    if (id == L"orange") return L"🟠";
    if (id == L"yellow") return L"🟡";
    if (id == L"green") return L"🟢";
    if (id == L"blue") return L"🔵";
    if (id == L"purple") return L"🟣";
    if (id == L"gray") return L"⚪";  // Unicode has no gray circle emoji.
    return nullptr;
}

// What goes into the Tags column: the label, the emoji, or both. Set
// HKCU\Software\ColorTags\ColumnFormat to label, emoji or emoji-label; read
// once per process, so Explorer has to be restarted.
//
// Stage 1 was closed on the finding that Windows 11 draws these emoji as a
// monochrome glyph. That is what GDI text rendering does with a color font;
// DirectWrite draws them in color, and Explorer shows color emoji in file
// names. Since the handler is now known to be asked and its value known to
// reach the column, the question is worth putting back to the real Explorer
// rather than inferred.
enum class ColumnFormat { Label, Emoji, EmojiLabel };

static ColumnFormat CurrentColumnFormat() {
    static const ColumnFormat format = [] {
        std::wstring value =
            RegGetString(HKEY_CURRENT_USER, L"Software\\ColorTags", L"ColumnFormat");
        for (wchar_t &c : value) c = static_cast<wchar_t>(std::towlower(c));
        if (value == L"emoji") return ColumnFormat::Emoji;
        if (value == L"emoji-label") return ColumnFormat::EmojiLabel;
        return ColumnFormat::Label;
    }();
    return format;
}

// Parse a Keywords string ("Red; Blue", "red", ...) into a tag id, or ""
// when nothing matches. Empty/whitespace input -> "" (means "remove").
static std::wstring ParseColor(const std::wstring &input) {
    static const wchar_t *ids[] = {L"red",   L"orange", L"yellow", L"green",
                                   L"blue",  L"purple", L"gray"};
    std::wstring token;
    auto check = [&]() -> std::wstring {
        size_t b = token.find_first_not_of(L" \t");
        if (b == std::wstring::npos) return L"";
        size_t e = token.find_last_not_of(L" \t");
        std::wstring t = token.substr(b, e - b + 1);
        for (const wchar_t *id : ids) {
            const wchar_t *emoji = TagEmoji(id);
            const colortags::TagInfo *info = colortags::FindTag(id);
            const std::wstring label = TagDisplayName(id);
            // The default label stays accepted after a rename so values typed
            // earlier, or cached by Explorer, still map back to the same tag.
            if (EqIgnoreCase(t, id) || EqIgnoreCase(t, label) ||
                (info && EqIgnoreCase(t, info->defaultLabel)) ||
                (emoji && t == emoji) ||
                (emoji && t == std::wstring(emoji) + L" " + label))
                return id;
        }
        return L"";
    };
    for (wchar_t c : input) {
        if (c == L';' || c == L',') {
            std::wstring hit = check();
            if (!hit.empty()) return hit;
            token.clear();
        } else {
            token += c;
        }
    }
    return check();
}

// Write the ":ColorTag" ADS (UTF-8, no BOM, no trailing newline — same format
// as AdsTagStore). Returns false on failure (e.g. read-only file).
static bool WriteTag(const std::wstring &path, const std::wstring &id) {
    std::wstring ads = path + L":ColorTag";
    int len = WideCharToMultiByte(CP_UTF8, 0, id.c_str(), -1, nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 1) return false;
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, id.c_str(), -1, &utf8[0], len, nullptr,
                        nullptr);
    HANDLE h = CreateFileW(ads.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()),
                        &written, nullptr);
    CloseHandle(h);
    return ok && written == utf8.size();
}

// Run the Python CLI; stdout/stderr go to %LOCALAPPDATA%\Colortags\menu.log.
static bool RunCli(const std::wstring &args) {
    std::wstring python =
        RegGetString(HKEY_CURRENT_USER, L"Software\\ColorTags", L"PythonPath");
    std::wstring root =
        RegGetString(HKEY_CURRENT_USER, L"Software\\ColorTags", L"ProjectRoot");
    if (python.empty() || root.empty()) {
        MessageBoxW(nullptr,
                    L"ColorTags: PythonPath/ProjectRoot not configured.\n"
                    L"Run register.ps1 first.",
                    L"ColorTags", MB_OK | MB_ICONERROR);
        return false;
    }

    std::wstring cmdline = QuoteArg(python) + L" " + args;
    DebugLog(L"RunCli cmdline=%ls", cmdline.c_str());

    wchar_t localAppData[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH))
        wcscpy(localAppData, L"C:\\Windows\\Temp");
    std::wstring logDir = std::wstring(localAppData) + L"\\Colortags";
    std::wstring logFile = logDir + L"\\menu.log";
    CreateDirectoryW(logDir.c_str(), nullptr);
    HANDLE hLog = CreateFileW(logFile.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (hLog != INVALID_HANDLE_VALUE) {
        // Redirect stdout/stderr to menu.log. stdin must be a VALID handle:
        // GetStdHandle() in Explorer (a GUI process) returns
        // INVALID_HANDLE_VALUE, and CreateProcessW with STARTF_USESTDHANDLES
        // then fails with ERROR_INVALID_HANDLE — so open NUL instead.
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        si.hStdInput = CreateFileW(L"NUL", GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    }

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmdline.begin(), cmdline.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(python.c_str(), cmdBuf.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, root.c_str(),
                             &si, &pi);
    if (si.hStdInput && si.hStdInput != INVALID_HANDLE_VALUE)
        CloseHandle(si.hStdInput);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) {
        DWORD err = GetLastError();
        DebugLog(L"RunCli CreateProcess FAILED err=%lu", err);
        MessageBoxW(nullptr,
                    (L"ColorTags: failed to start Python (error " +
                     std::to_wstring(err) + L").\n" + cmdline)
                        .c_str(),
                    L"ColorTags", MB_OK | MB_ICONERROR);
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DebugLog(L"RunCli exit=%lu", exitCode);
    if (exitCode != 0) {
        MessageBoxW(nullptr,
                    (L"ColorTags: command failed (exit " +
                     std::to_wstring(exitCode) + L").\nSee " + logFile)
                        .c_str(),
                    L"ColorTags", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

// Tell Explorer to re-read the items. The CLI preserves the file's
// LastWriteTime (so "Date modified" does not change), and deleting an ADS
// does not touch it either — without this notification the Tags column
// would keep showing the stale value until a manual refresh.
static void NotifyShellUpdate(const std::vector<std::wstring> &paths) {
    for (const auto &p : paths) {
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, p.c_str(), nullptr);
    }
}

// ---------------------------------------------------------------------------
// CTagsSubCommand — one color (or "Remove tag")
// ---------------------------------------------------------------------------

class CTagsSubCommand : public IExplorerCommand {
  public:
    CTagsSubCommand(std::wstring title, const wchar_t *colorId, bool isRemove,
                    const CLSID &canonical, int iconId)
        : m_ref(1), m_title(std::move(title)), m_colorId(colorId), m_isRemove(isRemove),
          m_canonical(canonical), m_iconId(iconId) {
        ModuleAddRef();
    }
    ~CTagsSubCommand() { ModuleRelease(); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
            *ppv = static_cast<IExplorerCommand *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // IExplorerCommand
    STDMETHODIMP GetTitle(IShellItemArray *, LPWSTR *ppszName) override {
        DebugLog(L"SubCmd::GetTitle %ls", m_title.c_str());
        *ppszName = StrDup(m_title);
        return *ppszName ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetIcon(IShellItemArray *psiItemArray, LPWSTR *ppszIcon) override {
        // Full module path — the shell does not resolve a bare DLL name for
        // packaged extensions, so "ColorTagsMenu.dll,-101" renders blank.
        // When this color is the active tag, swap to the circle-with-check
        // icon (id+100) — the Win11 menu does not render ECS_CHECKED.
        int iconId = m_iconId;
        if (IsActive(psiItemArray)) iconId += 100;
        DebugLog(L"SubCmd::GetIcon %ls icon=%d", m_title.c_str(), iconId);
        wchar_t dllPath[MAX_PATH] = {};
        GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);
        *ppszIcon =
            StrDup(std::wstring(dllPath) + L",-" + std::to_wstring(iconId));
        return *ppszIcon ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetToolTip(IShellItemArray *, LPWSTR *ppszTip) override {
        *ppszTip = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetCanonicalName(GUID *pguid) override {
        *pguid = m_canonical;
        return S_OK;
    }
    STDMETHODIMP GetState(IShellItemArray *psiItemArray, BOOL,
                          EXPCMDSTATE *pState) override {
        *pState = ECS_ENABLED;
        bool active = IsActive(psiItemArray);
        DebugLog(L"SubCmd::GetState %ls active=%d", m_title.c_str(), active);
        // Selection is indicated by the checked icon variant (id+100) only —
        // no ECS_CHECKED checkbox in front of the item.
        return S_OK;
    }
    STDMETHODIMP GetFlags(EXPCMDFLAGS *pFlags) override {
        *pFlags = ECF_DEFAULT;
        return S_OK;
    }
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand **ppEnum) override {
        *ppEnum = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP Invoke(IShellItemArray *psiItemArray, IBindCtx *) override {
        std::vector<std::wstring> paths = CollectPaths(psiItemArray);
        DebugLog(L"SubCmd::Invoke %ls paths=%zu", m_title.c_str(), paths.size());
        if (paths.empty()) return S_OK;

        // The stream is written here rather than through the Python CLI.
        // Invoke runs on Explorer's UI thread, and starting an interpreter cost
        // a noticeable fraction of a second on every single command. The DLL
        // already reads this stream natively, so writing it natively keeps the
        // one format in one place rather than adding a second one.
        //
        // Whatever the native write cannot handle still goes to the CLI, which
        // owns the SQLite fallback for volumes without named streams.
        std::vector<std::wstring> fallback;
        for (const auto &p : paths) {
            const bool written = m_isRemove ? RemoveTagNative(p)
                                            : WriteTagNative(p, m_colorId);
            if (!written) fallback.push_back(p);
        }
        if (!fallback.empty()) {
            DebugLog(L"SubCmd::Invoke native write failed for %zu paths,"
                     L" handing them to the CLI", fallback.size());
            std::wstring args = m_isRemove
                ? std::wstring(L"-m src.Cli.colortag remove-many")
                : std::wstring(L"-m src.Cli.colortag set-many ") + m_colorId;
            for (const auto &p : fallback) args += L" " + QuoteArg(p);
            if (!RunCli(args)) return E_FAIL;
        }
        // The icon overlay handlers read through a short-lived cache in this
        // same DLL; drop it so the new color is on screen with Explorer's
        // refresh rather than up to a cache lifetime later.
        InvalidateTagCache();
        NotifyShellUpdate(paths);
        return S_OK;
    }

  private:
    // True when every selected item carries exactly this color (or the
    // command is "Remove tag", which is never active).
    bool IsActive(IShellItemArray *psiItemArray) const {
        if (m_isRemove) return false;
        std::vector<std::wstring> paths = CollectPaths(psiItemArray);
        if (paths.empty()) return false;
        for (const auto &p : paths) {
            if (ReadTag(p) != m_colorId) return false;
        }
        return true;
    }

    LONG m_ref;
    std::wstring m_title;
    std::wstring m_colorId;
    bool m_isRemove;
    CLSID m_canonical;
    int m_iconId;
};

// ---------------------------------------------------------------------------
// CEnumExplorerCommand — the 8 subcommands
// ---------------------------------------------------------------------------

class CEnumExplorerCommand : public IEnumExplorerCommand {
  public:
    CEnumExplorerCommand() : m_ref(1), m_index(0) {
        ModuleAddRef();
        // Titles are read per menu build, so renaming a tag shows up on the
        // next right click without restarting Explorer. The canonical CLSIDs
        // and the color ids never change with the label.
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"red"), L"red", false,
                                             CLSID_TagsRed, 101));
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"orange"), L"orange", false,
                                             CLSID_TagsOrange, 102));
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"yellow"), L"yellow", false,
                                             CLSID_TagsYellow, 103));
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"green"), L"green", false,
                                             CLSID_TagsGreen, 104));
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"blue"), L"blue", false,
                                             CLSID_TagsBlue, 105));
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"purple"), L"purple", false,
                                             CLSID_TagsPurple, 106));
        m_cmds.push_back(new CTagsSubCommand(TagDisplayName(L"gray"), L"gray", false,
                                             CLSID_TagsGray, 107));
        m_cmds.push_back(new CTagsSubCommand(L"Remove tag", L"", true,
                                             CLSID_TagsRemove, 108));
    }
    ~CEnumExplorerCommand() {
        for (auto *c : m_cmds) c->Release();
        ModuleRelease();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumExplorerCommand) {
            *ppv = static_cast<IEnumExplorerCommand *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // IEnumExplorerCommand
    STDMETHODIMP Next(ULONG celt, IExplorerCommand **pelt,
                      ULONG *pceltFetched) override {
        ULONG fetched = 0;
        while (fetched < celt && m_index < m_cmds.size()) {
            pelt[fetched] = m_cmds[m_index];
            pelt[fetched]->AddRef();
            m_index++;
            fetched++;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return fetched == celt ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG celt) override {
        m_index += celt;
        if (m_index > m_cmds.size()) m_index = m_cmds.size();
        return S_OK;
    }
    STDMETHODIMP Reset() override {
        m_index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumExplorerCommand **ppEnum) override {
        auto *e = new CEnumExplorerCommand();
        e->m_index = m_index;
        *ppEnum = e;
        return S_OK;
    }

  private:
    LONG m_ref;
    size_t m_index;
    std::vector<CTagsSubCommand *> m_cmds;
};

// ---------------------------------------------------------------------------
// CTagsMenuCommand — the "Tags" parent
// ---------------------------------------------------------------------------

class CTagsMenuCommand : public IExplorerCommand {
  public:
    CTagsMenuCommand() : m_ref(1) { ModuleAddRef(); }
    ~CTagsMenuCommand() { ModuleRelease(); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
            *ppv = static_cast<IExplorerCommand *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // IExplorerCommand
    STDMETHODIMP GetTitle(IShellItemArray *, LPWSTR *ppszName) override {
        *ppszName = StrDup(L"Tags");
        return *ppszName ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetIcon(IShellItemArray *, LPWSTR *ppszIcon) override {
        // The menu assets use different neutral strokes so they remain legible
        // against the current app theme. Explorer asks again when the menu is
        // reopened; a theme change therefore does not require reinstalling.
        constexpr wchar_t personalizeKey[] =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
        bool light = RegGetDword(HKEY_CURRENT_USER, personalizeKey,
                                 L"AppsUseLightTheme", 1) != 0;
        int iconId = light ? 110 : 109;
        wchar_t dllPath[MAX_PATH] = {};
        GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);
        *ppszIcon =
            StrDup(std::wstring(dllPath) + L",-" + std::to_wstring(iconId));
        return *ppszIcon ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetToolTip(IShellItemArray *, LPWSTR *ppszTip) override {
        *ppszTip = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetCanonicalName(GUID *pguid) override {
        *pguid = CLSID_TagsMenu;
        return S_OK;
    }
    STDMETHODIMP GetState(IShellItemArray *, BOOL, EXPCMDSTATE *pState) override {
        DebugLog(L"Parent::GetState");
        *pState = ECS_ENABLED;
        return S_OK;
    }
    STDMETHODIMP GetFlags(EXPCMDFLAGS *pFlags) override {
        *pFlags = ECF_HASSUBCOMMANDS;
        return S_OK;
    }
    STDMETHODIMP Invoke(IShellItemArray *, IBindCtx *) override {
        return E_NOTIMPL;  // parent of a submenu; never invoked
    }
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand **ppEnum) override {
        DebugLog(L"Parent::EnumSubCommands");
        *ppEnum = new CEnumExplorerCommand();
        return *ppEnum ? S_OK : E_OUTOFMEMORY;
    }

  private:
    LONG m_ref;
};

// ---------------------------------------------------------------------------
// CTagsPropertyStore — IPropertyStore bridge: System.Keywords <-> :ColorTag
//
// Registered per-user (HKCU\Software\Classes\SystemFileAssociations\.ext\
// shellex\PropertyHandler) for file types without a native property handler,
// so Explorer's Details pane / Tags column / search see our tags.
// ---------------------------------------------------------------------------

class CTagsPropertyStore : public IInitializeWithFile,
                           public IPropertyStore,
                           public IPropertyStoreCapabilities,
                           public IPropertySetStorage {
  public:
    CTagsPropertyStore() : m_ref(1), m_initialized(false), m_dirty(false) {
        ModuleAddRef();
    }
    ~CTagsPropertyStore() { ModuleRelease(); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        wchar_t iidStr[64] = {};
        StringFromGUID2(riid, iidStr, 64);
        DebugLog(L"QI %ls", iidStr);
        if (riid == IID_IUnknown || riid == IID_IInitializeWithFile) {
            *ppv = static_cast<IInitializeWithFile *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IPropertyStore) {
            *ppv = static_cast<IPropertyStore *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IPropertyStoreCapabilities) {
            *ppv = static_cast<IPropertyStoreCapabilities *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IPropertySetStorage) {
            *ppv = static_cast<IPropertySetStorage *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithFile
    STDMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD) override {
        DebugLog(L"Initialize path=%ls", pszFilePath ? pszFilePath : L"(null)");
        m_path = pszFilePath ? pszFilePath : L"";
        m_initialized = true;
        m_dirty = false;
        m_pending.clear();
        return S_OK;
    }

    // IPropertyStore
    STDMETHODIMP GetCount(DWORD *pcProps) override {
        DebugLog(L"GetCount");
        if (!m_initialized) return STG_E_INVALIDFUNCTION;
        *pcProps = 7;  // System.Keywords + the String/Enumeration/IconList,
                       // draw-control, icon and fresh-identity probes
        return S_OK;
    }
    STDMETHODIMP GetAt(DWORD iProp, PROPERTYKEY *pkey) override {
        DebugLog(L"GetAt iProp=%lu", iProp);
        if (!m_initialized) return STG_E_INVALIDFUNCTION;
        if (iProp == 0)
            *pkey = PKEY_Keywords;
        else if (iProp == 1)
            *pkey = PKEY_ColorTags_ColorString;
        else if (iProp == 2)
            *pkey = PKEY_ColorTags_ColorEnum;
        else if (iProp == 3)
            *pkey = PKEY_ColorTags_ColorIcon;
        else if (iProp == 4)
            *pkey = PKEY_ColorTags_DrawControlProbe;
        else if (iProp == 5)
            *pkey = PKEY_ColorTags_IconProbe;
        else if (iProp == 6)
            *pkey = PKEY_ColorTags_ColorIconFresh;
        else
            return E_INVALIDARG;
        return S_OK;
    }
    STDMETHODIMP GetValue(REFPROPERTYKEY key, PROPVARIANT *pv) override {
        PropVariantInit(pv);
        DebugLog(L"GetValue key=%08lX-%04X-%04X", key.fmtid.Data1, key.fmtid.Data2, key.fmtid.Data3);
        if (!m_initialized) return STG_E_INVALIDFUNCTION;
        // A constant, deliberately unrelated to any tag: this probe measures
        // whether the column honours a draw control, nothing else. 60 of 99
        // lands in the middle of the star range, so three stars means the
        // control was applied and "60" means it was not.
        if (IsEqualPropertyKey(key, PKEY_ColorTags_DrawControlProbe)) {
            if (!m_initialized) return STG_E_INVALIDFUNCTION;
            return InitPropVariantFromUInt32(60, pv);
        }
        bool isKeywords = IsEqualPropertyKey(key, PKEY_Keywords);
        bool isColor = IsColorTagsProperty(key);
        if (!isKeywords && !isColor) {
            TraceLine(L"GetValue " + KeyText(key) + L" -> not ours");
            return S_OK;
        }
        std::wstring tag = ReadTag(m_path);
        DebugLog(L"  tag=%ls", tag.c_str());
        TraceLine(L"GetValue " + KeyText(key) + L" path=" + m_path +
                  L" tag=\"" + tag + L"\"");
        if (tag.empty()) return S_OK;  // empty string
        if (isColor) return InitPropVariantFromString(tag.c_str(), pv);
        // The label, not an emoji. Windows 11 Details View draws emoji as a
        // monochrome glyph (roadmap stage 1, closed negative), while plain text
        // is rendered by Explorer itself and therefore never lags behind a
        // scroll or a window move. Color is added on top by the overlay: this
        // value is the part that is always smooth.
        // In dot mode the overlay paints a disk in this very cell, and any text
        // underneath shows through around it ("...een" next to a green dot), so
        // the cell is left empty and the dot is the whole display. The value
        // comes back as soon as the mode changes; Explorer caches column text,
        // so the folder needs F5 either way.
        if (colortags::CurrentDisplayMode() == colortags::DisplayMode::Dot) {
            TraceLine(L"  column value -> (blank: dot mode)");
            return S_OK;
        }
        std::wstring display = TagDisplayName(tag);
        if (display.empty()) return S_OK;
        if (const wchar_t *emoji = TagEmoji(tag)) {
            if (CurrentColumnFormat() == ColumnFormat::Emoji) {
                display = emoji;
            } else if (CurrentColumnFormat() == ColumnFormat::EmojiLabel) {
                display = std::wstring(emoji) + L" " + display;
            }
        }
        TraceLine(L"  column value -> \"" + display + L"\"");
        // System.Keywords is a Multivalue String. Returning a scalar LPWSTR is
        // an ABI/type mismatch even when some Explorer builds display it.
        PCWSTR values[] = {display.c_str()};
        return InitPropVariantFromStringVector(values, 1, pv);
    }
    STDMETHODIMP SetValue(REFPROPERTYKEY key, REFPROPVARIANT propvar) override {
        DebugLog(L"SetValue vt=%u", propvar.vt);
        if (!m_initialized) return STG_E_INVALIDFUNCTION;
        if (!IsEqualPropertyKey(key, PKEY_Keywords) &&
            !IsColorTagsProperty(key))
            return S_OK;
        m_pending.clear();
        m_dirty = false;
        std::wstring input;
        if (propvar.vt == VT_LPWSTR && propvar.pwszVal) {
            input = propvar.pwszVal;
        } else if (propvar.vt == VT_LPSTR && propvar.pszVal) {
            input.assign(propvar.pszVal,
                         propvar.pszVal + strlen(propvar.pszVal));
        } else if (propvar.vt == (VT_VECTOR | VT_LPWSTR) &&
                   propvar.calpwstr.pElems) {
            for (ULONG i = 0; i < propvar.calpwstr.cElems; i++) {
                if (i) input += L"; ";
                input += propvar.calpwstr.pElems[i];
            }
        }
        // Empty input -> remove the tag; unknown text -> leave unchanged.
        bool hasText = false;
        for (wchar_t c : input)
            if (c != L' ' && c != L'\t' && c != L';' && c != L',') {
                hasText = true;
                break;
            }
        if (!hasText) {
            m_pending.clear();  // remove
            m_dirty = true;
            return S_OK;
        }
        std::wstring hit = ParseColor(input);
        if (hit.empty()) return S_OK;  // unknown color: ignore
        m_pending = hit;
        m_dirty = true;
        return S_OK;
    }
    STDMETHODIMP Commit() override {
        DebugLog(L"Commit");
        if (!m_initialized) return STG_E_INVALIDFUNCTION;
        if (!m_dirty) return S_OK;
        if (m_pending.empty()) {
            std::wstring ads = m_path + L":ColorTag";
            DeleteFileW(ads.c_str());
        } else if (!WriteTag(m_path, m_pending)) {
            return STG_E_ACCESSDENIED;
        }
        m_dirty = false;
        return S_OK;
    }
    // IPropertyStoreCapabilities
    STDMETHODIMP IsPropertyWritable(REFPROPERTYKEY key) override {
        DebugLog(L"IsPropertyWritable");
        return (IsEqualPropertyKey(key, PKEY_Keywords) ||
                IsColorTagsProperty(key))
                   ? S_OK
                   : S_FALSE;
    }

    // IPropertySetStorage — the shellex\PropertyHandler path creates the
    // handler with this IID, then initializes via IInitializeWithFile and
    // reads values through IPropertyStore. The legacy IPropertyStorage
    // protocol is not used on the read path, so these are stubs.
    STDMETHODIMP Create(REFFMTID fmtid, const CLSID *, DWORD, DWORD,
                        IPropertyStorage **ppPropStg) override {
        DebugLog(L"PSS::Create");
        if (!ppPropStg) return E_POINTER;
        *ppPropStg = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP Open(REFFMTID fmtid, DWORD, IPropertyStorage **ppPropStg) override {
        DebugLog(L"PSS::Open");
        if (!ppPropStg) return E_POINTER;
        *ppPropStg = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP Delete(REFFMTID) override {
        DebugLog(L"PSS::Delete");
        return E_NOTIMPL;
    }
    STDMETHODIMP Enum(IEnumSTATPROPSETSTG **ppenum) override {
        DebugLog(L"PSS::Enum");
        if (!ppenum) return E_POINTER;
        *ppenum = nullptr;
        return E_NOTIMPL;
    }

  private:
    LONG m_ref;
    bool m_initialized;
    bool m_dirty;
    std::wstring m_path;
    std::wstring m_pending;
};

// ---------------------------------------------------------------------------
// class factory — dispatches on the requested CLSID
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CTagsIconOverlay — one color's icon overlay handler
// ---------------------------------------------------------------------------

// The mingw headers declare IShellIconOverlayIdentifier with the legacy
// DECLARE_INTERFACE_ macros and no uuid attribute, so __uuidof cannot find it
// and the link fails. The IID is fixed and public, so it is spelled out here
// rather than taken from a library that may or may not export it.
// 0C6C4200-C589-11D0-999A-00C04FD655E1
static const IID IID_ColorTags_ShellIconOverlayIdentifier = {
    0x0C6C4200, 0xC589, 0x11D0,
    {0x99, 0x9A, 0x00, 0xC0, 0x4F, 0xD6, 0x55, 0xE1}};

// Those same macros give the interface's methods a stricter exception
// specification than STDMETHODIMP carries, which clang reports on every
// override. Nothing can be thrown across a COM boundary either way, so the
// warning describes a header artifact rather than a defect in this class.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-exception-spec"

class CTagsIconOverlay : public IShellIconOverlayIdentifier {
  public:
    explicit CTagsIconOverlay(const OverlayClass &definition)
        : m_ref(1), m_definition(definition) {
        ModuleAddRef();
    }
    ~CTagsIconOverlay() { ModuleRelease(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown ||
            riid == IID_ColorTags_ShellIconOverlayIdentifier) {
            *ppv = static_cast<IShellIconOverlayIdentifier *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // Called for every item Explorer displays, on Explorer's own threads. It
    // must stay cheap, must not block and must never touch the UI or the CLI.
    STDMETHODIMP IsMemberOf(PCWSTR pwszPath, DWORD) override {
        if (!pwszPath || !*pwszPath) return S_FALSE;
        return EqIgnoreCase(ReadTagCached(pwszPath), m_definition.colorId)
                   ? S_OK
                   : S_FALSE;
    }

    STDMETHODIMP GetOverlayInfo(PWSTR pwszIconFile, int cchMax, int *pIndex,
                                DWORD *pdwFlags) override {
        if (!pwszIconFile || !pIndex || !pdwFlags) return E_POINTER;
        // A plain .ico beside the DLL rather than a resource inside it: an
        // explicit file and index leave no doubt about which image the shell
        // picks up, and the icons can be replaced without a rebuild.
        const std::wstring icon =
            ModuleDirectory() + L"icons\\" + m_definition.iconFile;
        if (icon.empty() || icon.size() + 1 > static_cast<size_t>(cchMax))
            return E_FAIL;
        lstrcpynW(pwszIconFile, icon.c_str(), cchMax);
        *pIndex = 0;
        *pdwFlags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
        return S_OK;
    }

    // Every item carries at most one tag, so the handlers never compete with
    // each other and the value only orders them against other products'.
    STDMETHODIMP GetPriority(int *pIPriority) override {
        if (!pIPriority) return E_POINTER;
        *pIPriority = 0;
        return S_OK;
    }

  private:
    LONG m_ref;
    const OverlayClass &m_definition;
};

#pragma clang diagnostic pop

class CTagsFactory : public IClassFactory {
  public:
    CTagsFactory(REFCLSID clsid) : m_ref(1), m_clsid(clsid) { ModuleAddRef(); }
    ~CTagsFactory() { ModuleRelease(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid,
                                void **ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        IUnknown *obj = nullptr;
        if (IsEqualCLSID(m_clsid, CLSID_TagsMenu))
            obj = new CTagsMenuCommand();
        else if (IsEqualCLSID(m_clsid, CLSID_TagsProperty) ||
                 IsEqualCLSID(m_clsid, CLSID_TagsPropertyProbe))
            obj = static_cast<IInitializeWithFile *>(new CTagsPropertyStore());
        else if (const OverlayClass *overlay = FindOverlayClass(m_clsid))
            obj = static_cast<IShellIconOverlayIdentifier *>(
                new CTagsIconOverlay(*overlay));
        else
            return CLASS_E_CLASSNOTAVAILABLE;
        HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock)
            InterlockedIncrement(&g_refCount);
        else
            InterlockedDecrement(&g_refCount);
        return S_OK;
    }

  private:
    LONG m_ref;
    CLSID m_clsid;
};

// ---------------------------------------------------------------------------
// DLL exports (declared dllexport by the SDK headers)
// ---------------------------------------------------------------------------

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    if (!IsEqualCLSID(rclsid, CLSID_TagsMenu) &&
        !IsEqualCLSID(rclsid, CLSID_TagsProperty) &&
        !IsEqualCLSID(rclsid, CLSID_TagsPropertyProbe) &&
        !FindOverlayClass(rclsid))
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new CTagsFactory(rclsid);
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_refCount == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    // Per-user registration (no admin): HKCU\Software\Classes\CLSID\{guid}
    wchar_t clsidStr[64] = {};
    StringFromGUID2(CLSID_TagsMenu, clsidStr, 64);
    std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    const wchar_t *desc = L"ColorTags Tags Menu";
    RegSetValueExW(key, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(desc),
                   static_cast<DWORD>((wcslen(desc) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);

    std::wstring inproc = keyPath + L"\\InprocServer32";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, inproc.c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);
    RegSetValueExW(key, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(dllPath),
                   static_cast<DWORD>((wcslen(dllPath) + 1) * sizeof(wchar_t)));
    const wchar_t *model = L"Apartment";
    RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(model),
                   static_cast<DWORD>((wcslen(model) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsidStr[64] = {};
    StringFromGUID2(CLSID_TagsMenu, clsidStr, 64);
    std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;
    RegDeleteTreeW(HKEY_CURRENT_USER, keyPath.c_str());
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}
