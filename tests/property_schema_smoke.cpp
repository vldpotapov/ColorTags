#include <windows.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlwapi.h>

#include <cstdio>
#include <string>
#include <vector>

int wmain(int argc, wchar_t **argv) {
    if (argc != 5) return 1;
    PROPERTYKEY expectedKey = {};
    if (FAILED(CLSIDFromString(argv[2], &expectedKey.fmtid))) return 1;
    expectedKey.pid = 2;
    PROPDESC_DISPLAYTYPE expectedDisplay =
        wcscmp(argv[3], L"Enumeration") == 0 ? PDDT_ENUMERATED : PDDT_STRING;
    bool checkImages = _wtoi(argv[4]) != 0;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 2;

    IPropertyDescription *description = nullptr;
    hr = PSGetPropertyDescriptionByName(argv[1], IID_PPV_ARGS(&description));
    if (FAILED(hr)) {
        fwprintf(stderr, L"PSGetPropertyDescriptionByName(%ls) failed: 0x%08lX\n",
                 argv[1], static_cast<unsigned long>(hr));
        hr = PSGetPropertyDescription(expectedKey, IID_PPV_ARGS(&description));
    }
    if (FAILED(hr)) {
        fwprintf(stderr, L"PSGetPropertyDescription(PKEY) failed: 0x%08lX\n",
                 static_cast<unsigned long>(hr));
        CoUninitialize();
        return 3;
    }

    PROPERTYKEY key = {};
    PROPDESC_DISPLAYTYPE displayType = PDDT_STRING;
    hr = description->GetPropertyKey(&key);
    if (SUCCEEDED(hr)) hr = description->GetDisplayType(&displayType);
    bool valid = SUCCEEDED(hr) && IsEqualPropertyKey(key, expectedKey) &&
                 displayType == expectedDisplay;

    IPropertyDescription2 *description2 = nullptr;
    if (valid && checkImages)
        valid = SUCCEEDED(description->QueryInterface(IID_PPV_ARGS(&description2)));

    const wchar_t *ids[] = {L"red",   L"orange", L"yellow", L"green",
                            L"blue",  L"purple", L"gray"};
    for (int index = 0; valid && checkImages && index < 7; index++) {
        PROPVARIANT value;
        PropVariantInit(&value);
        hr = InitPropVariantFromString(ids[index], &value);
        LPWSTR imageReference = nullptr;
        if (SUCCEEDED(hr))
            hr = description2->GetImageReferenceForValue(value, &imageReference);
        std::wstring expected = L",-" + std::to_wstring(101 + index);
        std::wstring actual = imageReference ? imageReference : L"";
        valid = SUCCEEDED(hr) && actual.size() >= expected.size() &&
                actual.compare(actual.size() - expected.size(), expected.size(),
                               expected) == 0;
        if (valid) {
            std::vector<wchar_t> modulePath(actual.begin(), actual.end());
            modulePath.push_back(L'\0');
            int resourceIndex = PathParseIconLocationW(modulePath.data());
            HMODULE resourceModule = LoadLibraryExW(
                modulePath.data(), nullptr,
                LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            HICON icon = resourceModule
                             ? static_cast<HICON>(LoadImageW(
                                   resourceModule,
                                   MAKEINTRESOURCEW(abs(resourceIndex)), IMAGE_ICON,
                                   16, 16, LR_DEFAULTCOLOR))
                             : nullptr;
            valid = resourceIndex < 0 && icon != nullptr;
            if (icon) DestroyIcon(icon);
            if (resourceModule) FreeLibrary(resourceModule);
        }
        CoTaskMemFree(imageReference);
        PropVariantClear(&value);
    }

    if (description2) description2->Release();
    description->Release();
    CoUninitialize();

    if (!valid) return 4;
    wprintf(L"Property schema smoke test OK: %ls\n", argv[1]);
    return 0;
}
