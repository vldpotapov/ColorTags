#include <windows.h>
#include <exdisp.h>
#include <propkey.h>
#include <propsys.h>
#include <servprov.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <vector>

static bool SamePath(PCWSTR left, PCWSTR right) {
    wchar_t leftFull[MAX_PATH] = {};
    wchar_t rightFull[MAX_PATH] = {};
    return GetFullPathNameW(left, MAX_PATH, leftFull, nullptr) &&
           GetFullPathNameW(right, MAX_PATH, rightFull, nullptr) &&
           _wcsicmp(leftFull, rightFull) == 0;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 4 || (_wcsicmp(argv[3], L"add") != 0 &&
                      _wcsicmp(argv[3], L"remove") != 0 &&
                      _wcsicmp(argv[3], L"list") != 0)) {
        fwprintf(stderr,
                 L"usage: set_explorer_column <folder> <canonical> <add|remove|list>\n");
        return 2;
    }
    // "list" reports what Explorer currently considers visible. A column can be
    // accepted into that set and still never appear on screen, and only the two
    // answers together say which of those happened.
    const bool listOnly = _wcsicmp(argv[3], L"list") == 0;
    const bool add = _wcsicmp(argv[3], L"add") == 0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 3;

    PROPERTYKEY requested = {};
    hr = listOnly ? S_OK : PSGetPropertyKeyFromName(argv[2], &requested);
    if (FAILED(hr)) {
        fwprintf(stderr, L"Unknown property %ls: 0x%08lX\n", argv[2],
                 static_cast<unsigned long>(hr));
        CoUninitialize();
        return 4;
    }

    IShellWindows *windows = nullptr;
    hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                          IID_PPV_ARGS(&windows));
    if (FAILED(hr)) {
        CoUninitialize();
        return 5;
    }

    long count = 0;
    windows->get_Count(&count);
    int result = 6;
    for (long index = 0; index < count && result == 6; ++index) {
        VARIANT itemIndex;
        VariantInit(&itemIndex);
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;
        IDispatch *dispatch = nullptr;
        if (FAILED(windows->Item(itemIndex, &dispatch)) || !dispatch) continue;

        IWebBrowserApp *browserApp = nullptr;
        if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(&browserApp)))) {
            dispatch->Release();
            continue;
        }
        dispatch->Release();

        BSTR url = nullptr;
        browserApp->get_LocationURL(&url);
        wchar_t path[MAX_PATH] = {};
        DWORD pathLength = MAX_PATH;
        bool matches = url && SUCCEEDED(PathCreateFromUrlW(url, path, &pathLength, 0)) &&
                       SamePath(path, argv[1]);
        SysFreeString(url);
        if (!matches) {
            browserApp->Release();
            continue;
        }

        IServiceProvider *provider = nullptr;
        IShellBrowser *shellBrowser = nullptr;
        IShellView *view = nullptr;
        IColumnManager *columns = nullptr;
        hr = browserApp->QueryInterface(IID_PPV_ARGS(&provider));
        if (SUCCEEDED(hr))
            hr = provider->QueryService(SID_STopLevelBrowser,
                                        IID_PPV_ARGS(&shellBrowser));
        if (SUCCEEDED(hr)) hr = shellBrowser->QueryActiveShellView(&view);
        if (SUCCEEDED(hr)) hr = view->QueryInterface(IID_PPV_ARGS(&columns));
        if (SUCCEEDED(hr)) {
            UINT visibleCount = 0;
            hr = columns->GetColumnCount(CM_ENUM_VISIBLE, &visibleCount);
            std::vector<PROPERTYKEY> keys(visibleCount + 1);
            if (SUCCEEDED(hr) && visibleCount)
                hr = columns->GetColumns(CM_ENUM_VISIBLE, keys.data(), visibleCount);
            if (listOnly) {
                wprintf(L"Visible columns (%u):\n", visibleCount);
                for (UINT i = 0; SUCCEEDED(hr) && i < visibleCount; ++i) {
                    PWSTR name = nullptr;
                    CM_COLUMNINFO info = {};
                    info.cbSize = sizeof(info);
                    info.dwMask = CM_MASK_WIDTH | CM_MASK_STATE;
                    const HRESULT infoResult = columns->GetColumnInfo(keys[i], &info);
                    if (SUCCEEDED(PSGetNameFromPropertyKey(keys[i], &name)) && name) {
                        wprintf(L"  %2u. %-46ls width=%u state=0x%08lX%ls\n", i + 1, name,
                                SUCCEEDED(infoResult) ? info.uWidth : 0u,
                                SUCCEEDED(infoResult)
                                    ? static_cast<unsigned long>(info.dwState) : 0ul,
                                SUCCEEDED(infoResult) ? L"" : L"  (no column info)");
                        CoTaskMemFree(name);
                    } else {
                        wprintf(L"  %2u. <unnamed property>\n", i + 1);
                    }
                }
                result = 0;
                columns->Release();
                view->Release();
                shellBrowser->Release();
                provider->Release();
                browserApp->Release();
                break;
            }
            bool present = false;
            for (UINT i = 0; SUCCEEDED(hr) && i < visibleCount; ++i)
                present = present || IsEqualPropertyKey(keys[i], requested);
            if (SUCCEEDED(hr) && add && !present) {
                keys[visibleCount++] = requested;
                hr = columns->SetColumns(keys.data(), visibleCount);
            } else if (SUCCEEDED(hr) && !add && present) {
                UINT writeIndex = 0;
                for (UINT readIndex = 0; readIndex < visibleCount; ++readIndex) {
                    if (!IsEqualPropertyKey(keys[readIndex], requested))
                        keys[writeIndex++] = keys[readIndex];
                }
                visibleCount = writeIndex;
                hr = columns->SetColumns(keys.data(), visibleCount);
            }
            if (SUCCEEDED(hr)) {
                UINT confirmedCount = 0;
                hr = columns->GetColumnCount(CM_ENUM_VISIBLE, &confirmedCount);
                std::vector<PROPERTYKEY> confirmed(confirmedCount);
                if (SUCCEEDED(hr) && confirmedCount)
                    hr = columns->GetColumns(CM_ENUM_VISIBLE, confirmed.data(),
                                             confirmedCount);
                bool confirmedPresent = false;
                for (UINT i = 0; SUCCEEDED(hr) && i < confirmedCount; ++i)
                    confirmedPresent = confirmedPresent ||
                                       IsEqualPropertyKey(confirmed[i], requested);
                if (SUCCEEDED(hr) && confirmedPresent == add) result = 0;
            }
        }

        if (columns) columns->Release();
        if (view) view->Release();
        if (shellBrowser) shellBrowser->Release();
        if (provider) provider->Release();
        browserApp->Release();
    }

    windows->Release();
    CoUninitialize();
    if (result == 0 && !listOnly)
        wprintf(L"Visible Explorer column %ls: %ls\n",
                add ? L"added" : L"removed", argv[2]);
    return result;
}
