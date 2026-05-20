#pragma once

struct TrayCallbacks
{
    std::function<void()> showPet;
    std::function<void()> hidePet;
    std::function<void()> wave;
    std::function<void()> openControlCenter;
    std::function<void()> exitApp;
};

class TrayIcon
{
public:
    bool Create(HINSTANCE instance, std::wstring tooltip, TrayCallbacks callbacks);
    void Remove();
    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void ShowMenu();

    static constexpr UINT WM_TRAY = WM_APP + 100;
    static constexpr UINT ID_TRAY = 1;
    static constexpr UINT CMD_SHOW = 101;
    static constexpr UINT CMD_HIDE = 102;
    static constexpr UINT CMD_WAVE = 103;
    static constexpr UINT CMD_OPEN = 104;
    static constexpr UINT CMD_EXIT = 105;

    HWND m_hwnd{};
    NOTIFYICONDATAW m_nid{};
    TrayCallbacks m_callbacks{};
};
