#pragma once
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Windows.Storage.Streams.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <stdexcept>

namespace CrumbsBrowser {

// Thrown by route handlers when a requested resource does not exist.
class HttpNotFoundException : public std::runtime_error
{
public:
    explicit HttpNotFoundException(std::string const& msg) : std::runtime_error(msg) {}
};

// Routes HTTP requests intercepted via WebView2 WebResourceRequested to
// handler methods and writes JSON responses back into the event args.
//
// Usage:
//   m_router.Register(webView, port);   // call once after EnsureCoreWebView2Async
//   m_router.Unregister();              // call before closing the WebView
//
// Intercepted hosts (all on the configured port):
//   http://127.0.0.1:{port}/*
//   http://localhost:{port}/*
//   http://print:{port}/*
class PrintApiRouter
{
public:
    void Register(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& webView,
        int port);

    void Unregister();

private:
    // ---- WebView2 event handler ----------------------------------------
    void OnWebResourceRequested(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args);

    // ---- Route handlers ------------------------------------------------
    nlohmann::json HandleGetPrinters();
    nlohmann::json HandleGetPrinterByName(std::string const& name);

    // ---- Response helpers ----------------------------------------------
    void SendJsonResponse(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args,
        int statusCode,
        std::string const& statusText,
        nlohmann::json const& body);

    void SendErrorResponse(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args,
        int statusCode,
        std::string const& message);

    void SendCorsPreflightResponse(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args);

    // ---- Printer helpers -----------------------------------------------
    static nlohmann::json PrinterInfoToJson(PRINTER_INFO_2W const& info, bool includeCapabilities);
    static nlohmann::json DevModeDefaults(DEVMODEW const& dm);
    static std::string    PaperSizeToString(short dmPaperSize);
    static std::string    PrinterStatusToString(DWORD status);

    // ---- URL helpers ---------------------------------------------------
    static std::string              ExtractPath(std::string const& url);
    static std::vector<std::string> SplitPath(std::string const& path);
    static std::string              UrlDecode(std::string const& s);

    // ---- State ---------------------------------------------------------
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2 m_webView{ nullptr };
    winrt::event_token m_webResourceRequestedToken{};
};

} // namespace CrumbsBrowser
