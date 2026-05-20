#include "pch.h"
#include "App.xaml.h"

using namespace winrt;

namespace mux = winrt::Microsoft::UI::Xaml;

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    mux::Application::Start([](auto&&)
    {
        winrt::make<winrt::DesktopPet::implementation::App>();
    });
    return 0;
}
