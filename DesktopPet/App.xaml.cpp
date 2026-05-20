#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;

namespace mux = winrt::Microsoft::UI::Xaml;

namespace winrt::DesktopPet::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    void App::OnLaunched(mux::LaunchActivatedEventArgs const&)
    {
        m_window = winrt::make<winrt::DesktopPet::implementation::MainWindow>();
        m_window.Activate();
    }
}
