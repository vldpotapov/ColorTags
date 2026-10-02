// ColorTagsConfig.h — the single visual contract (roadmap stage 5).
//
// One table of ids, colors and labels, shared by every surface that shows a
// tag to the user:
//
//   - the context menu           (src/ShellExtension/tags_menu.cpp)
//   - the property handler       (System.Keywords value in the Tags column)
//   - the native overlay         (src/VisualNative/ColorTagsOverlayNative.cpp)
//
// The persisted value never changes. Storage keeps the stable ids
// "red|orange|yellow|green|blue|purple|gray" in the :ColorTag ADS, exactly as
// roadmap rule 1 requires. Labels are presentation only: renaming "Red" to
// "Work" rewrites no file and no stream.
//
// User settings live next to the existing ColorTags configuration that
// register.ps1 already writes (PythonPath / ProjectRoot):
//
//   HKCU\Software\ColorTags
//       DisplayMode   REG_DWORD   0 = colored dot, 1 = colored label
//   HKCU\Software\ColorTags\Labels
//       red           REG_SZ      user label; absent or empty = default
//       orange        REG_SZ
//       ...
//
// The registry was chosen over a config file because both consumers already
// read HKCU\Software\ColorTags, it needs no parser in C++, and PowerShell,
// Python and the future tray UI can all edit it without a shared library.
//
// Header-only. Win32 only. No dependencies.

#pragma once

#include <windows.h>

#include <cmath>
#include <cwctype>
#include <string>

namespace colortags {

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

enum class DisplayMode : DWORD {
    Dot = 0,    // small colored circle over Explorer's monochrome glyph
    Label = 1,  // colored badge carrying the tag's label
};

struct TagInfo {
    const wchar_t* id;            // stable storage id, never changes
    COLORREF color;               // sampled from the real menu PNG resources
    const wchar_t* defaultLabel;  // shown until the user renames the tag
    int iconId;                   // resource id in ColorTagsMenu.dll
};

// The palette the user picked: pastel fills meant to be read with dark text on
// them, which is what TextColorFor works out. This is the one place the values
// are allowed to live. The .ico resources in ColorTagsMenu.dll still carry the
// older, more saturated colours, so the menu icons and the badges do not match
// until those are regenerated from these values.
inline constexpr size_t kTagCount = 7;

inline const TagInfo* AllTags() {
    static const TagInfo tags[kTagCount] = {
        {L"red",    RGB(255, 153, 170), L"Red",    101},
        {L"orange", RGB(255, 180,  94), L"Orange", 102},
        {L"yellow", RGB(255, 234, 127), L"Yellow", 103},
        {L"green",  RGB(115, 229, 130), L"Green",  104},
        {L"blue",   RGB(127, 206, 255), L"Blue",   105},
        {L"purple", RGB(222, 148, 255), L"Purple", 106},
        {L"gray",   RGB(181, 176, 184), L"Gray",   107},
    };
    return tags;
}

// ---------------------------------------------------------------------------
// Small string helpers (deliberately local: this header must not depend on
// anything a consumer happens to define)
// ---------------------------------------------------------------------------

inline std::wstring ToLower(std::wstring value) {
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(std::towlower(ch));
    return value;
}

inline std::wstring TrimSpace(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

// A label is shown in a menu item, a column cell and a badge. Control
// characters would corrupt all three, and an unbounded string would push the
// badge past any sensible column width.
inline constexpr size_t kMaxLabelLength = 24;

inline std::wstring SanitizeLabel(std::wstring value) {
    std::wstring clean;
    clean.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch < 0x20 || ch == 0x7F) continue;
        clean.push_back(ch);
    }
    clean = TrimSpace(std::move(clean));
    if (clean.size() > kMaxLabelLength) clean.resize(kMaxLabelLength);
    return clean;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

inline const TagInfo* FindTag(const std::wstring& id) {
    const std::wstring key = ToLower(TrimSpace(id));
    if (key.empty()) return nullptr;
    const TagInfo* tags = AllTags();
    for (size_t index = 0; index < kTagCount; ++index) {
        if (key == tags[index].id) return &tags[index];
    }
    return nullptr;
}

// CLR_INVALID for an unknown or empty id, matching the overlay's existing
// "no indicator" contract.
inline COLORREF ColorOf(const std::wstring& id) {
    const TagInfo* tag = FindTag(id);
    return tag ? tag->color : CLR_INVALID;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

inline const wchar_t* kConfigKey = L"Software\\ColorTags";
inline const wchar_t* kLabelsKey = L"Software\\ColorTags\\Labels";

inline std::wstring ReadRegString(const wchar_t* subkey, const wchar_t* name) {
    std::wstring result;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return result;
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_SZ && size > sizeof(wchar_t)) {
        result.resize(size / sizeof(wchar_t));
        DWORD actual = size;
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&result[0]),
                             &actual) != ERROR_SUCCESS) {
            result.clear();
        }
        while (!result.empty() && result.back() == L'\0') result.pop_back();
    }
    RegCloseKey(key);
    return result;
}

inline bool WriteRegString(const wchar_t* subkey, const wchar_t* name,
                           const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

inline DWORD ReadRegDword(const wchar_t* subkey, const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD type = 0;
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return fallback;
    }
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value)) {
        value = fallback;
    }
    RegCloseKey(key);
    return value;
}

inline bool WriteRegDword(const wchar_t* subkey, const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// The user's label for a tag, or its default when unset. Never returns empty
// for a known id, so a caller can hand the result straight to a menu item.
inline std::wstring LabelOf(const std::wstring& id) {
    const TagInfo* tag = FindTag(id);
    if (!tag) return {};
    const std::wstring stored = SanitizeLabel(ReadRegString(kLabelsKey, tag->id));
    return stored.empty() ? std::wstring(tag->defaultLabel) : stored;
}

inline bool SetLabel(const std::wstring& id, const std::wstring& label) {
    const TagInfo* tag = FindTag(id);
    if (!tag) return false;
    // An empty label restores the default rather than blanking the menu item.
    return WriteRegString(kLabelsKey, tag->id, SanitizeLabel(label));
}

inline DisplayMode CurrentDisplayMode() {
    const DWORD value = ReadRegDword(kConfigKey, L"DisplayMode",
                                     static_cast<DWORD>(DisplayMode::Dot));
    return value == static_cast<DWORD>(DisplayMode::Label) ? DisplayMode::Label
                                                           : DisplayMode::Dot;
}

inline bool SetDisplayMode(DisplayMode mode) {
    return WriteRegDword(kConfigKey, L"DisplayMode", static_cast<DWORD>(mode));
}

// ---------------------------------------------------------------------------
// Contrast
// ---------------------------------------------------------------------------

inline double SrgbChannel(BYTE value) {
    const double c = value / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double RelativeLuminance(COLORREF color) {
    return 0.2126 * SrgbChannel(GetRValue(color)) +
           0.7152 * SrgbChannel(GetGValue(color)) +
           0.0722 * SrgbChannel(GetBValue(color));
}

// Black or white, whichever has the higher WCAG contrast ratio against the
// badge color. Computed rather than hardcoded so the choice stays correct if
// the palette is ever re-sampled from new icons.
inline COLORREF TextColorFor(COLORREF background) {
    const double luminance = RelativeLuminance(background);
    const double againstWhite = 1.05 / (luminance + 0.05);
    const double againstBlack = (luminance + 0.05) / 0.05;
    return againstBlack >= againstWhite ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

}  // namespace colortags
