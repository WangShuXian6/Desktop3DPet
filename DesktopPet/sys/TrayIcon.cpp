#include "pch.h"
#include "TrayIcon.h"

bool TrayIcon::Create(HINSTANCE instance, std::wstring tooltip, TrayCallbacks callbacks)
{
    m_callbacks = std::move(callbacks);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &TrayIcon::StaticWndProc;
    wc.lpszClassName = L"DesktopPet.TrayMessageWindow";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"DesktopPetTray", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!m_hwnd) return false;

    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = ID_TRAY;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY;
    m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);

    return Shell_NotifyIconW(NIM_ADD, &m_nid) == TRUE;
}

void TrayIcon::Remove()
{
    if (m_nid.hWnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_nid = {};
    }
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

LRESULT CALLBACK TrayIcon::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TrayIcon* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<TrayIcon*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayIcon::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TRAY)
    {
        if (LOWORD(lp) == WM_RBUTTONUP)
        {
            ShowMenu();
            return 0;
        }
        if (LOWORD(lp) == WM_LBUTTONDBLCLK)
        {
            if (m_callbacks.openControlCenter) m_callbacks.openControlCenter();
            return 0;
        }
    }

    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case CMD_SHOW:
            if (m_callbacks.showPet) m_callbacks.showPet();
            return 0;
        case CMD_HIDE:
            if (m_callbacks.hidePet) m_callbacks.hidePet();
            return 0;
        case CMD_WAVE:
            if (m_callbacks.wave) m_callbacks.wave();
            return 0;
        case CMD_OPEN:
            if (m_callbacks.openControlCenter) m_callbacks.openControlCenter();
            return 0;
        case CMD_EXIT:
            if (m_callbacks.exitApp) m_callbacks.exitApp();
            return 0;
        }
        break;
    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void TrayIcon::ShowMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CMD_SHOW, L"显示宠物");
    AppendMenuW(menu, MF_STRING, CMD_HIDE, L"隐藏宠物");
    AppendMenuW(menu, MF_STRING, CMD_WAVE, L"挥手");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_OPEN, L"打开控制中心");
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"退出");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}
