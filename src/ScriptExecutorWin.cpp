// ============================================================================
// ScriptExecutorWin.cpp
// Windows: dock folder location + running scripts through Illustrator's own
// automation interface (COM "Illustrator.Application" -> DoJavaScriptFile).
// This is the same mechanism v1 used, which is proven to work in AI 2027.
// ============================================================================

#ifdef _WIN32

#include "ScriptExecutor.h"
#include "LaserScriptDockID.h"

#include <windows.h>
#include <oleauto.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0)
        return L"";
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], len);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return "";
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &utf8[0], len, nullptr, nullptr);
    return utf8;
}

fs::path PluginDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PluginDirectory),
        &module);

    std::wstring buffer(32768, L'\0');
    DWORD len = GetModuleFileNameW(module, &buffer[0], static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    return fs::path(buffer).parent_path();
}

fs::path DocumentsDirectory()
{
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw)) && raw)
        result = raw;
    if (raw)
        CoTaskMemFree(raw);

    if (result.empty())
    {
        wchar_t profile[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
            result = fs::path(profile) / L"Documents";
    }
    return result;
}

void FreeExcepInfo(EXCEPINFO& e)
{
    if (e.bstrDescription) SysFreeString(e.bstrDescription);
    if (e.bstrSource)      SysFreeString(e.bstrSource);
    if (e.bstrHelpFile)    SysFreeString(e.bstrHelpFile);
    e.bstrDescription = e.bstrSource = e.bstrHelpFile = nullptr;
}

// Runs on a worker thread. COM marshals the call onto Illustrator's main
// thread, which is free because the button click has already returned.
bool InvokeDoJavaScriptFile(const std::wstring& scriptPath, std::string& outError)
{
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(hrInit);
    bool ok = false;

    IUnknown*  unknown = nullptr;
    IDispatch* app = nullptr;

    do
    {
        CLSID clsid {};
        HRESULT hr = CLSIDFromProgID(L"Illustrator.Application", &clsid);
        if (FAILED(hr))
        {
            outError = "Illustrator's scripting connection isn't registered (try reinstalling Illustrator).";
            break;
        }

        hr = GetActiveObject(clsid, nullptr, &unknown);
        if (FAILED(hr) || !unknown)
            hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IUnknown, reinterpret_cast<void**>(&unknown));
        if (FAILED(hr) || !unknown)
        {
            outError = "Couldn't connect to Illustrator.";
            break;
        }

        hr = unknown->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
        if (FAILED(hr) || !app)
        {
            outError = "Couldn't talk to Illustrator.";
            break;
        }

        OLECHAR* methodName = const_cast<OLECHAR*>(L"DoJavaScriptFile");
        DISPID dispid = 0;
        hr = app->GetIDsOfNames(IID_NULL, &methodName, 1, LOCALE_USER_DEFAULT, &dispid);
        if (FAILED(hr))
        {
            outError = "Illustrator doesn't offer DoJavaScriptFile.";
            break;
        }

        VARIANT argFile;
        VariantInit(&argFile);
        argFile.vt = VT_BSTR;
        argFile.bstrVal = SysAllocString(scriptPath.c_str());

        DISPPARAMS params {};
        params.rgvarg = &argFile;
        params.cArgs = 1;

        EXCEPINFO excep {};
        VARIANT result;
        VariantInit(&result);

        hr = app->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &result, &excep, nullptr);

        VariantClear(&argFile);
        VariantClear(&result);

        if (FAILED(hr))
        {
            if (excep.bstrDescription && *excep.bstrDescription)
                outError = WideToUtf8(excep.bstrDescription);
            else
                outError = "the script stopped with an error.";
            FreeExcepInfo(excep);
            break;
        }

        FreeExcepInfo(excep);
        ok = true;
    } while (false);

    if (app)     app->Release();
    if (unknown) unknown->Release();
    if (shouldUninit)
        CoUninitialize();
    return ok;
}

} // namespace

namespace ScriptExecutor {

std::string GetDockFolder()
{
    fs::path docs = DocumentsDirectory();
    if (docs.empty())
        return "";
    fs::path dock = docs / Utf8ToWide(kLaserScriptDockFolderName);
    std::error_code ec;
    fs::create_directories(dock, ec);
    return WideToUtf8(dock.wstring());
}

std::string GetSeedFolder()
{
    return WideToUtf8((PluginDirectory() / Utf8ToWide(kLaserScriptDockSeedFolderName)).wstring());
}

bool RunScriptAsync(const std::string& scriptPathUtf8, CompletionCallback onDone, std::string& errorOut)
{
    const std::wstring widePath = Utf8ToWide(scriptPathUtf8);
    std::error_code ec;
    if (widePath.empty() || !fs::is_regular_file(fs::path(widePath), ec))
    {
        errorOut = "script file not found - press Refresh";
        return false;
    }

    try
    {
        std::thread([widePath, onDone]() {
            std::string err;
            bool ok = InvokeDoJavaScriptFile(widePath, err);
            if (onDone)
                onDone(ok, err);
        }).detach();
    }
    catch (...)
    {
        errorOut = "couldn't start the script";
        return false;
    }
    return true;
}

void OpenInShell(const std::string& pathUtf8)
{
    const std::wstring path = Utf8ToWide(pathUtf8);
    if (path.empty())
        return;

    HINSTANCE r = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32)
    {
        // No app is set up for this file type (common for .json): use Notepad.
        ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }
}

} // namespace ScriptExecutor

#endif // _WIN32
