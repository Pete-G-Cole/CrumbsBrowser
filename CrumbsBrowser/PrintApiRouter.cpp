#include "pch.h"
#include "PrintApiRouter.hpp"
#include "StringUtils.hpp"
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.ApplicationModel.h>
#include <future>

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
            if (segments.size() == 1 && segments[0] == "ping")
            {
				json responseBody;
                responseBody["status"] = "ok";
                responseBody["app"] = "CrumbsBrowser";
				responseBody["version"] = GetAppVersion();
                SendJsonResponse(args, 200, "OK", responseBody);
                return;
            }
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

		if (method == "POST")
		{
			if (segments.size() == 1 && segments[0] == "print")
			{
				SendJsonResponse(args, 202, "Accepted", HandlePostPrint(ReadRequestBodyJson(args)));
				return;
			}
			if (segments.size() == 1 && segments[0] == "print-to-pdf")
			{
				HandlePostPrintToPdf(args, ReadRequestBodyJson(args));
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

// ===========================================================================
// Print API  —  /ping | /printers/* | /print
//
// Provides an Electron-compatible printing interface backed by WebView2.
// For each new API group add a matching section header and its handlers below.
//
//   GET  /ping              Health-check; confirms running in CrumbsBrowser.
//   GET  /printers          Enumerate available printers.
//   GET  /printers/{name}   Retrieve detail for a named printer.
//   POST /print             Print the current page (silent or via dialog).
//   POST /print-to-pdf      Render the current page to PDF; returns application/pdf binary.
// ===========================================================================

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

// Note: This handler initiates a print operation but does not wait for its completion before responding to the client.
// This design allows the client to remain responsive and handle the print operation's outcome asynchronously, rather than blocking until the print job finishes.
// see: https://learn.microsoft.com/en-us/microsoft-edge/webview2/how-to/print?tabs=dotnetcsharp
json PrintApiRouter::HandlePostPrint(json const& options)
{
    bool silent = options.value("silent", false);

	{
		std::lock_guard lock(m_printMutex);
		if (m_activePrintOperation || m_lastPrintStatus == CoreWebView2PrintStatus::PrinterUnavailable)
		{
			// If a print is already in progress or the printer is unavailable or errored, reject new requests
			// clear the error state ready for next request
			m_lastPrintStatus = CoreWebView2PrintStatus::Succeeded;
			throw std::runtime_error("Printer is currently unavailable or busy with another print job.");
		}
	}

	if (!silent)
	{
		// Show the browser print dialog (non-silent mode)
		m_webView.ShowPrintUI(CoreWebView2PrintDialogKind::Browser);
	}
	else
	{
		// Silent print to the specified (or default) printer
		auto settings = BuildPrintSettings(options);
		{
			std::lock_guard lock(m_printMutex);
			m_activePrintOperation = m_webView.PrintAsync(settings);
		}
		m_activePrintOperation.Completed(
			[this](winrt::Windows::Foundation::IAsyncOperation<CoreWebView2PrintStatus> const& op,
				   winrt::Windows::Foundation::AsyncStatus asyncStatus)
			{
				// NOTE: Once the print operation completes, we store the result status and clear the active operation.
				// This allows subsequent print requests to proceed, while still providing feedback on the last print attempt's outcome.
				// A status of OtherError means something went wrong with the print operation that isn't covered by the other status codes (e.g. an exception was thrown).
				// The client needs to callback to determine what the error was and decide how to proceed (e.g. retry, show error message, etc.)
				// Compute status before acquiring the lock to keep the critical section minimal.
				CoreWebView2PrintStatus status;
				try
				{
					status = (asyncStatus == winrt::Windows::Foundation::AsyncStatus::Completed)
						? op.GetResults()
						: CoreWebView2PrintStatus::OtherError;
				}
				catch (...)
				{
					status = CoreWebView2PrintStatus::OtherError;
				}
				std::lock_guard lock(m_printMutex);
				m_lastPrintStatus = status;
				m_activePrintOperation = nullptr;
			});
	}

    json j;
    j["status"] = "accepted";
    return j;
}

void PrintApiRouter::HandlePostPrintToPdf(
    CoreWebView2WebResourceRequestedEventArgs const& args,
    json const& options)
{
    // Take a deferral so WebView2 keeps the response slot open until the async PDF
    // operation completes and we have called deferral.Complete().
    auto deferral = args.GetDeferral();

    try
    {
        // BuildPdfSettings and PrintToPdfStreamAsync must be called on the STA thread.
        // We start the operation here without blocking, then attach a .Completed() handler
        // that fires on a background/MTA thread — keeping the STA message pump free so
        // WebView2 can deliver the operation's completion IPC.
        auto settings = BuildPdfSettings(options);
        auto printOp  = m_webView.PrintToPdfStreamAsync(settings);

        printOp.Completed(
            [this, args, deferral](
                winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IRandomAccessStream> const& op,
                winrt::Windows::Foundation::AsyncStatus asyncStatus) mutable
            {
                // This callback fires on a background thread — safe to block here.
                try
                {
                    if (asyncStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
                        throw std::runtime_error("PrintToPdfStreamAsync did not complete successfully");

                    auto stream = op.GetResults();
                    auto size   = static_cast<uint32_t>(stream.Size());
                    stream.Seek(0);

                    DataReader reader(stream);
                    // LoadAsync is agile but we are already off the STA, so dispatching
                    // to a further background thread is still the safest pattern.
                    std::async(std::launch::async, [loadOp = reader.LoadAsync(size)]() { loadOp.get(); }).get();

                    std::vector<uint8_t> pdfBytes(size);
                    reader.ReadBytes(pdfBytes);

                    SendBinaryResponse(args, 200, "OK", pdfBytes, "application/pdf");
                }
                catch (winrt::hresult_error const& e)
                {
                    SendErrorResponse(args, 500, winrt::to_string(e.message()));
                }
                catch (std::exception const& e)
                {
                    SendErrorResponse(args, 500, e.what());
                }
                catch (...)
                {
                    SendErrorResponse(args, 500, "Unknown error during PDF generation");
                }

                deferral.Complete();
            });
    }
    catch (std::exception const& e)
    {
        // Construction of settings or the async operation itself failed on the STA thread.
        SendErrorResponse(args, 500, e.what());
        deferral.Complete();
    }
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

PrintApiRouter::PaperSize PrintApiRouter::GetPaperSize(
    std::string const& printerName,
    std::string const& paperName)
{
    auto wPrinterName = utf8_to_utf16(printerName);
    auto wPaperName   = utf8_to_utf16(paperName);

    DWORD n = DeviceCapabilitiesW(wPrinterName.c_str(), nullptr, DC_PAPERNAMES, nullptr, nullptr);
    if (n == (DWORD)-1 || n == 0)
        throw std::runtime_error("Failed to query paper names for printer: " + printerName);

    // DC_PAPERNAMES: parallel array of null-padded 64-wchar slots.
    // DC_PAPERSIZE:  parallel array of POINT, dimensions in tenths of a millimetre.
    std::vector<wchar_t> names(n * 64);
    std::vector<POINT>   sizes(n);
    DeviceCapabilitiesW(wPrinterName.c_str(), nullptr, DC_PAPERNAMES, names.data(), nullptr);
    DeviceCapabilitiesW(wPrinterName.c_str(), nullptr, DC_PAPERSIZE,
        reinterpret_cast<LPWSTR>(sizes.data()), nullptr);

    for (DWORD i = 0; i < n; ++i)
    {
        std::wstring ws(&names[i * 64], 64);
        auto nulPos = ws.find(L'\0');
        if (nulPos != std::wstring::npos) ws.resize(nulPos);

        if (_wcsicmp(ws.c_str(), wPaperName.c_str()) == 0)
        {
            // Tenths of a millimetre → inches  (1 inch = 25.4 mm = 254 tenths)
            return { sizes[i].x / 254.0, sizes[i].y / 254.0 };
        }
    }

    throw std::runtime_error("Paper size not found: '" + paperName +
        "' on printer '" + printerName + "'");
}

std::string PrintApiRouter::ResolvePrinterName(std::string const& printerName)
{
    if (!printerName.empty())
        return printerName;

    DWORD size = 0;
    GetDefaultPrinterW(nullptr, &size);
    if (size == 0)
        throw std::runtime_error("Failed to query default printer name");

    std::vector<wchar_t> buf(size);
    if (!GetDefaultPrinterW(buf.data(), &size))
        throw std::runtime_error("Failed to get default printer name: " + std::to_string(GetLastError()));

    return utf16_to_utf8(buf.data());
}

// ---- Print settings helpers ----------------------------------------

CoreWebView2PrintSettings PrintApiRouter::BuildPrintSettings(json const& options)
{
	auto settings = m_webView.Environment().CreatePrintSettings();

	// collate: boolean → CoreWebView2PrintCollation
	if (options.contains("collate") && options["collate"].is_boolean())
		settings.Collation(options["collate"].get<bool>()
			? CoreWebView2PrintCollation::Collated
			: CoreWebView2PrintCollation::Uncollated);

	// color: boolean → CoreWebView2PrintColorMode
	if (options.contains("color") && options["color"].is_boolean())
		settings.ColorMode(options["color"].get<bool>()
			? CoreWebView2PrintColorMode::Color
			: CoreWebView2PrintColorMode::Grayscale);

	// copies: integer (1–999) → Copies
	if (options.contains("copies") && options["copies"].is_number_integer())
		settings.Copies(static_cast<int32_t>(options["copies"].get<int>()));

	// deviceName: string → PrinterName
	if (options.contains("deviceName") && options["deviceName"].is_string())
		settings.PrinterName(winrt::to_hstring(options["deviceName"].get<std::string>()));

	// dpi: { horizontal, vertical } — no CoreWebView2PrintSettings equivalent

	// duplexMode: "simplex"|"shortEdge"|"longEdge" → CoreWebView2PrintDuplex
	if (options.contains("duplexMode") && options["duplexMode"].is_string())
	{
		auto const& mode = options["duplexMode"].get<std::string>();
		if (mode == "longEdge")
			settings.Duplex(CoreWebView2PrintDuplex::TwoSidedLongEdge);
		else if (mode == "shortEdge")
			settings.Duplex(CoreWebView2PrintDuplex::TwoSidedShortEdge);
		else
			settings.Duplex(CoreWebView2PrintDuplex::OneSided);
	}

	// footer: string → FooterUri (note: WebView2 treats this as a URI, not plain text)
	// header: string → HeaderTitle
	// Both require ShouldPrintHeaderAndFooter = true to take effect.
	{
		bool const hasHeader = options.contains("header") && options["header"].is_string();
		bool const hasFooter = options.contains("footer") && options["footer"].is_string();
		if (hasHeader || hasFooter)
		{
			settings.ShouldPrintHeaderAndFooter(true);
			if (hasHeader)
				settings.HeaderTitle(winrt::to_hstring(options["header"].get<std::string>()));
			if (hasFooter)
				settings.FooterUri(winrt::to_hstring(options["footer"].get<std::string>()));
		}
	}

	// landscape: boolean → CoreWebView2PrintOrientation
	if (options.contains("landscape") && options["landscape"].is_boolean())
		settings.Orientation(options["landscape"].get<bool>()
			? CoreWebView2PrintOrientation::Landscape
			: CoreWebView2PrintOrientation::Portrait);

	// margins: { top, bottom, left, right } pixels → MarginTop/Bottom/Left/Right inches (÷96)
	// marginType ("default"|"none"|"printableArea"|"custom") — no CoreWebView2PrintSettings equivalent
	if (options.contains("margins") && options["margins"].is_object())
	{
		auto const& m   = options["margins"];
		auto px_to_in   = [](double px) { return px / 96.0; };
		if (m.contains("top")    && m["top"].is_number())    settings.MarginTop(px_to_in(m["top"].get<double>()));
		if (m.contains("bottom") && m["bottom"].is_number()) settings.MarginBottom(px_to_in(m["bottom"].get<double>()));
		if (m.contains("left")   && m["left"].is_number())   settings.MarginLeft(px_to_in(m["left"].get<double>()));
		if (m.contains("right")  && m["right"].is_number())  settings.MarginRight(px_to_in(m["right"].get<double>()));
	}

	// pageRanges: [{ from, to }, ...] 0-based → PageRanges string "1-3,5" 1-based
	if (options.contains("pageRanges") && options["pageRanges"].is_array())
	{
		std::string rangeStr;
		for (auto const& r : options["pageRanges"])
		{
			if (!r.is_object()) continue;
			if (!rangeStr.empty()) rangeStr += ',';
			int const from = r.value("from", 0) + 1;
			int const to   = r.value("to",   0) + 1;
			rangeStr += std::to_string(from);
			if (to != from)
				rangeStr += '-' + std::to_string(to);
		}
		settings.PageRanges(winrt::to_hstring(rangeStr));
	}

	// pageSize: named string → MediaSize=Custom + PageWidth/PageHeight (standard inch dimensions)
	// pageSize: { width, height } micrometers → MediaSize=Custom + PageWidth/PageHeight (÷25400)
	if (options.contains("pageSize"))
	{
		auto const& ps = options["pageSize"];
		if (ps.is_string())
		{
            PaperSize paperSize = GetPaperSize(ResolvePrinterName(options.value("deviceName", "")), ps.get<std::string>());
            settings.MediaSize(CoreWebView2PrintMediaSize::Custom);
            settings.PageWidth(paperSize.width);
            settings.PageHeight(paperSize.height);
		}
		else if (ps.is_object() && ps.contains("width") && ps.contains("height"))
		{
			settings.MediaSize(CoreWebView2PrintMediaSize::Custom);
			settings.PageWidth(ps["width"].get<double>()  / 25400.0);
			settings.PageHeight(ps["height"].get<double>() / 25400.0);
		}
	}

	// pagesPerSheet: integer (1,2,4,6,9,16) → PagesPerSide
	if (options.contains("pagesPerSheet") && options["pagesPerSheet"].is_number_integer())
		settings.PagesPerSide(static_cast<int32_t>(options["pagesPerSheet"].get<int>()));

	// printBackground: boolean → ShouldPrintBackgrounds
	if (options.contains("printBackground") && options["printBackground"].is_boolean())
		settings.ShouldPrintBackgrounds(options["printBackground"].get<bool>());

	// scaleFactor: number (0–200, percentage) → ScaleFactor (0.1–2.0, i.e. ÷100)
	if (options.contains("scaleFactor") && options["scaleFactor"].is_number())
		settings.ScaleFactor(options["scaleFactor"].get<double>() / 100.0);

	// silent: boolean — handled by the caller (HandlePostPrint), not a settings field

	// ShouldPrintSelectionOnly — no Electron equivalent

	return settings;
}

// Maps Electron's printToPDF() options to CoreWebView2PrintSettings.
// Key differences from BuildPrintSettings:
//   - scale: 0.1–2.0 direct (not a percentage)
//   - margins: inches (not pixels)
//   - displayHeaderFooter + headerTemplate/footerTemplate instead of header/footer
//   - no printer-specific options (copies, duplex, collate, pagesPerSheet, deviceName)
CoreWebView2PrintSettings PrintApiRouter::BuildPdfSettings(json const& options)
{
	auto settings = m_webView.Environment().CreatePrintSettings();

	// color: boolean → CoreWebView2PrintColorMode
	if (options.contains("color") && options["color"].is_boolean())
		settings.ColorMode(options["color"].get<bool>()
			? CoreWebView2PrintColorMode::Color
			: CoreWebView2PrintColorMode::Grayscale);

	// landscape: boolean → CoreWebView2PrintOrientation
	if (options.contains("landscape") && options["landscape"].is_boolean())
		settings.Orientation(options["landscape"].get<bool>()
			? CoreWebView2PrintOrientation::Landscape
			: CoreWebView2PrintOrientation::Portrait);

	// printBackground: boolean → ShouldPrintBackgrounds
	if (options.contains("printBackground") && options["printBackground"].is_boolean())
		settings.ShouldPrintBackgrounds(options["printBackground"].get<bool>());

	// scale: number 0.1–2.0 → ScaleFactor (direct, no conversion; contrast with print()'s scaleFactor÷100)
	if (options.contains("scale") && options["scale"].is_number())
		settings.ScaleFactor(options["scale"].get<double>());

	// margins: top/bottom/left/right in inches (Electron printToPDF convention)
	if (options.contains("margins") && options["margins"].is_object())
	{
		auto const& m = options["margins"];
		if (m.contains("top")    && m["top"].is_number())    settings.MarginTop(m["top"].get<double>());
		if (m.contains("bottom") && m["bottom"].is_number()) settings.MarginBottom(m["bottom"].get<double>());
		if (m.contains("left")   && m["left"].is_number())   settings.MarginLeft(m["left"].get<double>());
		if (m.contains("right")  && m["right"].is_number())  settings.MarginRight(m["right"].get<double>());
	}

	// pageRanges: [{ from, to }, ...] 0-based → PageRanges string "1-3,5" 1-based
	if (options.contains("pageRanges") && options["pageRanges"].is_array())
	{
		std::string rangeStr;
		for (auto const& r : options["pageRanges"])
		{
			if (!r.is_object()) continue;
			if (!rangeStr.empty()) rangeStr += ',';
			int const from = r.value("from", 0) + 1;
			int const to   = r.value("to",   0) + 1;
			rangeStr += std::to_string(from);
			if (to != from)
				rangeStr += '-' + std::to_string(to);
		}
		settings.PageRanges(winrt::to_hstring(rangeStr));
	}

	// pageSize: named string (looked up via default printer) or { width, height } in micrometres
	if (options.contains("pageSize"))
	{
		auto const& ps = options["pageSize"];
		if (ps.is_string())
		{
			PaperSize paperSize = GetPaperSize(ResolvePrinterName(""), ps.get<std::string>());
			settings.MediaSize(CoreWebView2PrintMediaSize::Custom);
			settings.PageWidth(paperSize.width);
			settings.PageHeight(paperSize.height);
		}
		else if (ps.is_object() && ps.contains("width") && ps.contains("height"))
		{
			settings.MediaSize(CoreWebView2PrintMediaSize::Custom);
			settings.PageWidth(ps["width"].get<double>()  / 25400.0);
			settings.PageHeight(ps["height"].get<double>() / 25400.0);
		}
	}

	// displayHeaderFooter + headerTemplate / footerTemplate
	{
		bool const hasHeader = options.contains("headerTemplate") && options["headerTemplate"].is_string();
		bool const hasFooter = options.contains("footerTemplate") && options["footerTemplate"].is_string();
		bool const display   = options.value("displayHeaderFooter", false);
		if (display || hasHeader || hasFooter)
		{
			settings.ShouldPrintHeaderAndFooter(true);
			if (hasHeader)
				settings.HeaderTitle(winrt::to_hstring(options["headerTemplate"].get<std::string>()));
			if (hasFooter)
				settings.FooterUri(winrt::to_hstring(options["footerTemplate"].get<std::string>()));
		}
	}

	return settings;
}

// ---------------------------------------------------------------------------
// Request helpers
// ---------------------------------------------------------------------------

json PrintApiRouter::ReadRequestBodyJson(
    CoreWebView2WebResourceRequestedEventArgs const& args)
{
    auto content = args.Request().Content();
    if (!content) return json::object();

    auto size = content.Size();
    if (size == 0) return json::object();

    content.Seek(0);
    DataReader reader(content);
    // LoadAsync().get() must not be called on the STA thread directly as it triggers
    // a WinRT deadlock assertion. Dispatch the blocking wait to a background thread.
    std::async(std::launch::async, [loadOp = reader.LoadAsync(static_cast<uint32_t>(size))]() { loadOp.get(); }).get();

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    reader.ReadBytes(buf);

    std::string bodyStr(reinterpret_cast<char const*>(buf.data()), buf.size());
    auto parsed = json::parse(bodyStr, nullptr, false);
    return parsed.is_discarded() ? json::object() : parsed;
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
    // StoreAsync().get() must not be called on the STA thread directly as it triggers
    // a WinRT deadlock assertion. Dispatch the blocking wait to a background thread;
    // InMemoryRandomAccessStream is agile so the IAsyncAction is safe to use cross-thread.
    std::async(std::launch::async, [storeOp = writer.StoreAsync()]() { storeOp.get(); }).get();
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

void PrintApiRouter::SendBinaryResponse(
    CoreWebView2WebResourceRequestedEventArgs const& args,
    int statusCode,
    std::string const& statusText,
    std::vector<uint8_t> const& data,
    std::string const& contentType)
{
    auto stream = InMemoryRandomAccessStream();
    auto writer = DataWriter(stream);
    writer.WriteBytes(winrt::array_view<uint8_t const>(data.data(), static_cast<uint32_t>(data.size())));
    // StoreAsync().get() must not be called on the STA thread directly — dispatch to background thread.
    std::async(std::launch::async, [storeOp = writer.StoreAsync()]() { storeOp.get(); }).get();
    writer.DetachStream();
    stream.Seek(0);

    auto headers = winrt::to_hstring(
        "Content-Type: " + contentType + "\r\n"
        "Content-Length: " + std::to_string(data.size()) + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type");

    auto response = m_webView.Environment().CreateWebResourceResponse(
        stream, statusCode, winrt::to_hstring(statusText), headers);

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

std::string PrintApiRouter::GetAppVersion()
{
    try
    {
        auto package = winrt::Windows::ApplicationModel::Package::Current();
        auto version = package.Id().Version();
        
        return std::to_string(version.Major) + "." +
               std::to_string(version.Minor) + "." +
               std::to_string(version.Build) + "." +
               std::to_string(version.Revision);
    }
    catch (...)
    {
        return "0.0.0.0";  // Fallback if package info unavailable
    }
}

} // namespace CrumbsBrowser
