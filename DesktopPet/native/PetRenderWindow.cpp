#include "pch.h"
#include "PetRenderWindow.h"

namespace
{
    constexpr int kMinPetCanvasSize = 520;

    void ClampWindowToWorkArea(int& x, int& y, int width, int height)
    {
        POINT anchor{ x, y };
        HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info))
        {
            return;
        }

        RECT const& work = info.rcWork;
        const int workLeft = static_cast<int>(work.left);
        const int workTop = static_cast<int>(work.top);
        const int workRight = static_cast<int>(work.right);
        const int workBottom = static_cast<int>(work.bottom);
        const int maxX = std::max(workLeft, workRight - width);
        const int maxY = std::max(workTop, workBottom - height);
        x = std::clamp(x, workLeft, maxX);
        y = std::clamp(y, workTop, maxY);
    }
}

bool PetRenderWindow::Create(HINSTANCE instance, int x, int y, int width, int height)
{
    m_baseWidth = std::max(kMinPetCanvasSize, width);
    m_baseHeight = std::max(kMinPetCanvasSize, height);
    ClampWindowToWorkArea(x, y, m_baseWidth, m_baseHeight);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpszClassName = L"DesktopPet.NativePetWindow";
    wc.lpfnWndProc = &PetRenderWindow::StaticWndProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName,
        L"DesktopPetWindow",
        WS_POPUP,
        x,
        y,
        m_baseWidth,
        m_baseHeight,
        nullptr,
        nullptr,
        instance,
        this);

    if (!m_hwnd) return false;

    // MVP 使用黑色 color key 演示透明窗口。真实产品建议替换为 DirectComposition per-pixel alpha。
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY);

    if (!m_renderer.Initialize(m_hwnd, m_baseWidth, m_baseHeight))
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    return true;
}

void PetRenderWindow::Show()
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void PetRenderWindow::Hide()
{
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

void PetRenderWindow::Destroy()
{
    m_renderer.Destroy();
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void PetRenderWindow::Tick(float dt)
{
    m_animation.Update(dt);
    m_renderer.Tick(dt);
    PoseParams pose = m_animation.CurrentPose();
    pose.scale *= m_scale;
    m_renderer.Render(pose);
}

void PetRenderWindow::TriggerWave()
{
    m_animation.RequestMontage("wave");
}

void PetRenderWindow::SetScale(float scale)
{
    m_scale = std::clamp(scale, 0.1f, 2.0f);
}

void PetRenderWindow::SetAlwaysOnTop(bool top)
{
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool PetRenderWindow::SetModelPath(std::filesystem::path const& path)
{
    return m_renderer.SetModelPath(path);
}

void PetRenderWindow::SetModelRotation(float pitchDegrees, float yawDegrees, float rollDegrees)
{
    m_renderer.SetModelRotation(pitchDegrees, yawDegrees, rollDegrees);
}

void PetRenderWindow::SetMaterialSettings(
    float brightness,
    float contrast,
    float saturation,
    float ambient,
    float directLight,
    float specular,
    float roughness)
{
    m_renderer.SetMaterialSettings(brightness, contrast, saturation, ambient, directLight, specular, roughness);
}

RECT PetRenderWindow::WindowRect() const
{
    RECT rc{};
    if (m_hwnd) GetWindowRect(m_hwnd, &rc);
    return rc;
}

LRESULT CALLBACK PetRenderWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PetRenderWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<PetRenderWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<PetRenderWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PetRenderWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
        BeginDrag(lp);
        return 0;
    case WM_MOUSEMOVE:
        if (m_dragging) DragTo(lp);
        return 0;
    case WM_LBUTTONUP:
        EndDrag();
        return 0;
    case WM_LBUTTONDBLCLK:
        TriggerWave();
        return 0;
    case WM_SIZE:
        if (m_hwnd && wp != SIZE_MINIMIZED)
        {
            int width = LOWORD(lp);
            int height = HIWORD(lp);
            if (width > 0 && height > 0)
            {
                m_renderer.Resize(width, height);
            }
        }
        return 0;
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void PetRenderWindow::BeginDrag(LPARAM)
{
    if (!m_hwnd) return;
    m_dragging = true;
    SetCapture(m_hwnd);
    GetCursorPos(&m_dragStartScreen);
    GetWindowRect(m_hwnd, &m_dragStartRect);
}

void PetRenderWindow::DragTo(LPARAM)
{
    POINT now{};
    GetCursorPos(&now);
    int dx = now.x - m_dragStartScreen.x;
    int dy = now.y - m_dragStartScreen.y;
    int width = m_dragStartRect.right - m_dragStartRect.left;
    int height = m_dragStartRect.bottom - m_dragStartRect.top;
    SetWindowPos(m_hwnd, HWND_TOPMOST, m_dragStartRect.left + dx, m_dragStartRect.top + dy, width, height, SWP_NOACTIVATE);
}

void PetRenderWindow::EndDrag()
{
    if (!m_dragging) return;
    m_dragging = false;
    ReleaseCapture();
}
