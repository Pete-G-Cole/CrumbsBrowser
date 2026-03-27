#include "pch.h"
#include "PrintApiRouter.hpp"
#include "StringUtils.hpp"
#include <winrt/Windows.Storage.Streams.h>

using json = nlohmann::json;

using namespace winrt::Microsoft::Web::WebView2::Core;
using namespace winrt::Windows::Storage::Streams;

namespace CrumbsBrowser {

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void PrintApiRouter::Register(CoreWebView2 const& webView, int port)
{
    m_webView = webView;
    auto portStr = std::to_wstring(port);

    for (auto& host : { std::wstring(L"127.0.0.1"), std::wstring(L"localhost"), std::wstring(L"print") })
    {
        webView.AddWebResourceRequestedFilter(
            winrt::hstring(L"http://" + host + L":" + portStr + L"/*"),
            CoreWebView2WebResourceContext::All);
    }

    m_webResourceRequestedToken = webView.WebResourceRequested(
        [this](CoreWebView2 const& sender, CoreWebView2WebResourceRequestedEventArgs const& args)
        {
            OnWebResourceRequested(sender, args);
        });
}

void PrintApiRouter::Unregister()
{
    if (m_webView)
    {
        m_webView.WebResourceRequested(m_webResourceRequestedToken);
        m_webView = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Request dispatcher
// ---------------------------------------------------------------------------

void PrintApiRouter::OnWebResourceRequested(
    CoreWebView2 const&,
    CoreWebView2WebResourceRequestedEventArgs const& args)
{
    try
    {
        auto method   = winrt::to_string(args.Request().Method());
        auto path     = ExtractPath(winrt::to_string(args.Request().Uri()));
        auto segments = SplitPath(path);

        if (method == "OPTIONS")
        {
            SendCorsPreflightResponse(args);
            return;
        }

        if (method == "GET")
        {
            if (segments.size() == 1 && segments[0] == "printers")
            {
                SendJsonResponse(args, 200, "OK", HandleGetPrinters());
                return;
            }
            if (segments.size() == 2 && segments[0] == "printers")
            {
                SendJsonResponse(args, 200, "OK", HandleGetPrinterByName(segments[1]));
                return;
            }
        }

        SendErrorResponse(args, 404, "Not Found");
    }
    catch (HttpNotFoundException const& e)
    {
        SendErrorResponse(args, 404, e.what());
    }
    catch (std::exception const& e)
    {
        SendErrorResponse(args, 500, e.what());
    }
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

json PrintApiRouter::HandleGetPrinters()
{
    DWORD needed = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
        nullptr, 2, nullptr, 0, &needed, &count);

    std::vector<BYTE> buf(needed);
    if (needed > 0 &&
        !EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
            nullptr, 2, buf.data(), needed, &needed, &count))
    {
        throw std::runtime_error("EnumPrintersW failed: " + std::to_string(GetLastError()));
    }

    json arr = json::array();
    auto* info = reinterpret_cast<PRINTER_INFO_2W*>(buf.data());
    for (DWORD i = 0; i < count; ++i)
        arr.push_back(PrinterInfoToJson(info[i], false));

    return arr;
}

json PrintApiRouter::HandleGetPrinterByName(std::string const& name)
{
    auto wname = utf8_to_utf16(name);

    DWORD needed = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
        nullptr, 2, nullptr, 0, &needed, &count);

    std::vector<BYTE> buf(needed);
    if (needed > 0 &&
        !EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
            nullptr, 2, buf.data(), needed, &needed, &count))
    {
        throw std::runtime_error("EnumPrintersW failed: " + std::to_string(GetLastError()));
    }

    auto* info = reinterpret_cast<PRINTER_INFO_2W*>(buf.data());
    for (DWORD i = 0; i < count; ++i)
    {
        if (info[i].pPrinterName && _wcsicmp(info[i].pPrinterName, wname.c_str()) == 0)
            return PrinterInfoToJson(info[i], true);
    }

    throw HttpNotFoundException("Printer not found: " + name);
}

// ---------------------------------------------------------------------------
// Printer helpers
// ---------------------------------------------------------------------------

json PrintApiRouter::PrinterInfoToJson(PRINTER_INFO_2W const& info, bool includeCapabilities)
{
    json j;
    j["name"]       = utf16_to_utf8(info.pPrinterName ? info.pPrinterName : L"");
    j["isDefault"]  = (info.Attributes & PRINTER_ATTRIBUTE_DEFAULT) != 0;
    j["status"]     = PrinterStatusToString(info.Status);
    j["portName"]   = utf16_to_utf8(info.pPortName   ? info.pPortName   : L"");
    j["driverName"] = utf16_to_utf8(info.pDriverName ? info.pDriverName : L"");

    if (info.pDevMode)
        j["defaults"] = DevModeDefaults(*info.pDevMode);

    if (includeCapabilities && info.pPrinterName)
    {
        json caps;

        // Paper sizes — each name occupies a null-padded 64-wchar slot
        DWORD n = DeviceCapabilitiesW(
            info.pPrinterName, info.pPortName, DC_PAPERNAMES, nullptr, nullptr);
        if (n != (DWORD)-1 && n > 0)
        {
            std::vector<wchar_t> names(n * 64);
            DeviceCapabilitiesW(
                info.pPrinterName, info.pPortName, DC_PAPERNAMES, names.data(), nullptr);

            json papers = json::array();
            for (DWORD i = 0; i < n; ++i)
            {
                std::wstring ws(&names[i * 64], 64);
                auto nulPos = ws.find(L'\0');
                if (nulPos != std::wstring::npos) ws.resize(nulPos);
                papers.push_back(utf16_to_utf8(ws));
            }
            caps["paperSizes"] = papers;
        }

        caps["duplex"] = (DeviceCapabilitiesW(
            info.pPrinterName, info.pPortName, DC_DUPLEX, nullptr, nullptr) == 1);

        caps["color"] = (DeviceCapabilitiesW(
            info.pPrinterName, info.pPortName, DC_COLORDEVICE, nullptr, nullptr) == 1);

        j["capabilities"] = caps;
    }

    return j;
}

json PrintApiRouter::DevModeDefaults(DEVMODEW const& dm)
{
    json j;
    if (dm.dmFields & DM_ORIENTATION)
        j["orientation"] = (dm.dmOrientation == DMORIENT_LANDSCAPE) ? "landscape" : "portrait";
    if (dm.dmFields & DM_PAPERSIZE)
        j["paperSize"] = PaperSizeToString(dm.dmPaperSize);
    if (dm.dmFields & DM_COPIES)
        j["copies"] = static_cast<int>(dm.dmCopies);
    if (dm.dmFields & DM_DUPLEX)
        j["duplex"] = (dm.dmDuplex == DMDUP_SIMPLEX)  ? "simplex"
                    : (dm.dmDuplex == DMDUP_VERTICAL)  ? "long-edge"
                                                       : "short-edge";
    if (dm.dmFields & DM_COLOR)
        j["color"] = (dm.dmColor == DMCOLOR_COLOR);
    if (dm.dmFields & DM_COLLATE)
        j["collate"] = (dm.dmCollate == DMCOLLATE_TRUE);
    return j;
}

std::string PrintApiRouter::PaperSizeToString(short dmPaperSize)
{
    switch (dmPaperSize)
    {
        case DMPAPER_LETTER:      return "Letter";
        case DMPAPER_LETTERSMALL: return "Letter Small";
        case DMPAPER_TABLOID:     return "Tabloid";
        case DMPAPER_LEDGER:      return "Ledger";
        case DMPAPER_LEGAL:       return "Legal";
        case DMPAPER_A4:          return "A4";
        case DMPAPER_A4SMALL:     return "A4 Small";
        case DMPAPER_A5:          return "A5";
        case DMPAPER_A3:          return "A3";
        case DMPAPER_B4:          return "B4";
        case DMPAPER_B5:          return "B5";
        default:                  return "Custom (" + std::to_string(dmPaperSize) + ")";
    }
}

std::string PrintApiRouter::PrinterStatusToString(DWORD status)
{
    if (status == 0)                                  return "ready";
    if (status & PRINTER_STATUS_BUSY)                 return "busy";
    if (status & PRINTER_STATUS_ERROR)                return "error";
    if (status & PRINTER_STATUS_OFFLINE)              return "offline";
    if (status & PRINTER_STATUS_PAPER_JAM)            return "paper-jam";
    if (status & PRINTER_STATUS_PAPER_OUT)            return "paper-out";
    if (status & PRINTER_STATUS_PAPER_PROBLEM)        return "paper-problem";
    if (status & PRINTER_STATUS_OUTPUT_BIN_FULL)      return "output-bin-full";
    if (status & PRINTER_STATUS_NOT_AVAILABLE)        return "not-available";
    if (status & PRINTER_STATUS_WAITING)              return "waiting";
    if (status & PRINTER_STATUS_PROCESSING)           return "processing";
    if (status & PRINTER_STATUS_INITIALIZING)         return "initializing";
    if (status & PRINTER_STATUS_WARMING_UP)           return "warming-up";
    if (status & PRINTER_STATUS_TONER_LOW)            return "toner-low";
    if (status & PRINTER_STATUS_NO_TONER)             return "no-toner";
    if (status & PRINTER_STATUS_PAGE_PUNT)            return "page-punt";
    if (status & PRINTER_STATUS_USER_INTERVENTION)    return "user-intervention";
    if (status & PRINTER_STATUS_OUT_OF_MEMORY)        return "out-of-memory";
    if (status & PRINTER_STATUS_DOOR_OPEN)            return "door-open";
    if (status & PRINTER_STATUS_SERVER_UNKNOWN)       return "server-unknown";
    if (status & PRINTER_STATUS_POWER_SAVE)           return "power-save";
    return "unknown";
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void PrintApiRouter::SendJsonResponse(
    CoreWebView2WebResourceRequestedEventArgs const& args,
    int statusCode,
    std::string const& statusText,
    json const& body)
{
    auto bodyStr = body.dump();

    auto stream = InMemoryRandomAccessStream();
    auto writer = DataWriter(stream);
    writer.WriteBytes(winrt::array_view<uint8_t const>(
        reinterpret_cast<uint8_t const*>(bodyStr.data()),
        static_cast<uint32_t>(bodyStr.size())));
    writer.StoreAsync().get();
    writer.DetachStream();
    stream.Seek(0);

    auto response = m_webView.Environment().CreateWebResourceResponse(
        stream,
        statusCode,
        winrt::to_hstring(statusText),
        L"Content-Type: application/json\r\n"
        L"Access-Control-Allow-Origin: *\r\n"
        L"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        L"Access-Control-Allow-Headers: Content-Type");

    args.Response(response);
}

void PrintApiRouter::SendErrorResponse(
    CoreWebView2WebResourceRequestedEventArgs const& args,
    int statusCode,
    std::string const& message)
{
    json body;
    body["status"]  = statusCode;
    body["message"] = message;
    SendJsonResponse(args, statusCode, message, body);
}

void PrintApiRouter::SendCorsPreflightResponse(
    CoreWebView2WebResourceRequestedEventArgs const& args)
{
    auto stream   = InMemoryRandomAccessStream();
    auto response = m_webView.Environment().CreateWebResourceResponse(
        stream,
        204,
        L"No Content",
        L"Access-Control-Allow-Origin: *\r\n"
        L"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        L"Access-Control-Allow-Headers: Content-Type\r\n"
        L"Access-Control-Max-Age: 86400");

    args.Response(response);
}

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------

std::string PrintApiRouter::ExtractPath(std::string const& url)
{
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "/";

    auto pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string::npos) return "/";

    auto queryStart = url.find('?', pathStart);
    auto fragStart  = url.find('#', pathStart);
    auto end        = (queryStart < fragStart) ? queryStart : fragStart;

    return url.substr(pathStart,
        end == std::string::npos ? std::string::npos : end - pathStart);
}

std::vector<std::string> PrintApiRouter::SplitPath(std::string const& path)
{
    std::vector<std::string> segments;
    size_t start = (!path.empty() && path[0] == '/') ? 1 : 0;

    while (start < path.size())
    {
        auto end     = path.find('/', start);
        auto segment = path.substr(start,
            end == std::string::npos ? std::string::npos : end - start);

        if (!segment.empty())
            segments.push_back(UrlDecode(segment));

        if (end == std::string::npos) break;
        start = end + 1;
    }

    return segments;
}

std::string PrintApiRouter::UrlDecode(std::string const& s)
{
    std::string result;
    result.reserve(s.size());

    auto hexVal = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return 0;
    };

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            result += static_cast<char>(hexVal(s[i + 1]) * 16 + hexVal(s[i + 2]));
            i += 2;
        }
        else if (s[i] == '+')
        {
            result += ' ';
        }
        else
        {
            result += s[i];
        }
    }

    return result;
}

} // namespace CrumbsBrowser
