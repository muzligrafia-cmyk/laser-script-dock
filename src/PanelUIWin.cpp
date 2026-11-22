// ============================================================================
// PanelUIWin.cpp
// Windows panel UI: a scrollable list of
// script buttons grouped under headings, and a status line at the bottom.
// Everything is drawn by hand so it follows Illustrator's UI brightness.
// ============================================================================

#ifdef _WIN32

#include "PanelUIController.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"LaserScriptDock2Wnd";
constexpr UINT    WM_LSD_STATUS  = WM_APP + 101; // lParam = StatusPayload*
constexpr UINT    WM_LSD_COMMAND = WM_APP + 102; // wParam = control id

constexpr int kIdRefresh    = 100;
constexpr int kIdOpenFolder = 101;
constexpr int kIdScriptBase = 1000;

// ---- Sizes in "96 dpi pixels" (scaled for the screen) ----------------------
constexpr int kPad            = 12;
constexpr int kListTop        = 6;
constexpr int kStatusH        = 28;
constexpr int kHeaderH        = 22;
constexpr int kButtonH        = 44;  // with description
constexpr int kButtonHNoDesc  = 32;  // title only
constexpr int kButtonGap      = 6;
constexpr int kCategoryGap    = 8;
constexpr int kWheelStep      = 56;

// ---- Font sizes in tenths of a point ----------------------------------------
constexpr int kFontTool   = 90;
constexpr int kFontHeader = 80;
constexpr int kFontTitle  = 90;
constexpr int kFontDesc   = 80;
constexpr int kFontStatus = 80;

HINSTANCE ThisModule()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ThisModule),
        &module);
    return module;
}

COLORREF ToColorRef(uint32_t rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

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

std::wstring ToUpper(std::wstring s)
{
    if (!s.empty())
        CharUpperBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

int DpiForWindow(HWND hwnd)
{
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd)
    {
        UINT dpi = fn(hwnd);
        if (dpi > 0)
            return static_cast<int>(dpi);
    }
    HDC hdc = GetDC(nullptr);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc)
        ReleaseDC(nullptr, hdc);
    return dpi > 0 ? dpi : 96;
}

struct StatusPayload
{
    std::string text;
    bool        isError;
};

class PanelUIWin final : public PanelUIController
{
public:
    ~PanelUIWin() override
    {
        Destroy();
        DestroyFonts();
    }

    bool Create(AIPanelPlatformWindow parentWindow, const PanelThemeColors& theme, const PanelCallbacks& callbacks) override
    {
        fParentHwnd = static_cast<HWND>(parentWindow);
        if (!fParentHwnd)
            return false;

        fTheme = theme;
        fCallbacks = callbacks;
        fModule = ThisModule();
        fDpi = DpiForWindow(fParentHwnd);

        if (!RegisterWindowClass(fModule))
            return false;

        CreateFonts();

        RECT rc {};
        GetClientRect(fParentHwnd, &rc);
        const int width  = (rc.right - rc.left) > 0 ? (rc.right - rc.left) : S(290);
        const int height = (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : S(560);

        // Host window (toolbar + status line). WM_NCCREATE stores its handle.
        HWND host = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                    0, 0, width, height, fParentHwnd, nullptr, fModule, this);
        if (!host)
            return false;

        // Scrollable list area inside the host.
        HWND list = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                    0, 0, width, height, host, nullptr, fModule, this);
        if (!list)
        {
            Destroy();
            return false;
        }

        LayoutHost(width, height);
        return true;
    }

    void Destroy() override
    {
        HWND host = fHostHwnd.exchange(nullptr);
        fListHwnd = nullptr;
        fToolButtons.clear();
        fScriptButtons.clear();
        if (host && IsWindow(host))
            DestroyWindow(host); // also destroys the list and all buttons
    }

    void SetModel(const DockModel& model) override
    {
        fModel = model;

        for (auto& b : fScriptButtons)
        {
            if (b.hwnd)
                DestroyWindow(b.hwnd);
        }
        fScriptButtons.clear();

        if (!fListHwnd)
            return;

        for (size_t i = 0; i < fModel.scripts.size(); ++i)
        {
            const DockScript& s = fModel.scripts[i];
            fScriptButtons.push_back(MakeButton(
                fListHwnd,
                kIdScriptBase + static_cast<int>(i),
                Utf8ToWide(s.name),
                Utf8ToWide(s.description),
                false));
        }

        LayoutList();
        InvalidateRect(fListHwnd, nullptr, TRUE);
    }

    void UpdateTheme(const PanelThemeColors& theme) override
    {
        fTheme = theme;
        RedrawEverything();
    }

    void SetStatusText(const std::string& utf8Status, bool isError) override
    {
        HWND host = fHostHwnd.load();
        if (!host)
            return;
        // Posted, so it's safe from the script worker thread too.
        auto* payload = new StatusPayload { utf8Status, isError };
        if (!PostMessageW(host, WM_LSD_STATUS, 0, reinterpret_cast<LPARAM>(payload)))
            delete payload;
    }

    void Resize(int width, int height) override
    {
        HWND host = fHostHwnd.load();
        if (!host)
            return;

        // Illustrator reports the size in its own units, which can differ from
        // real pixels on scaled screens - so measure the panel window itself.
        RECT rc {};
        if (fParentHwnd && GetClientRect(fParentHwnd, &rc) && rc.right > 0 && rc.bottom > 0)
        {
            width = rc.right - rc.left;
            height = rc.bottom - rc.top;
        }
        SetWindowPos(host, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutHost(width, height);
        InvalidateRect(host, nullptr, TRUE);
    }

private:
    struct Button
    {
        HWND         hwnd = nullptr;
        int          id = 0;
        bool         isTool = false;
        std::wstring title;
        std::wstring description;
        bool         hovered = false;
    };

    // ---- Setup ------------------------------------------------------------------

    int S(int px) const { return MulDiv(px, fDpi, 96); }

    static bool RegisterWindowClass(HINSTANCE module)
    {
        WNDCLASSEXW existing {};
        existing.cbSize = sizeof(existing);
        if (GetClassInfoExW(module, kWindowClass, &existing))
            return true;

        WNDCLASSEXW wc {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = module;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClass;
        return RegisterClassExW(&wc) != 0;
    }

    void CreateFonts()
    {
        DestroyFonts();
        auto make = [this](int tenthsPt, int weight) -> HFONT {
            return CreateFontW(-MulDiv(tenthsPt, fDpi, 720), 0, 0, 0, weight,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        };
        fFontTool   = make(kFontTool, FW_SEMIBOLD);
        fFontHeader = make(kFontHeader, FW_SEMIBOLD);
        fFontTitle  = make(kFontTitle, FW_SEMIBOLD);
        fFontDesc   = make(kFontDesc, FW_NORMAL);
        fFontStatus = make(kFontStatus, FW_NORMAL);
    }

    void DestroyFonts()
    {
        for (HFONT* f : { &fFontTool, &fFontHeader, &fFontTitle, &fFontDesc, &fFontStatus })
        {
            if (*f)
            {
                DeleteObject(*f);
                *f = nullptr;
            }
        }
    }

    Button MakeButton(HWND parent, int id, const std::wstring& title, const std::wstring& description, bool isTool)
    {
        Button b;
        b.id = id;
        b.isTool = isTool;
        b.title = title;
        b.description = description;
        b.hwnd = CreateWindowExW(0, L"BUTTON", title.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, S(100), S(40), parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 fModule, nullptr);
        if (b.hwnd)
            SetWindowSubclass(b.hwnd, ButtonSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        return b;
    }

    Button* FindButton(HWND hwnd)
    {
        for (auto& b : fToolButtons)
            if (b.hwnd == hwnd)
                return &b;
        for (auto& b : fScriptButtons)
            if (b.hwnd == hwnd)
                return &b;
        return nullptr;
    }

    void RedrawEverything()
    {
        if (HWND host = fHostHwnd.load())
            InvalidateRect(host, nullptr, TRUE);
        if (fListHwnd)
            InvalidateRect(fListHwnd, nullptr, TRUE);
        for (auto& b : fToolButtons)
            if (b.hwnd) InvalidateRect(b.hwnd, nullptr, TRUE);
        for (auto& b : fScriptButtons)
            if (b.hwnd) InvalidateRect(b.hwnd, nullptr, TRUE);
    }

    // ---- Layout -----------------------------------------------------------------

    void LayoutHost(int width, int height)
    {
        fHostWidth = width;
        fHostHeight = height;

        const int pad = S(kPad);
        const int innerW = std::max(S(80), width - pad * 2);

        const int listTop = S(kListTop);
        const int statusTop = std::max(listTop + S(40), height - S(kStatusH));
        fStatusRect = { pad, statusTop + S(4), pad + innerW, height - S(2) };

        if (fListHwnd)
        {
            MoveWindow(fListHwnd, 0, listTop, width, std::max(0, statusTop - listTop), TRUE);
            LayoutList();
        }
    }

    void LayoutList()
    {
        if (!fListHwnd)
            return;

        RECT rc {};
        GetClientRect(fListHwnd, &rc);
        const int width = rc.right - rc.left;
        const int viewH = rc.bottom - rc.top;
        const int pad = S(kPad);
        const int innerW = std::max(S(80), width - pad * 2);

        // 1. Measure total content height
        int contentH = S(2);
        for (const auto& cat : fModel.categories)
        {
            contentH += S(kHeaderH);
            for (size_t idx : cat.scripts)
            {
                const bool hasDesc = idx < fModel.scripts.size() && !fModel.scripts[idx].description.empty();
                contentH += S(hasDesc ? kButtonH : kButtonHNoDesc) + S(kButtonGap);
            }
            contentH += S(kCategoryGap);
        }
        fContentH = contentH;
        fViewH = viewH;
        fScroll = std::max(0, std::min(fScroll, std::max(0, fContentH - fViewH)));

        // 2. Place headers and buttons
        fHeaders.clear();
        int y = S(2) - fScroll;
        HDWP batch = BeginDeferWindowPos(static_cast<int>(fScriptButtons.size()));
        for (const auto& cat : fModel.categories)
        {
            fHeaders.push_back({ ToUpper(Utf8ToWide(cat.name)), RECT { pad + S(2), y, pad + innerW, y + S(kHeaderH) } });
            y += S(kHeaderH);
            for (size_t idx : cat.scripts)
            {
                const bool hasDesc = idx < fModel.scripts.size() && !fModel.scripts[idx].description.empty();
                const int h = S(hasDesc ? kButtonH : kButtonHNoDesc);
                if (idx < fScriptButtons.size() && fScriptButtons[idx].hwnd)
                {
                    if (batch)
                        batch = DeferWindowPos(batch, fScriptButtons[idx].hwnd, nullptr, pad, y, innerW, h,
                                               SWP_NOZORDER | SWP_NOACTIVATE);
                    else
                        MoveWindow(fScriptButtons[idx].hwnd, pad, y, innerW, h, TRUE);
                }
                y += h + S(kButtonGap);
            }
            y += S(kCategoryGap);
        }
        if (batch)
            EndDeferWindowPos(batch);

        InvalidateRect(fListHwnd, nullptr, TRUE);
    }

    void ScrollBy(int delta)
    {
        const int maxScroll = std::max(0, fContentH - fViewH);
        const int next = std::max(0, std::min(fScroll + delta, maxScroll));
        if (next != fScroll)
        {
            fScroll = next;
            LayoutList();
        }
    }

    // ---- Painting ---------------------------------------------------------------

    void PaintHost(HDC hdc, HWND hwnd)
    {
        RECT rc {};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(ToColorRef(fTheme.backgroundRGB));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Divider above the status line
        HPEN pen = CreatePen(PS_SOLID, 1, ToColorRef(fTheme.borderRGB));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, fStatusRect.left, fStatusRect.top - S(4), nullptr);
        LineTo(hdc, fStatusRect.right, fStatusRect.top - S(4));
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, fFontStatus);
        COLORREF errorColor = fTheme.isDark ? RGB(255, 120, 110) : RGB(196, 30, 30);
        SetTextColor(hdc, fStatusError ? errorColor : ToColorRef(fTheme.textMutedRGB));
        RECT statusRc = fStatusRect;
        DrawTextW(hdc, fStatusText.c_str(), -1, &statusRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }

    void PaintList(HDC hdc, HWND hwnd)
    {
        RECT rc {};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(ToColorRef(fTheme.backgroundRGB));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);

        if (fModel.scripts.empty())
        {
            HGDIOBJ oldFont = SelectObject(hdc, fFontDesc);
            SetTextColor(hdc, ToColorRef(fTheme.textMutedRGB));
            RECT msg = { S(kPad) + S(2), S(8), rc.right - S(kPad), rc.bottom };
            DrawTextW(hdc,
                      L"No scripts yet.\n\nPut your .jsx or .js files into the folder "
                      L"(\u2261 menu > Open Scripts Folder), then choose Refresh Scripts.",
                      -1, &msg, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            return;
        }

        HGDIOBJ oldFont = SelectObject(hdc, fFontHeader);
        SetTextColor(hdc, ToColorRef(fTheme.textMutedRGB));
        for (auto& h : fHeaders)
        {
            RECT r = h.rect;
            DrawTextW(hdc, h.text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        SelectObject(hdc, oldFont);

        // Slim scroll indicator when the list is taller than the panel
        if (fContentH > fViewH && fViewH > 0)
        {
            const int trackW = S(4);
            const int x = rc.right - trackW - S(2);
            const int thumbH = std::max(S(30), fViewH * fViewH / fContentH);
            const int maxScroll = fContentH - fViewH;
            const int thumbY = (fViewH - thumbH) * fScroll / std::max(1, maxScroll);
            RECT thumb = { x, thumbY, x + trackW, thumbY + thumbH };
            HBRUSH tb = CreateSolidBrush(ToColorRef(fTheme.textMutedRGB));
            FillRect(hdc, &thumb, tb);
            DeleteObject(tb);
        }
    }

    void DrawButton(const DRAWITEMSTRUCT* dis)
    {
        Button* b = FindButton(dis->hwndItem);
        if (!b)
            return;

        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        uint32_t fill = fTheme.surfaceRGB;
        if (pressed)
            fill = fTheme.surfacePressedRGB;
        else if (b->hovered)
            fill = fTheme.surfaceHoverRGB;
        const uint32_t border = b->hovered ? fTheme.accentRGB : fTheme.borderRGB;

        HDC hdc = dis->hDC;
        RECT r = dis->rcItem;

        // Paint the corners in the background colour first
        HBRUSH bg = CreateSolidBrush(ToColorRef(fTheme.backgroundRGB));
        FillRect(hdc, &r, bg);
        DeleteObject(bg);

        HBRUSH fillBrush = CreateSolidBrush(ToColorRef(fill));
        HPEN borderPen = CreatePen(PS_SOLID, 1, ToColorRef(border));
        HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, S(6), S(6));
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(fillBrush);
        DeleteObject(borderPen);

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, b->isTool ? fFontTool : fFontTitle);
        SetTextColor(hdc, ToColorRef(fTheme.textPrimaryRGB));

        if (b->isTool)
        {
            RECT t = r;
            DrawTextW(hdc, b->title.c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        else if (b->description.empty())
        {
            RECT t = { r.left + S(10), r.top, r.right - S(10), r.bottom };
            DrawTextW(hdc, b->title.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        else
        {
            RECT t = { r.left + S(10), r.top + S(5), r.right - S(10), r.top + S(23) };
            DrawTextW(hdc, b->title.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            SelectObject(hdc, fFontDesc);
            SetTextColor(hdc, ToColorRef(fTheme.textMutedRGB));
            RECT d = { r.left + S(10), r.top + S(22), r.right - S(10), r.bottom - S(5) };
            DrawTextW(hdc, b->description.c_str(), -1, &d,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        SelectObject(hdc, oldFont);
    }

    // ---- Messages -----------------------------------------------------------------

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        PanelUIWin* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<PanelUIWin*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            // The host is created first, then the list.
            if (self)
            {
                if (!self->fHostHwnd.load())
                    self->fHostHwnd = hwnd;
                else if (!self->fListHwnd)
                    self->fListHwnd = hwnd;
            }
        }
        else
        {
            self = reinterpret_cast<PanelUIWin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (msg == WM_NCDESTROY)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (self && self->fListHwnd == hwnd)
                self->fListHwnd = nullptr;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (self)
            return self->HandleMessage(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        const bool isList = (hwnd == fListHwnd);

        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1; // painted in WM_PAINT

        case WM_PAINT:
        {
            PAINTSTRUCT ps {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (isList)
                PaintList(hdc, hwnd);
            else
                PaintHost(hdc, hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            if (isList)
                LayoutList();
            else if (hwnd == fHostHwnd.load())
                LayoutHost(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_MOUSEWHEEL:
            if (isList)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                ScrollBy(-delta * S(kWheelStep) / WHEEL_DELTA);
                return 0;
            }
            break;

        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON)
            {
                DrawButton(dis);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                // Handle it a moment later, outside the button's own click
                // handling, so a Refresh can safely rebuild the buttons.
                if (HWND host = fHostHwnd.load())
                    PostMessageW(host, WM_LSD_COMMAND, LOWORD(wParam), 0);
                return 0;
            }
            break;

        case WM_LSD_COMMAND:
            HandleCommand(static_cast<int>(wParam));
            return 0;

        case WM_LSD_STATUS:
        {
            auto* payload = reinterpret_cast<StatusPayload*>(lParam);
            if (payload)
            {
                fStatusText = Utf8ToWide(payload->text);
                fStatusError = payload->isError;
                delete payload;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void HandleCommand(int id)
    {
        // Give keyboard focus back to wherever it was (usually the artboard),
        // so Illustrator's shortcuts keep working after clicking a button.
        HWND giveBack = fFocusBeforeClick;
        fFocusBeforeClick = nullptr;

        if (id == kIdRefresh)
        {
            if (fCallbacks.onRefresh)
                fCallbacks.onRefresh();
        }
        else if (id == kIdOpenFolder)
        {
            if (fCallbacks.onOpenFolder)
                fCallbacks.onOpenFolder();
        }
        else if (id >= kIdScriptBase)
        {
            const size_t index = static_cast<size_t>(id - kIdScriptBase);
            if (index < fModel.scripts.size() && fCallbacks.onRunScript)
                fCallbacks.onRunScript(index);
        }

        if (giveBack && IsWindow(giveBack) && !FindButton(giveBack))
            SetFocus(giveBack);
    }

    static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR subclassId, DWORD_PTR refData)
    {
        auto* self = reinterpret_cast<PanelUIWin*>(refData);
        if (self)
        {
            switch (msg)
            {
            case WM_LBUTTONDOWN:
                self->fFocusBeforeClick = GetFocus();
                break;
            case WM_MOUSEMOVE:
                if (Button* b = self->FindButton(hwnd); b && !b->hovered)
                {
                    b->hovered = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    TRACKMOUSEEVENT tme { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                    TrackMouseEvent(&tme);
                }
                break;
            case WM_MOUSELEAVE:
                if (Button* b = self->FindButton(hwnd); b && b->hovered)
                {
                    b->hovered = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, ButtonSubclassProc, subclassId);
                break;
            }
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // ---- State ------------------------------------------------------------------

    struct Header
    {
        std::wstring text;
        RECT         rect;
    };

    HWND                fParentHwnd = nullptr;
    std::atomic<HWND>   fHostHwnd { nullptr };
    HWND                fListHwnd = nullptr;
    HINSTANCE           fModule = nullptr;
    int                 fDpi = 96;

    PanelThemeColors    fTheme {};
    PanelCallbacks      fCallbacks;
    DockModel           fModel;

    std::vector<Button> fToolButtons;
    std::vector<Button> fScriptButtons; // same order as fModel.scripts
    std::vector<Header> fHeaders;

    HFONT fFontTool = nullptr;
    HFONT fFontHeader = nullptr;
    HFONT fFontTitle = nullptr;
    HFONT fFontDesc = nullptr;
    HFONT fFontStatus = nullptr;

    int  fHostWidth = 0;
    int  fHostHeight = 0;
    int  fContentH = 0;
    int  fViewH = 0;
    int  fScroll = 0;
    RECT fStatusRect {};

    std::wstring fStatusText = L"Ready";
    bool         fStatusError = false;
    HWND         fFocusBeforeClick = nullptr;
};

} // namespace

std::shared_ptr<PanelUIController> PanelUIController::CreatePlatformController()
{
    return std::make_shared<PanelUIWin>();
}

#endif // _WIN32
