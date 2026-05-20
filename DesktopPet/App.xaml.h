#pragma once
#include "App.g.h"
#include "MainWindow.xaml.h"

namespace winrt::DesktopPet::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        winrt::DesktopPet::MainWindow m_window{ nullptr };
    };
}

namespace winrt::DesktopPet::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
