#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.ApplicationModel.Resources.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <Microsoft.UI.Xaml.Window.h>
#include <Lmcons.h>

#include "configuration.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::Web::WebView2::Core;
using namespace Windows::ApplicationModel::Resources;
using namespace Windows::Foundation;
using namespace Windows::System;
using namespace Microsoft::UI::Xaml::Media; // <-- Added this line


// UTF-8 to UTF-16
inline std::wstring utf8_to_utf16(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (size_needed <= 0) throw std::runtime_error("MultiByteToWideChar failed");
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &result[0], size_needed);
    return result;
}

// UTF-16 to UTF-8
inline std::string utf16_to_utf8(const std::wstring& utf16)
{
    if (utf16.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), (int)utf16.size(), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), (int)utf16.size(), &result[0], size_needed, nullptr, nullptr);
    return result;
}

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::CrumbsBrowser::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        auto windowNative = this->try_as<::IWindowNative>();
        HWND hwnd{};
        windowNative->get_WindowHandle(&hwnd);
        
        Microsoft::UI::WindowId windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
        auto appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
        appWindow.SetIcon(L"file.ico");

        auto resourceLoader = ResourceLoader::GetForViewIndependentUse();
        auto title = resourceLoader.GetString(L"MainWindow_Title");
        if (!title.empty())
        {
            Title(title);
        }

        if (auto root = Content().try_as<FrameworkElement>())
        {
            root.Loaded({ this, &MainWindow::OnLoaded });
        }

        Closed([this](IInspectable const&, WindowEventArgs const&)
        {
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

        try
        {
            if (auto startUrl = ResolveStartupUrl())
            {
                startUrl = ReplaceTokensInString(*startUrl);
                Uri uri{ *startUrl };
                WebBrowser().Source(uri);
                addressBar().Text(uri.AbsoluteUri());
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
    hstring MainWindow::ReplaceTokensInString(hstring replaceIn)
    {
        constexpr wchar_t token[] = L"[USERNAME]";
        constexpr size_t tokenLength = sizeof(token) / sizeof(wchar_t) - 1;

        wchar_t userName[UNLEN + 1]{};
        DWORD userNameSize = static_cast<DWORD>(std::size(userName));
        if (!GetUserNameW(userName, &userNameSize))
        {
            return replaceIn;
        }

        std::wstring output{ replaceIn.c_str(), replaceIn.size() };
        std::wstring replacement{ userName };
        size_t pos = 0;

        while (pos + tokenLength <= output.size())
        {
            if (_wcsnicmp(output.c_str() + pos, token, tokenLength) == 0)
            {
                output.replace(pos, tokenLength, replacement);
                pos += replacement.size();
            }
            else
            {
                ++pos;
            }
        }

        return hstring{ output };
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

        ConfigLib::Configuration config("Crumbs", "Development");

        //int maxItems = config.getInt("App/MaxItems", 10);
        //bool debug = config.getBool("Logging/DebugEnabled", false);
        //auto servers = config.getArray("App/Servers");

        //std::cout << "Max items: " << maxItems << "\n";
        //std::cout << "Debug: " << std::boolalpha << debug << "\n";

        //for (auto& s : servers)
        //    std::cout << "Server: " << s.get<std::string>() << "\n";

        RequireHttps = config.getBool("Security/RequireHttps", false);

		auto sUrl = config.getString("Startup/Url", "");
		if (!sUrl.empty()) {
			auto arguments = config.getArray("Startup/Arguments");
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

    void MainWindow::AddressBar_GotFocus(IInspectable const&, RoutedEventArgs const&)
    {
        addressBarBorder().BorderBrush(SolidColorBrush({ 255, 0, 120, 212 }));
    }

    void MainWindow::AddressBar_LostFocus(IInspectable const&, RoutedEventArgs const&)
    {
        addressBarBorder().BorderBrush(SolidColorBrush({ 255, 153, 153, 153 }));
    }
}
