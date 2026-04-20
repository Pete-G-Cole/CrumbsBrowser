#pragma once
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Windows.Storage.Streams.h>
#include <nlohmann/json.hpp>
#include <mutex>
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
    // Width and height of a named paper size, in inches.
    struct PaperSize
    {
        double width;
        double height;
    };

    void Register(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& webView,
        int port);

    void Unregister();

    // Returns the dimensions of a named paper size supported by the given printer.
    // Throws std::runtime_error if the printer cannot be queried or the paper name
    // is not found in the printer's supported sizes.
    static PaperSize GetPaperSize(std::string const& printerName, std::string const& paperName);

    // Returns printerName unchanged if it is non-empty, otherwise queries and
    // returns the system default printer name.
    // Throws std::runtime_error if printerName is empty and no default printer is set.
    static std::string ResolvePrinterName(std::string const& printerName);

private:
    // ---- WebView2 event handler ----------------------------------------
    void OnWebResourceRequested(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args);

    // ---- Route handlers ------------------------------------------------
    nlohmann::json HandleGetPrinters();
    nlohmann::json HandleGetPrinterByName(std::string const& name);
    nlohmann::json HandlePostPrint(winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintSettings const& settings, bool silent);
    void HandlePostPrintToPdf(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args,
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintSettings const& settings);

    // ---- Request helpers -----------------------------------------------
    nlohmann::json ReadRequestBodyJson(winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args);

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

    void SendBinaryResponse(
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& args,
        int statusCode,
        std::string const& statusText,
        std::vector<uint8_t> const& data,
        std::string const& contentType);

    // ---- Print settings helpers ----------------------------------------
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintSettings BuildPrintSettings(nlohmann::json const& options);
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintSettings BuildPdfSettings(nlohmann::json const& options);

    // ---- Printer helpers -----------------------------------------------
    static nlohmann::json PrinterInfoToJson(PRINTER_INFO_2W const& info, bool includeCapabilities);
    static nlohmann::json DevModeDefaults(DEVMODEW const& dm);
    static std::string    PaperSizeToString(short dmPaperSize);
    static std::string    PrinterStatusToString(DWORD status);
    static std::string    GetAppVersion();

    // ---- URL helpers ---------------------------------------------------
    static std::string              ExtractPath(std::string const& url);
    static std::vector<std::string> SplitPath(std::string const& path);
    static std::string              UrlDecode(std::string const& s);

    // ---- State ---------------------------------------------------------
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2 m_webView{ nullptr };
    winrt::event_token m_webResourceRequestedToken{};
    std::mutex m_printMutex;
    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintStatus> m_activePrintOperation{ nullptr };
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IRandomAccessStream> m_activePdfOperation{ nullptr };
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintStatus m_lastPrintStatus{ winrt::Microsoft::Web::WebView2::Core::CoreWebView2PrintStatus::OtherError };
};

} // namespace CrumbsBrowser
