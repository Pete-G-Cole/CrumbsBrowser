#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::CrumbsBrowser::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        
        // Window doesn't have Loaded event, so access it through the content
        //if (auto root = this->Content().try_as<Microsoft::UI::Xaml::FrameworkElement>())
        //{
        //    root.Loaded({ this, &MainWindow::OnLoaded });
        //}
        //else {
			auto webView = WebBrowser();
            if ( webView )
            {
                webView.Loaded({ this, &MainWindow::OnLoaded });
			}
        //}
    }

    winrt::fire_and_forget MainWindow::OnLoaded(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        co_await WebBrowser().EnsureCoreWebView2Async();
        WebBrowser().CoreWebView2().Navigate(L"https://www.bing.com");
    }

    int32_t MainWindow::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }
}
