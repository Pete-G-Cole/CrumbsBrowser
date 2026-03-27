#pragma once

#include "MainWindow.g.h"
#include "configuration.hpp"
#include "PrintApiRouter.hpp"

namespace winrt::CrumbsBrowser::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        winrt::fire_and_forget OnLoaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        void AddressBar_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void AddressBar_GotFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddressBar_LostFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EnsureHttps(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& sender, winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args);
        void BackButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ForwardButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void RefreshButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void HomeButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        private:
            std::optional<winrt::hstring> ResolveStartupUrl();
            bool RequireHttps{ false };
            winrt::Windows::Foundation::Uri m_startUri{ nullptr };
            ConfigLib::Configuration appConfiguration{ "Crumbs", "Development" };
            ::CrumbsBrowser::PrintApiRouter m_apiRouter;
    };
}

namespace winrt::CrumbsBrowser::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
