#pragma once
#include "MainWindow.g.h"
#include "native/PetRenderWindow.h"
#include "sys/TrayIcon.h"
#include "data/ConfigStore.h"
#include "data/MemoryStore.h"
#include "voice/MockTtsProvider.h"

namespace winrt::DesktopPet::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        void OnShowPetClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHidePetClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnWaveClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSpeakClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnWriteMemoryClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAlwaysTopClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnScaleChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void OnModelSelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnBrowseModelClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnModelRotationChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void OnResetModelRotationClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMaterialChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void OnResetMaterialClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnClosed(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::WindowEventArgs const& args);

    private:
        void InitializeNativeServices();
        void StartRenderTimer();
        void ShutdownNativeServices();
        void SaveRuntimeConfig();
        void SetStatus(std::wstring const& text);
        void ApplyModelPath(std::wstring const& modelPath);
        void UpdateModelControls();
        void ApplyModelRotation();
        void UpdateModelRotationControls();
        void ApplyMaterialSettings();
        void UpdateMaterialControls();
        void UpdateScaleControls();

        std::unique_ptr<PetRenderWindow> m_petWindow;
        std::unique_ptr<TrayIcon> m_trayIcon;
        std::unique_ptr<ITtsProvider> m_tts;
        ConfigStore m_configStore;
        AppConfig m_config{};
        MemoryStore m_memoryStore;
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        std::chrono::steady_clock::time_point m_lastTick{};
        bool m_initialized{ false };
        bool m_shuttingDown{ false };
        bool m_updatingModelControls{ false };
        bool m_updatingRotationControls{ false };
        bool m_updatingMaterialControls{ false };
    };
}

namespace winrt::DesktopPet::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
