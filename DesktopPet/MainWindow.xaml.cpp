#include "pch.h"
#include "MainWindow.xaml.h"
#include <commdlg.h>

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace muxcp = winrt::Microsoft::UI::Xaml::Controls::Primitives;
namespace wf = winrt::Windows::Foundation;

namespace
{
    constexpr wchar_t const* kDefaultModelPath = L"1.fbx";
    constexpr wchar_t const* kElfModelPath = L"EL_F_EY_N_13500.fbx";

    std::wstring NormalizeModelPath(std::wstring const& path)
    {
        return path.empty() ? std::wstring(kDefaultModelPath) : path;
    }

    int BuiltInModelIndex(std::wstring const& path)
    {
        const auto fileName = std::filesystem::path(NormalizeModelPath(path)).filename().wstring();
        if (fileName == kDefaultModelPath) return 0;
        if (fileName == kElfModelPath) return 1;
        return -1;
    }

    std::wstring BuiltInModelPathForIndex(int index)
    {
        switch (index)
        {
        case 0: return kDefaultModelPath;
        case 1: return kElfModelPath;
        default: return {};
        }
    }

    std::wstring DisplayModelPath(std::wstring const& path)
    {
        const auto normalized = NormalizeModelPath(path);
        std::filesystem::path fsPath(normalized);
        if (fsPath.is_absolute())
        {
            return normalized;
        }

        const auto fileName = fsPath.filename().wstring();
        return fileName.empty() ? normalized : fileName;
    }

    std::wstring DisplayModelName(std::wstring const& path)
    {
        const auto fileName = std::filesystem::path(NormalizeModelPath(path)).filename().wstring();
        return fileName.empty() ? NormalizeModelPath(path) : fileName;
    }

    std::wstring FormatDegrees(float value)
    {
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"%.0f°", value);
        return buffer;
    }

    std::wstring FormatScalar(float value)
    {
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"%.2f", value);
        return buffer;
    }

    std::wstring PickFbxModel(HWND owner)
    {
        std::vector<wchar_t> fileName(32768);
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"FBX 模型 (*.fbx)\0*.fbx\0所有文件 (*.*)\0*.*\0";
        dialog.lpstrFile = fileName.data();
        dialog.nMaxFile = static_cast<DWORD>(fileName.size());
        dialog.lpstrDefExt = L"fbx";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

        if (!GetOpenFileNameW(&dialog))
        {
            return {};
        }

        return fileName.data();
    }
}

namespace winrt::DesktopPet::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"DesktopPet Control Center");
        Closed({ this, &MainWindow::OnClosed });

        m_tts = std::make_unique<MockTtsProvider>();
        InitializeNativeServices();
        StartRenderTimer();
    }

    MainWindow::~MainWindow()
    {
        ShutdownNativeServices();
    }

    void MainWindow::InitializeNativeServices()
    {
        if (m_initialized) return;

        m_config = m_configStore.Load();
        m_memoryStore.Open();

        const int w = m_config.petWidth;
        const int h = m_config.petHeight;
        const int x = m_config.petX;
        const int y = m_config.petY;
        m_config.scale = std::clamp(m_config.scale, 0.1f, 2.0f);
        if (m_config.modelPath.empty())
        {
            m_config.modelPath = kDefaultModelPath;
        }

        m_petWindow = std::make_unique<PetRenderWindow>();
        bool modelLoaded = true;
        if (m_petWindow->Create(GetModuleHandleW(nullptr), x, y, w, h))
        {
            modelLoaded = m_petWindow->SetModelPath(m_config.modelPath);
            m_petWindow->SetModelRotation(m_config.modelRotationX, m_config.modelRotationY, m_config.modelRotationZ);
            ApplyMaterialSettings();
            m_petWindow->SetScale(m_config.scale);
            m_petWindow->SetAlwaysOnTop(m_config.alwaysOnTop);
            if (m_config.showPetOnStart)
            {
                m_petWindow->Show();
            }
        }

        TrayCallbacks callbacks{};
        callbacks.showPet = [this]() { if (!m_shuttingDown && m_petWindow) m_petWindow->Show(); };
        callbacks.hidePet = [this]() { if (!m_shuttingDown && m_petWindow) m_petWindow->Hide(); };
        callbacks.wave = [this]() { if (!m_shuttingDown && m_petWindow) m_petWindow->TriggerWave(); };
        callbacks.openControlCenter = [this]() { if (!m_shuttingDown) this->Activate(); };
        callbacks.exitApp = [this]()
        {
            ShutdownNativeServices();
            mux::Application::Current().Exit();
        };

        m_trayIcon = std::make_unique<TrayIcon>();
        m_trayIcon->Create(GetModuleHandleW(nullptr), L"DesktopPet", callbacks);

        ScaleSlider().Value(m_config.scale);
        UpdateScaleControls();
        AlwaysTopCheck().IsChecked(winrt::box_value(m_config.alwaysOnTop).as<wf::IReference<bool>>());
        UpdateModelControls();
        UpdateModelRotationControls();
        UpdateMaterialControls();
        std::wstring status = std::wstring(L"状态：已启动。可拖动宠物窗口，或使用托盘菜单。配置目录：") + m_configStore.DataDirectory();
        if (!modelLoaded)
        {
            status += std::wstring(L" 模型加载失败，已使用占位宠物：") + DisplayModelPath(m_config.modelPath);
        }
        SetStatus(status);
        m_initialized = true;
    }

    void MainWindow::StartRenderTimer()
    {
        m_lastTick = std::chrono::steady_clock::now();
        m_timer = mux::DispatcherTimer();
        m_timer.Interval(std::chrono::milliseconds(16));
        m_timer.Tick([this](wf::IInspectable const&, wf::IInspectable const&)
        {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - m_lastTick).count();
            m_lastTick = now;
            if (!m_shuttingDown && m_petWindow)
            {
                m_petWindow->Tick(std::clamp(dt, 0.0f, 0.05f));
            }
        });
        m_timer.Start();
    }

    void MainWindow::ShutdownNativeServices()
    {
        if (m_shuttingDown) return;
        m_shuttingDown = true;

        if (m_timer)
        {
            m_timer.Stop();
            m_timer = nullptr;
        }

        SaveRuntimeConfig();

        if (m_trayIcon)
        {
            m_trayIcon->Remove();
            m_trayIcon.reset();
        }

        if (m_petWindow)
        {
            m_petWindow->Destroy();
            m_petWindow.reset();
        }
    }

    void MainWindow::SaveRuntimeConfig()
    {
        if (m_petWindow)
        {
            RECT rc = m_petWindow->WindowRect();
            m_config.petX = rc.left;
            m_config.petY = rc.top;
            m_config.petWidth = rc.right - rc.left;
            m_config.petHeight = rc.bottom - rc.top;
        }

        m_configStore.Save(m_config);
    }

    void MainWindow::SetStatus(std::wstring const& text)
    {
        StatusText().Text(winrt::hstring(text));
    }

    void MainWindow::ApplyModelPath(std::wstring const& modelPath)
    {
        m_config.modelPath = NormalizeModelPath(modelPath);
        const bool loaded = m_petWindow ? m_petWindow->SetModelPath(m_config.modelPath) : true;
        UpdateModelControls();
        SaveRuntimeConfig();

        if (loaded)
        {
            SetStatus(std::wstring(L"状态：已切换模型：") + DisplayModelName(m_config.modelPath));
        }
        else
        {
            SetStatus(std::wstring(L"状态：模型加载失败，已使用占位宠物：") + DisplayModelPath(m_config.modelPath));
        }
    }

    void MainWindow::UpdateModelControls()
    {
        m_updatingModelControls = true;
        ModelComboBox().SelectedIndex(BuiltInModelIndex(m_config.modelPath));
        ModelPathText().Text(winrt::hstring(std::wstring(L"当前模型：") + DisplayModelPath(m_config.modelPath)));
        m_updatingModelControls = false;
    }

    void MainWindow::ApplyModelRotation()
    {
        if (m_petWindow)
        {
            m_petWindow->SetModelRotation(m_config.modelRotationX, m_config.modelRotationY, m_config.modelRotationZ);
        }
    }

    void MainWindow::UpdateModelRotationControls()
    {
        m_updatingRotationControls = true;
        ModelPitchSlider().Value(m_config.modelRotationX);
        ModelYawSlider().Value(m_config.modelRotationY);
        ModelRollSlider().Value(m_config.modelRotationZ);
        ModelPitchText().Text(winrt::hstring(FormatDegrees(m_config.modelRotationX)));
        ModelYawText().Text(winrt::hstring(FormatDegrees(m_config.modelRotationY)));
        ModelRollText().Text(winrt::hstring(FormatDegrees(m_config.modelRotationZ)));
        m_updatingRotationControls = false;
    }

    void MainWindow::ApplyMaterialSettings()
    {
        if (m_petWindow)
        {
            m_petWindow->SetMaterialSettings(
                m_config.materialBrightness,
                m_config.materialContrast,
                m_config.materialSaturation,
                m_config.materialAmbient,
                m_config.materialDirectLight,
                m_config.materialSpecular,
                m_config.materialRoughness);
        }
    }

    void MainWindow::UpdateMaterialControls()
    {
        m_updatingMaterialControls = true;
        MaterialBrightnessSlider().Value(m_config.materialBrightness);
        MaterialContrastSlider().Value(m_config.materialContrast);
        MaterialSaturationSlider().Value(m_config.materialSaturation);
        MaterialAmbientSlider().Value(m_config.materialAmbient);
        MaterialDirectLightSlider().Value(m_config.materialDirectLight);
        MaterialSpecularSlider().Value(m_config.materialSpecular);
        MaterialRoughnessSlider().Value(m_config.materialRoughness);
        MaterialBrightnessText().Text(winrt::hstring(FormatScalar(m_config.materialBrightness)));
        MaterialContrastText().Text(winrt::hstring(FormatScalar(m_config.materialContrast)));
        MaterialSaturationText().Text(winrt::hstring(FormatScalar(m_config.materialSaturation)));
        MaterialAmbientText().Text(winrt::hstring(FormatScalar(m_config.materialAmbient)));
        MaterialDirectLightText().Text(winrt::hstring(FormatScalar(m_config.materialDirectLight)));
        MaterialSpecularText().Text(winrt::hstring(FormatScalar(m_config.materialSpecular)));
        MaterialRoughnessText().Text(winrt::hstring(FormatScalar(m_config.materialRoughness)));
        m_updatingMaterialControls = false;
    }

    void MainWindow::UpdateScaleControls()
    {
        ScaleValueText().Text(winrt::hstring(FormatScalar(m_config.scale) + L"x"));
    }

    void MainWindow::OnShowPetClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        if (m_petWindow) m_petWindow->Show();
        SetStatus(L"状态：宠物已显示。 ");
    }

    void MainWindow::OnHidePetClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        if (m_petWindow) m_petWindow->Hide();
        SetStatus(L"状态：宠物已隐藏。 ");
    }

    void MainWindow::OnWaveClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        if (m_petWindow) m_petWindow->TriggerWave();
        SetStatus(L"状态：触发挥手动作。 ");
    }

    void MainWindow::OnSpeakClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        std::wstring text = SpeakTextBox().Text().c_str();
        if (text.empty())
        {
            text = L"你好，我是桌面宠物。";
        }

        m_memoryStore.Append(L"default", L"dialog", text);
        if (m_petWindow) m_petWindow->TriggerWave();

        if (m_tts)
        {
            SpeakRequest req{};
            req.text = text;
            req.voice = L"mock";
            req.style = L"cheerful";
            const auto result = m_tts->Speak(req);
            SetStatus(std::wstring(L"状态：") + result.message + L" 已写入本地记忆。 ");
        }
    }

    void MainWindow::OnWriteMemoryClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        m_memoryStore.Append(L"default", L"debug", L"这是一条测试记忆。 ");
        SetStatus(L"状态：已写入测试记忆。 ");
    }

    void MainWindow::OnAlwaysTopClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        auto checked = AlwaysTopCheck().IsChecked();
        bool top = checked && checked.Value();
        m_config.alwaysOnTop = top;
        if (m_petWindow) m_petWindow->SetAlwaysOnTop(top);
        SaveRuntimeConfig();
    }

    void MainWindow::OnScaleChanged(wf::IInspectable const&, muxcp::RangeBaseValueChangedEventArgs const& args)
    {
        if (!m_initialized || m_shuttingDown) return;

        m_config.scale = static_cast<float>(args.NewValue());
        UpdateScaleControls();
        if (m_petWindow) m_petWindow->SetScale(m_config.scale);
    }

    void MainWindow::OnModelSelectionChanged(wf::IInspectable const&, muxc::SelectionChangedEventArgs const&)
    {
        if (m_updatingModelControls || m_shuttingDown) return;

        std::wstring modelPath = BuiltInModelPathForIndex(ModelComboBox().SelectedIndex());
        if (!modelPath.empty())
        {
            ApplyModelPath(modelPath);
        }
    }

    void MainWindow::OnBrowseModelClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        HWND hwnd{};
        if (auto nativeWindow = this->try_as<IWindowNative>())
        {
            nativeWindow->get_WindowHandle(&hwnd);
        }

        std::wstring modelPath = PickFbxModel(hwnd);
        if (!modelPath.empty())
        {
            ApplyModelPath(modelPath);
        }
    }

    void MainWindow::OnModelRotationChanged(wf::IInspectable const&, muxcp::RangeBaseValueChangedEventArgs const&)
    {
        if (!m_initialized || m_updatingRotationControls || m_shuttingDown) return;

        m_config.modelRotationX = static_cast<float>(ModelPitchSlider().Value());
        m_config.modelRotationY = static_cast<float>(ModelYawSlider().Value());
        m_config.modelRotationZ = static_cast<float>(ModelRollSlider().Value());
        ApplyModelRotation();
        UpdateModelRotationControls();
        SaveRuntimeConfig();
    }

    void MainWindow::OnResetModelRotationClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        m_config.modelRotationX = 0.0f;
        m_config.modelRotationY = 0.0f;
        m_config.modelRotationZ = 0.0f;
        ApplyModelRotation();
        UpdateModelRotationControls();
        SaveRuntimeConfig();
        SetStatus(L"状态：已重置模型旋转。 ");
    }

    void MainWindow::OnMaterialChanged(wf::IInspectable const&, muxcp::RangeBaseValueChangedEventArgs const&)
    {
        if (!m_initialized || m_updatingMaterialControls || m_shuttingDown) return;

        m_config.materialBrightness = static_cast<float>(MaterialBrightnessSlider().Value());
        m_config.materialContrast = static_cast<float>(MaterialContrastSlider().Value());
        m_config.materialSaturation = static_cast<float>(MaterialSaturationSlider().Value());
        m_config.materialAmbient = static_cast<float>(MaterialAmbientSlider().Value());
        m_config.materialDirectLight = static_cast<float>(MaterialDirectLightSlider().Value());
        m_config.materialSpecular = static_cast<float>(MaterialSpecularSlider().Value());
        m_config.materialRoughness = static_cast<float>(MaterialRoughnessSlider().Value());
        ApplyMaterialSettings();
        UpdateMaterialControls();
        SaveRuntimeConfig();
    }

    void MainWindow::OnResetMaterialClicked(wf::IInspectable const&, mux::RoutedEventArgs const&)
    {
        m_config.materialBrightness = 1.0f;
        m_config.materialContrast = 1.0f;
        m_config.materialSaturation = 1.0f;
        m_config.materialAmbient = 0.22f;
        m_config.materialDirectLight = 0.78f;
        m_config.materialSpecular = 0.0f;
        m_config.materialRoughness = 0.45f;
        ApplyMaterialSettings();
        UpdateMaterialControls();
        SaveRuntimeConfig();
        SetStatus(L"状态：已重置材质参数。 ");
    }

    void MainWindow::OnClosed(wf::IInspectable const&, mux::WindowEventArgs const&)
    {
        ShutdownNativeServices();
    }
}
