#include <windows.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>

#include <cstdio>
#include <cstring>
#include <string>

static const CLSID CLSID_TagsProperty = {
    0xC9E3056C, 0x1843, 0x4196, {0xA7, 0x16, 0x83, 0xD5, 0xB7, 0x7F, 0x83, 0x12}};
static const CLSID CLSID_TagsMenu = {
    0x111B2250, 0x529E, 0x4194, {0xAE, 0xE7, 0x5D, 0xE3, 0xE7, 0xEA, 0xAC, 0x5C}};
static const PROPERTYKEY PKEY_ColorTags_ColorString = {
    {0x828A77DC, 0x07B7, 0x4624, {0x9B, 0xE0, 0x25, 0x32, 0x54, 0xF6, 0x8A, 0x76}},
    2};
static const PROPERTYKEY PKEY_ColorTags_ColorEnum = {
    {0x6BE2AA1A, 0x59B2, 0x423D, {0xBA, 0xCF, 0x21, 0xF5, 0x77, 0x02, 0x8C, 0xCD}},
    2};
static const PROPERTYKEY PKEY_ColorTags_ColorIcon = {
    {0xEB41E2DC, 0xD0F2, 0x40D3, {0xA4, 0x98, 0x01, 0x6A, 0x61, 0x14, 0x7F, 0xE5}},
    2};

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, LPVOID *);
using DllCanUnloadNowFn = HRESULT(STDAPICALLTYPE *)();

static bool WriteAds(const std::wstring &path, const char *value) {
    HANDLE file = CreateFileW((path + L":ColorTag").c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, value, static_cast<DWORD>(strlen(value)), &written,
                        nullptr);
    CloseHandle(file);
    return ok && written == strlen(value);
}

static std::string ReadAds(const std::wstring &path) {
    HANDLE file = CreateFileW((path + L":ColorTag").c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char buffer[64] = {};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
    CloseHandle(file);
    return std::string(buffer, read);
}

static HRESULT CreateStore(IClassFactory *factory, const std::wstring &path,
                           IPropertyStore **store) {
    *store = nullptr;
    IInitializeWithFile *initialize = nullptr;
    HRESULT hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&initialize));
    if (FAILED(hr)) return hr;
    hr = initialize->Initialize(path.c_str(), STGM_READWRITE);
    if (SUCCEEDED(hr)) hr = initialize->QueryInterface(IID_PPV_ARGS(store));
    initialize->Release();
    return hr;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        fwprintf(stderr, L"usage: property_store_smoke.exe <ColorTagsMenu.dll>\n");
        return 2;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 3;

    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) {
        fwprintf(stderr, L"LoadLibrary failed: %lu\n", GetLastError());
        CoUninitialize();
        return 4;
    }
    auto getClassObject = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<DllCanUnloadNowFn>(
        GetProcAddress(module, "DllCanUnloadNow"));
    if (!getClassObject || !canUnload) return 5;

    IClassFactory *factory = nullptr;
    hr = getClassObject(CLSID_TagsProperty, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return 6;
    if (canUnload() != S_FALSE) return 7;

    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    if (!GetTempFileNameW(tempPath, L"ctg", 0, tempFile)) return 8;
    std::wstring path = tempFile;

    struct Case {
        const char *id;
        const wchar_t *emoji;
    } cases[] = {{"red", L"🔴"},       {"orange", L"🟠"},
                 {"yellow", L"🟡"},   {"green", L"🟢"},
                 {"blue", L"🔵"},     {"purple", L"🟣"},
                 {"gray", L"⚪"}};

    int result = 0;
    for (const auto &test : cases) {
        if (!WriteAds(path, test.id)) {
            result = 9;
            break;
        }
        IPropertyStore *store = nullptr;
        hr = CreateStore(factory, path, &store);
        if (FAILED(hr)) {
            result = 10;
            break;
        }
        PROPVARIANT value;
        PropVariantInit(&value);
        hr = store->GetValue(PKEY_Keywords, &value);
        bool valid = SUCCEEDED(hr) && value.vt == (VT_VECTOR | VT_LPWSTR) &&
                     value.calpwstr.cElems == 1 && value.calpwstr.pElems &&
                     value.calpwstr.pElems[0] &&
                     wcscmp(value.calpwstr.pElems[0], test.emoji) == 0;
        PropVariantClear(&value);
        std::wstring expectedId(test.id, test.id + strlen(test.id));
        const PROPERTYKEY customKeys[] = {PKEY_ColorTags_ColorString,
                                          PKEY_ColorTags_ColorEnum,
                                          PKEY_ColorTags_ColorIcon};
        for (const auto &customKey : customKeys) {
            PropVariantInit(&value);
            hr = store->GetValue(customKey, &value);
            valid = valid && SUCCEEDED(hr) && value.vt == VT_LPWSTR &&
                    value.pwszVal &&
                    wcscmp(value.pwszVal, expectedId.c_str()) == 0;
            PropVariantClear(&value);
        }
        store->Release();
        if (!valid) {
            result = 11;
            break;
        }
    }

    if (result == 0) {
        IPropertyStore *store = nullptr;
        hr = CreateStore(factory, path, &store);
        if (FAILED(hr)) {
            result = 12;
        } else {
            PCWSTR input[] = {L"🟣"};
            PROPVARIANT value;
            PropVariantInit(&value);
            hr = InitPropVariantFromStringVector(input, 1, &value);
            if (SUCCEEDED(hr)) hr = store->SetValue(PKEY_Keywords, value);
            if (SUCCEEDED(hr)) hr = store->Commit();
            PropVariantClear(&value);
            store->Release();
            if (FAILED(hr) || ReadAds(path) != "purple") result = 13;
        }
    }

    DeleteFileW(path.c_str());
    factory->Release();

    if (result == 0) {
        IClassFactory *menuFactory = nullptr;
        hr = getClassObject(CLSID_TagsMenu, IID_PPV_ARGS(&menuFactory));
        if (FAILED(hr)) {
            result = 14;
        } else {
            IExplorerCommand *menu = nullptr;
            hr = menuFactory->CreateInstance(nullptr, IID_PPV_ARGS(&menu));
            if (FAILED(hr)) {
                result = 15;
            } else {
                LPWSTR iconReference = nullptr;
                hr = menu->GetIcon(nullptr, &iconReference);
                const wchar_t *comma = iconReference
                                           ? wcsrchr(iconReference, L',')
                                           : nullptr;
                long resource = comma ? wcstol(comma + 1, nullptr, 10) : 0;
                int resourceId = static_cast<int>(-resource);
                HICON icon = resourceId == 109 || resourceId == 110
                                 ? static_cast<HICON>(LoadImageW(
                                       module, MAKEINTRESOURCEW(resourceId),
                                       IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR))
                                 : nullptr;
                if (FAILED(hr) || !iconReference || !icon) result = 16;
                if (icon) DestroyIcon(icon);
                for (int checkedId = 201; result == 0 && checkedId <= 207;
                     checkedId++) {
                    HICON checked = static_cast<HICON>(LoadImageW(
                        module, MAKEINTRESOURCEW(checkedId), IMAGE_ICON, 16, 16,
                        LR_DEFAULTCOLOR));
                    if (!checked)
                        result = 17;
                    else
                        DestroyIcon(checked);
                }
                CoTaskMemFree(iconReference);
                menu->Release();
            }
            menuFactory->Release();
        }
    }

    if (result == 0 && canUnload() != S_OK) result = 18;
    FreeLibrary(module);
    CoUninitialize();

    if (result == 0)
        printf("Shell smoke test OK: properties + themed menu icon + 7 checked icons\n");
    return result;
}
