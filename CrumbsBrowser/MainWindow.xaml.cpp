#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.ApplicationModel.Resources.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <Microsoft.UI.Xaml.Window.h>
#include <Lmcons.h>
#include "UrlUtils.hpp"
#include "StringUtils.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::Web::WebView2::Core;
using namespace Windows::ApplicationModel::Resources;
using namespace Windows::Foundation;
using namespace Windows::System;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::CrumbsBrowser::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        {
            const bool showNavButtons = appConfiguration.getBool("UI/NavigationButtons", true);
            const bool showHomeButton  = appConfiguration.getBool("UI/HomeButton", true);
            const bool showAddressBar  = appConfiguration.getBool("UI/AddressBar", true);

            if (!showNavButtons)
                navButtonsPanel().Visibility(Visibility::Collapsed);

            if (!showHomeButton)
                homeButton().Visibility(Visibility::Collapsed);

            if (!showNavButtons || !showHomeButton)
                navHomeSeparator().Visibility(Visibility::Collapsed);

            if (!showAddressBar)
                addressBarBorder().Visibility(Visibility::Collapsed);

            if (!showAddressBar)
            {
                // Move the nav controls panel out of the toolbar row and into the
                // title bar, anchored to the left edge of the window.
                auto navPanel = navControlsPanel();

                auto uiChildren = uiGrid().Children();
                uint32_t idx{};
                if (uiChildren.IndexOf(navPanel, idx))
                    uiChildren.RemoveAt(idx);

                navPanel.HorizontalAlignment(HorizontalAlignment::Left);
                navPanel.VerticalAlignment(VerticalAlignment::Center);
                navPanel.Margin({ 4, 0, 0, 0 });

                titleBarGrid().Children().Append(navPanel);

                // Collapse the now-empty toolbar row so it takes no space.
                uiGrid().Visibility(Visibility::Collapsed);
            }
        }

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(titleBarGrid());

        auto windowNative = this->try_as<::IWindowNative>();
        HWND hwnd{};
        windowNative->get_WindowHandle(&hwnd);

        Microsoft::UI::WindowId windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
        auto appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
        appWindow.SetIcon(L"file.ico");

        // Make caption button backgrounds transparent so they blend with the custom title bar
        auto titleBar = appWindow.TitleBar();
        titleBar.ButtonBackgroundColor(winrt::Windows::UI::Color{ 0, 0, 0, 0 });
        titleBar.ButtonInactiveBackgroundColor(winrt::Windows::UI::Color{ 0, 0, 0, 0 });

        auto resourceLoader = ResourceLoader::GetForViewIndependentUse();
        auto title = resourceLoader.GetString(L"MainWindow_Title");
        if (!title.empty())
        {
            Title(title);
            titleBarText().Text(title);
        }

        if (auto root = Content().try_as<FrameworkElement>())
        {
            root.Loaded({ this, &MainWindow::OnLoaded });
        }

        Closed([this](IInspectable const&, WindowEventArgs const&)
        {
            m_apiRouter.Unregister();
            if (auto webview = WebBrowser())
            {
                webview.Close();
            }
        });
    }

    winrt::fire_and_forget MainWindow::OnLoaded(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        co_await WebBrowser().EnsureCoreWebView2Async();

        WebBrowser().CoreWebView2().NavigationStarting(
            [this](CoreWebView2 const&, CoreWebView2NavigationStartingEventArgs const&)
            {
                titleBarIcon().Visibility(Visibility::Collapsed);
                titleBarProgressRing().IsActive(true);
                titleBarProgressRing().Visibility(Visibility::Visible);
            });

        WebBrowser().CoreWebView2().NavigationCompleted(
            [this](CoreWebView2 const&, CoreWebView2NavigationCompletedEventArgs const&)
            {
                titleBarProgressRing().IsActive(false);
                titleBarProgressRing().Visibility(Visibility::Collapsed);
                if (titleBarIcon().Source() && titleBarIcon().Visibility() == Visibility::Collapsed)
                {
                    titleBarIcon().Visibility(Visibility::Visible);
                }
            });

        WebBrowser().CoreWebView2().FaviconChanged(
            [this](CoreWebView2 const& sender, IInspectable const&) -> winrt::fire_and_forget
            {
                auto stream = co_await sender.GetFaviconAsync(CoreWebView2FaviconImageFormat::Png);
                if (stream)
                {
                    try
                    {
                        BitmapImage bitmap{};
                        co_await bitmap.SetSourceAsync(stream);
                        titleBarIcon().Source(bitmap);
                        titleBarIcon().Visibility(Visibility::Visible);
                    }
                    catch (hresult_error const&)
                    {
                        titleBarIcon().Source(nullptr);
                        titleBarIcon().Visibility(Visibility::Collapsed);
                    }
                }
                else
                {
                    titleBarIcon().Source(nullptr);
                    titleBarIcon().Visibility(Visibility::Collapsed);
                }
            });

        WebBrowser().CoreWebView2().DocumentTitleChanged(
            [this](CoreWebView2 const& sender, IInspectable const&)
            {
                auto pageTitle = sender.DocumentTitle();
                titleBarText().Text(pageTitle.empty() ? Title() : pageTitle);
            });

        WebBrowser().CoreWebView2().HistoryChanged(
            [this](CoreWebView2 const& sender, IInspectable const&)
            {
                backButton().IsEnabled(sender.CanGoBack());
                forwardButton().IsEnabled(sender.CanGoForward());
            });

		WebBrowser().CoreWebView2().SourceChanged(
			[this](CoreWebView2 const& sender, CoreWebView2SourceChangedEventArgs const&)
			{
				addressBar().Text(sender.Source());
			});

		m_apiRouter.Register(WebBrowser().CoreWebView2(),
			appConfiguration.getInt("Api/Port", 41190));

		refreshButton().IsEnabled(true);
		RequireHttps = appConfiguration.getBool("Security/RequireHttps", false);

		try
        {
            if (auto startUrl = ResolveStartupUrl())
            {
                startUrl = ::CrumbsBrowser::ReplaceTokens(std::wstring(*startUrl));
                m_startUri = Uri{ *startUrl };
                homeButton().IsEnabled(true);
                WebBrowser().Source(m_startUri);
                addressBar().Text(m_startUri.AbsoluteUri());
            }
        }
        catch (hresult_error const&)
        {
            // Ignore invalid configured URI and fall back to default.
        }
        catch (std::exception const&)
        {
            // Prevent unhandled std::exceptions from terminating the app.
        }
    }
  
    void MainWindow::AddressBar_KeyDown(IInspectable const&,KeyRoutedEventArgs const& args)
    {
        if (args.Key() == VirtualKey::Enter)
        {
            args.Handled(true);
            auto text = addressBar().Text();

            if (text.empty())
            {
                return;
            }

            if (std::wstring_view(text).find(L"://") == std::wstring_view::npos)
            {
                text = L"https://" + text;
            }

            try
            {
                Uri uri{ text };
                auto webview = WebBrowser();
                if (webview)
                {
                    webview.Source(uri);
                    addressBar().Text(uri.AbsoluteUri());
                }
            }
            catch (hresult_error const&)
            {
                // Invalid URI; ignore navigation
            }
        }
    }

    void MainWindow::EnsureHttps(WebView2 const&, CoreWebView2NavigationStartingEventArgs const& args)
    {
        if (RequireHttps) {
            Uri uri{ args.Uri() };
            if (uri.SchemeName() != L"https")
            {
                args.Cancel(true);
            }
        }
    }

    
    int32_t MainWindow::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }

    std::optional<hstring> MainWindow::ResolveStartupUrl() {
        // The startup URL can be configured via the CRUMBS_STARTUP_URL environment variable or appsettings.json files located in either the user's LocalAppData or the ProgramFiles directory. 
        // The environment variable takes precedence over the configuration files, and the user's LocalAppData file takes precedence over the ProgramFiles file.

        //int maxItems = appConfiguration.getInt("App/MaxItems", 10);
        //bool debug = appConfiguration.getBool("Logging/DebugEnabled", false);
        //auto servers = appConfiguration.getArray("App/Servers");

        //std::cout << "Max items: " << maxItems << "\n";
        //std::cout << "Debug: " << std::boolalpha << debug << "\n";

        //for (auto& s : servers)
        //    std::cout << "Server: " << s.get<std::string>() << "\n";

		auto sUrl = appConfiguration.getString("Startup/Url", "");
		if (!sUrl.empty()) {
			auto arguments = appConfiguration.getArray("Startup/Arguments");
			bool first = true;
			for (auto& arg : arguments)
			{
				auto name  = arg.value("Name",  "");
                auto value = arg.value("Value", "");
				if (!name.empty())
				{
					sUrl += first ? "?" : "&";
					sUrl += name + "=" + value;
					first = false;
				}
			}

			return hstring{ utf8_to_utf16(sUrl) };
		}

        return std::nullopt;
    }

    void MainWindow::HomeButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_startUri)
        {
            WebBrowser().Source(m_startUri);
        }
    }

    void MainWindow::BackButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        WebBrowser().CoreWebView2().GoBack();
    }

    void MainWindow::ForwardButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        WebBrowser().CoreWebView2().GoForward();
    }

    void MainWindow::RefreshButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        WebBrowser().CoreWebView2().Reload();
    }

    void MainWindow::AddressBar_GotFocus(IInspectable const&, RoutedEventArgs const&)
    {
        addressBarBorder().BorderThickness({ 1.5, 1.5, 1.5, 1.5 });
        addressBarBorder().BorderBrush(SolidColorBrush({ 255, 0, 120, 212 }));
    }

    void MainWindow::AddressBar_LostFocus(IInspectable const&, RoutedEventArgs const&)
    {
        addressBarBorder().BorderThickness({ 1.125, 1.125, 1.125, 1.125 });
        addressBarBorder().BorderBrush(SolidColorBrush({ 255, 200, 200, 200 }));
    }
}
