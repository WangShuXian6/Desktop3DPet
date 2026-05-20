#pragma once
#include "../render/D3D11Renderer.h"
#include "../anim/AnimationController.h"

class PetRenderWindow
{
public:
    bool Create(HINSTANCE instance, int x, int y, int width, int height);
    void Show();
    void Hide();
    void Destroy();
    void Tick(float dt);
    void TriggerWave();
    void SetScale(float scale);
    void SetAlwaysOnTop(bool top);
    bool SetModelPath(std::filesystem::path const& path);
    void SetModelRotation(float pitchDegrees, float yawDegrees, float rollDegrees);
    void SetMaterialSettings(
        float brightness,
        float contrast,
        float saturation,
        float ambient,
        float directLight,
        float specular,
        float roughness);
    RECT WindowRect() const;
    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void BeginDrag(LPARAM lp);
    void DragTo(LPARAM lp);
    void EndDrag();

    HWND m_hwnd{};
    int m_baseWidth{ 520 };
    int m_baseHeight{ 520 };
    float m_scale{ 1.0f };
    bool m_dragging{ false };
    POINT m_dragStartScreen{};
    RECT m_dragStartRect{};
    D3D11Renderer m_renderer;
    AnimationController m_animation;
};
