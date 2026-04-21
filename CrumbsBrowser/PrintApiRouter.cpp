#include "pch.h"
#include "PrintApiRouter.hpp"
#include "StringUtils.hpp"
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.ApplicationModel.h>
#include <future>
#include <random>
#include <optional>

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
					auto const body = ReadRequestBodyJson(args);
					bool const silent = body.value("silent", false);
					SendJsonResponse(args, 202, "Accepted", HandlePostPrint(BuildPrintSettings(body), silent));
					return;
				}
			if (segments.size() == 1 && segments[0] == "print-to-pdf")
			{
				HandlePostPrintToPdf(args, BuildPdfSettings(ReadRequestBodyJson(args)));
				return;
			}
		}

        // ---- ScriptX.Services API emulation (/api and /api/v1/...) --------
        // GET /api  — service description
        if (method == "GET" && segments.size() == 1 && segments[0] == "api")
        {
            SendJsonResponse(args, 200, "OK", EmulateSXHandleGetServiceDescription());
            return;
        }

        // All remaining SX routes require at least /api/v1/<group>/...
        // segments[0]=="api", segments[1]=="v1"
        if (segments.size() >= 3 && segments[0] == "api" && segments[1] == "v1")
        {
            auto const& group = segments[2];

            // ---- Licensing ------------------------------------------------
            if (group == "licensing")
            {
                if (method == "GET" && segments.size() == 3)
                {
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetLicensing());
                    return;
                }
                if (method == "GET" && segments.size() == 4 && segments[3] == "ping")
                {
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetLicensingPing());
                    return;
                }
                if (method == "POST" && segments.size() == 3)
                {
                    // POST /api/v1/licensing — install license. We just return the
                    // existing license stub; actual installation is not applicable.
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetLicensing());
                    return;
                }
            }

            // ---- PrintHtml ------------------------------------------------
            if (group == "printHtml")
            {
                if (method == "GET" && segments.size() == 4 && segments[3] == "settings")
                {
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetHtmlPrintSettings());
                    return;
                }
                // GET /api/v1/printHtml/htmlPrintDefaults/{units}
                if (method == "GET" && segments.size() == 5 && segments[3] == "htmlPrintDefaults")
                {
                    int units = 0;
                    try { units = std::stoi(segments[4]); } catch (...) {}
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetHtmlPrintDefaults(units));
                    return;
                }
                // GET /api/v1/printHtml/htmlPrintDefaults/?units=0  (query-string form)
                if (method == "GET" && segments.size() == 4 && segments[3] == "htmlPrintDefaults")
                {
                    int units = 0;
                    auto q = ExtractQueryParam(winrt::to_string(args.Request().Uri()), "units");
                    try { if (!q.empty()) units = std::stoi(q); } catch (...) {}
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetHtmlPrintDefaults(units));
                    return;
                }
                // GET /api/v1/printHtml/deviceinfo/{deviceName}/{units}
                if (method == "GET" && segments.size() == 6 && segments[3] == "deviceinfo")
                {
                    int units = 0;
                    try { units = std::stoi(segments[5]); } catch (...) {}
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetDeviceInfo(segments[4], units));
                    return;
                }
                if (method == "POST" && segments.size() == 4 && segments[3] == "print")
                {
                    EmulateSXHandlePostPrintHtml(args, ReadRequestBodyJson(args));
                    return;
                }
                // GET /api/v1/printHtml/status/{jobToken}
                if (method == "GET" && segments.size() == 5 && segments[3] == "status")
                {
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetJobStatus(segments[4]));
                    return;
                }
                // PUT /api/v1/printHtml/canceljob/{jobToken}
                if (method == "PUT" && segments.size() == 5 && segments[3] == "canceljob")
                {
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleCancelJob(segments[4]));
                    return;
                }
                // GET /api/v1/printHtml/download/{jobToken}
                if (method == "GET" && segments.size() == 5 && segments[3] == "download")
                {
                    EmulateSXHandleGetDownload(args, segments[4]);
                    return;
                }
            }

            // ---- PrintPdf -------------------------------------------------
            if (group == "printPdf")
            {
                if (method == "POST" && segments.size() == 4 && segments[3] == "print")
                {
                    // Printing an external PDF is not supported by WebView2 in this context.
                    json err;
                    err["status"]  = 3; // SoftError
                    err["message"] = "printPdf is not supported by CrumbsBrowser";
                    SendJsonResponse(args, 200, "OK", err);
                    return;
                }
                if (method == "GET" && segments.size() == 5 && segments[3] == "status")
                {
                    SendJsonResponse(args, 200, "OK", EmulateSXHandleGetJobStatus(segments[4]));
                    return;
                }
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
// For each new API group add a matching section header and its handlers below//
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
json PrintApiRouter::HandlePostPrint(CoreWebView2PrintSettings const& settings, bool silent)
{
	{
		std::lock_guard lock(m_printMutex);
		if (m_activePrintOperation || m_activePdfOperation || m_lastPrintStatus == CoreWebView2PrintStatus::PrinterUnavailable)
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
	CoreWebView2PrintSettings const& settings)
{
	{
		std::lock_guard lock(m_printMutex);
		if (m_activePrintOperation || m_activePdfOperation || m_lastPrintStatus == CoreWebView2PrintStatus::PrinterUnavailable)
		{
			// If a print is already in progress or the printer is unavailable or errored, reject new requests
			// clear the error state ready for next request
			m_lastPrintStatus = CoreWebView2PrintStatus::Succeeded;
			throw std::runtime_error("Printer is currently unavailable or busy with another print job.");
		}
	}

	// Take a deferral so WebView2 keeps the response slot open until the async PDF
	// operation completes and we have called deferral.Complete().
	auto deferral = args.GetDeferral();

	try
	{
		// PrintToPdfStreamAsync must be called on the STA thread.
		// We start the operation here without blocking, then attach a .Completed() handler
		// that fires on a background/MTA thread — keeping the STA message pump free so
		// WebView2 can deliver the operation's completion IPC.
		{
			std::lock_guard lock(m_printMutex);
			m_activePdfOperation = m_webView.PrintToPdfStreamAsync(settings);
		}

		m_activePdfOperation.Completed(
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

				{
					std::lock_guard lock(m_printMutex);
					m_activePdfOperation = nullptr;
				}
				deferral.Complete();
			});
	}
	catch (std::exception const& e)
	{
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

// At the moment, assumes electron print() options are being sent
//
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
        L"Access-Control-Allow-Headers: Content-Type, Authorization, x-meadroid-path");

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
        L"Access-Control-Allow-Headers: Content-Type, Authorization, x-meadroid-path\r\n"
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
        "Access-Control-Allow-Headers: Content-Type, Authorization, x-meadroid-path");

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

std::string PrintApiRouter::ExtractQueryParam(std::string const& url, std::string const& name)
{
    auto queryStart = url.find('?');
    if (queryStart == std::string::npos) return {};

    auto fragStart = url.find('#', queryStart);
    auto query = url.substr(queryStart + 1,
        fragStart == std::string::npos ? std::string::npos : fragStart - queryStart - 1);

    size_t pos = 0;
    while (pos < query.size())
    {
        auto ampPos = query.find('&', pos);
        auto pair   = query.substr(pos, ampPos == std::string::npos ? std::string::npos : ampPos - pos);
        auto eqPos  = pair.find('=');
        auto key    = eqPos == std::string::npos ? pair : pair.substr(0, eqPos);
        if (UrlDecode(key) == name)
            return eqPos == std::string::npos ? std::string{} : UrlDecode(pair.substr(eqPos + 1));
        if (ampPos == std::string::npos) break;
        pos = ampPos + 1;
    }
    return {};
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

// ===========================================================================
// ScriptX.Services API emulation
//
//   GET  /api                                        ServiceDescription
//   GET  /api/v1/licensing[/ping]                   License / LicenseOptions
//   POST /api/v1/licensing                           License (stub)
//   GET  /api/v1/printHtml/settings                 HtmlPrintSettings
//   GET  /api/v1/printHtml/deviceinfo/{dev}/{units} DeviceSettings
//   GET  /api/v1/printHtml/htmlPrintDefaults/{units} PrintHtmlDefaultSettings
//   POST /api/v1/printHtml/print                    Print → QueuedToDevice|QueuedToFile
//   GET  /api/v1/printHtml/status/{token}           JobStatus
//   PUT  /api/v1/printHtml/canceljob/{token}        JobStatus (cancelled)


// ---------------------------------------------------------------------------
// GET /api  — ServiceDescription
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetServiceDescription()
{
    // Collect available printer names from the existing helper
    json printerNames = json::array();
    try
    {
        auto printers = HandleGetPrinters();
        for (auto const& p : printers)
            if (p.contains("name")) printerNames.push_back(p["name"]);
    }
    catch (...) {}

    // Parse version from GetAppVersion() "major.minor.build.revision"
    auto vstr = GetAppVersion();
    int major = 0, minor = 0, build = 0, revision = 0;
    sscanf_s(vstr.c_str(), "%d.%d.%d.%d", &major, &minor, &build, &revision);

    json ver;
    ver["major"]    = major;
    ver["minor"]    = minor;
    ver["build"]    = build;
    ver["revision"] = revision;

    json j;
    j["serviceClass"]      = 3;    // 3 = WindowsPC
    j["currentAPIVersion"] = "v1";
    j["serviceVersion"]    = ver;
    j["serverVersion"]     = ver;
    j["availablePrinters"] = printerNames;
    j["printHTML"]         = true;
    j["printPDF"]          = false; // external PDF not supported
    j["printDIRECT"]       = false;
    return j;
}

// ---------------------------------------------------------------------------
// GET /api/v1/licensing/ping  — LicenseOptions
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetLicensingPing()
{
    json j;
    j["basicHtmlPrinting"]  = true;
    j["advancedPrinting"]   = true;
    j["enhancedFormatting"] = true;
    j["printPdf"]           = false;
    j["printRaw"]           = false;
    return j;
}

// ---------------------------------------------------------------------------
// GET /api/v1/licensing  — License
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetLicensing()
{
    json j;
    j["guid"]            = "00000000-0000-0000-0000-000000000000";
    j["company"]         = "CrumbsBrowser";
    j["companyHomePage"] = "";
    j["revision"]        = 1;
    j["from"]            = "2000-01-01T00:00:00Z";
    j["to"]              = "2999-12-31T00:00:00Z";
    j["options"]         = EmulateSXHandleGetLicensingPing();
    j["domains"]         = json::array();
    return j;
}

// ---------------------------------------------------------------------------
// Helpers: build JSON representations of settings/device from WebView2 state
// ---------------------------------------------------------------------------

json PrintApiRouter::EmulateSXHtmlPrintSettingsToJson()
{
    // Return conservative defaults matching what WebView2 will use by default.
    json margins;
    margins["left"]   = "1.0";
    margins["top"]    = "1.0";
    margins["bottom"] = "1.0";
    margins["right"]  = "1.0";

    json page;
    page["orientation"] = 0; // Default
    page["units"]       = 1; // Inches
    page["margins"]     = margins;

    json j;
    j["header"]                       = "&D&bPage &p of &P";
    j["footer"]                       = "";
    j["headerFooterFont"]             = "Arial 10pt";
    j["page"]                         = page;
    j["viewScale"]                    = 100;
    j["printBackgroundColorsAndImages"] = 0; // Default
    j["pageRange"]                    = "";
    j["printingPass"]                 = 0; // All
    j["jobTitle"]                     = "";
    return j;
}

json PrintApiRouter::EmulateSXDeviceSettingsToJson(std::string const& printerName, int units)
{
    // Resolve "default" keyword to the actual default printer name
    std::string resolved;
    try { resolved = ResolvePrinterName(printerName == "default" ? "" : printerName); }
    catch (...) { resolved = printerName; }

    json j;
    j["printerName"]   = resolved;
    j["copies"]        = 1;
    j["collate"]       = 0;  // Default
    j["duplex"]        = 0;  // Default
    j["units"]         = units;

    // Populate from printer info if available
    try
    {
        auto info = HandleGetPrinterByName(resolved);
        j["isDefault"]   = info.value("isDefault", false);
        j["port"]        = info.value("portName", "");
        j["driverName"]  = info.value("driverName", "");

        if (info.contains("defaults"))
        {
            auto const& d = info["defaults"];
            if (d.contains("copies"))  j["copies"]  = d["copies"];
            if (d.contains("collate")) j["collate"]  = d["collate"].get<bool>() ? 1 : 2;
            if (d.contains("duplex"))
            {
                auto const& dux = d["duplex"].get<std::string>();
                j["duplex"] = (dux == "long-edge") ? 2 : (dux == "short-edge") ? 3 : 1;
            }
        }
        if (info.contains("capabilities"))
        {
            auto const& caps = info["capabilities"];
            if (caps.contains("paperSizes") && caps["paperSizes"].is_array() && !caps["paperSizes"].empty())
                j["paperSizeName"] = caps["paperSizes"][0];
        }
    }
    catch (...) {}

    // Provide a minimal unprintable margins block
    json unp;
    unp["left"] = unp["top"] = unp["bottom"] = unp["right"] = "0.0";
    j["unprintableMargins"] = unp;
    j["bins"]  = json::array();
    j["forms"] = json::array();

    return j;
}

// ---------------------------------------------------------------------------
// GET /api/v1/printHtml/settings  — HtmlPrintSettings
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetHtmlPrintSettings()
{
    return EmulateSXHtmlPrintSettingsToJson();
}

// ---------------------------------------------------------------------------
// GET /api/v1/printHtml/deviceinfo/{deviceName}/{units}  — DeviceSettings
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetDeviceInfo(std::string const& deviceName, int units)
{
    return EmulateSXDeviceSettingsToJson(deviceName, units);
}

// ---------------------------------------------------------------------------
// GET /api/v1/printHtml/htmlPrintDefaults/{units}  — PrintHtmlDefaultSettings
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetHtmlPrintDefaults(int units)
{
    json printerNames = json::array();
    try
    {
        auto printers = HandleGetPrinters();
        for (auto const& p : printers)
            if (p.contains("name")) printerNames.push_back(p["name"]);
    }
    catch (...) {}

    json j;
    j["settings"]          = EmulateSXHtmlPrintSettingsToJson();
    j["device"]            = EmulateSXDeviceSettingsToJson("default", units);
    j["availablePrinters"] = printerNames;
    return j;
}

// ---------------------------------------------------------------------------
// EmulateSXBuildPrintSettings
// Maps HtmlPrintSettings + DevicePrintSettings → CoreWebView2PrintSettings
// ---------------------------------------------------------------------------
CoreWebView2PrintSettings PrintApiRouter::EmulateSXBuildPrintSettings(
    json const& html,
    json const& device)
{
    auto settings = m_webView.Environment().CreatePrintSettings();

    // --- Device settings ---

    // printerName
    if (device.contains("printerName") && device["printerName"].is_string())
    {
        auto const& name = device["printerName"].get<std::string>();
        if (!name.empty())
            settings.PrinterName(winrt::to_hstring(name));
    }

    // copies
    if (device.contains("copies") && device["copies"].is_number_integer())
        settings.Copies(static_cast<int32_t>(device["copies"].get<int>()));

    // collate: 0=Default, 1=True, 2=False
    if (device.contains("collate") && device["collate"].is_number_integer())
    {
        int c = device["collate"].get<int>();
        if (c == 1) settings.Collation(CoreWebView2PrintCollation::Collated);
        else if (c == 2) settings.Collation(CoreWebView2PrintCollation::Uncollated);
    }

    // duplex: 0=Default, 1=Simplex, 2=Vertical(LongEdge), 3=Horizontal(ShortEdge)
    if (device.contains("duplex") && device["duplex"].is_number_integer())
    {
        switch (device["duplex"].get<int>())
        {
            case 1: settings.Duplex(CoreWebView2PrintDuplex::OneSided);          break;
            case 2: settings.Duplex(CoreWebView2PrintDuplex::TwoSidedLongEdge);  break;
            case 3: settings.Duplex(CoreWebView2PrintDuplex::TwoSidedShortEdge); break;
            default: break;
        }
    }

    // paperSizeName — look up dimensions from the printer
    if (device.contains("paperSizeName") && device["paperSizeName"].is_string())
    {
        auto const& szName = device["paperSizeName"].get<std::string>();
        std::string printerName;
        if (device.contains("printerName") && device["printerName"].is_string())
            printerName = device["printerName"].get<std::string>();

        try
        {
            auto sz = GetPaperSize(ResolvePrinterName(printerName), szName);
            settings.MediaSize(CoreWebView2PrintMediaSize::Custom);
            settings.PageWidth(sz.width);
            settings.PageHeight(sz.height);
        }
        catch (...) { /* fall through — leave at default */ }
    }

    // --- HtmlPrintSettings ---

    // page.orientation: 0=Default, 1=Landscape, 2=Portrait
    if (html.contains("page") && html["page"].is_object())
    {
        auto const& page = html["page"];

        if (page.contains("orientation") && page["orientation"].is_number_integer())
        {
            switch (page["orientation"].get<int>())
            {
                case 1: settings.Orientation(CoreWebView2PrintOrientation::Landscape); break;
                case 2: settings.Orientation(CoreWebView2PrintOrientation::Portrait);  break;
                default: break;
            }
        }

        // margins — parse as the given unit and convert to inches for WebView2
        if (page.contains("margins") && page["margins"].is_object())
        {
            auto const& m   = page["margins"];
            int units = page.value("units", 1); // 1=inches, 2=mm, 0=default(inches)

            auto marginIn = [&](char const* key) -> std::optional<double>
            {
                if (m.contains(key) && m[key].is_string())
                    return EmulateSXParseMargin(m[key].get<std::string>(), units);
                return std::nullopt;
            };

            if (auto v = marginIn("top"))    settings.MarginTop(*v);
            if (auto v = marginIn("bottom")) settings.MarginBottom(*v);
            if (auto v = marginIn("left"))   settings.MarginLeft(*v);
            if (auto v = marginIn("right"))  settings.MarginRight(*v);
        }
    }

    // viewScale: integer percentage → ScaleFactor (÷100)
    if (html.contains("viewScale") && html["viewScale"].is_number_integer())
    {
        double scale = html["viewScale"].get<int>() / 100.0;
        if (scale >= 0.1 && scale <= 2.0)
            settings.ScaleFactor(scale);
    }

    // printBackgroundColorsAndImages: 0=Default, 1=True, 2=False
    if (html.contains("printBackgroundColorsAndImages") && html["printBackgroundColorsAndImages"].is_number_integer())
    {
        int bg = html["printBackgroundColorsAndImages"].get<int>();
        if (bg == 1) settings.ShouldPrintBackgrounds(true);
        else if (bg == 2) settings.ShouldPrintBackgrounds(false);
    }

    // pageRange: already 1-based string e.g. "1-3,5" — pass directly
    if (html.contains("pageRange") && html["pageRange"].is_string())
    {
        auto const& pr = html["pageRange"].get<std::string>();
        if (!pr.empty())
            settings.PageRanges(winrt::to_hstring(pr));
    }

    // header / footer
    {
        bool const hasHeader = html.contains("header") && html["header"].is_string() && !html["header"].get<std::string>().empty();
        bool const hasFooter = html.contains("footer") && html["footer"].is_string() && !html["footer"].get<std::string>().empty();
        if (hasHeader || hasFooter)
        {
            settings.ShouldPrintHeaderAndFooter(true);
            if (hasHeader) settings.HeaderTitle(winrt::to_hstring(html["header"].get<std::string>()));
            if (hasFooter) settings.FooterUri(winrt::to_hstring(html["footer"].get<std::string>()));
        }
    }

    return settings;
}

// ---------------------------------------------------------------------------
// GET /api/v1/printHtml/status/{jobToken}  — JobStatus
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleGetJobStatus(std::string const& jobToken)
{
    json j;
    j["jobIdentifier"] = jobToken;

    std::lock_guard lock(m_printMutex);

    // Check if there is a completed download waiting
    if (m_sxPendingDownloads.count(jobToken))
    {
        j["status"]  = 102; // CompletedWaitingForCollection
        j["message"] = "PDF ready for download";
        return j;
    }

    // Check explicit status map
    if (m_sxJobStatus.count(jobToken))
    {
        j["status"]  = m_sxJobStatus.at(jobToken);
        j["message"] = "";
        return j;
    }

    // Active jobs
    if (m_sxActivePdfJobToken == jobToken)
    {
        j["status"]  = 5; // Printing (PDF)
        j["message"] = "Generating PDF";
        return j;
    }
    if (m_sxActivePrintJobToken == jobToken)
    {
        j["status"]  = 5; // Printing
        j["message"] = "Printing";
        return j;
    }

    j["status"]  = -1; // ItemError — unknown token
    j["message"] = "Unknown job token";
    return j;
}

// ---------------------------------------------------------------------------
// PUT /api/v1/printHtml/canceljob/{jobToken}  — JobStatus
// ---------------------------------------------------------------------------
json PrintApiRouter::EmulateSXHandleCancelJob(std::string const& jobToken)
{
    {
        std::lock_guard lock(m_printMutex);

        if (m_sxActivePdfJobToken == jobToken && m_activePdfOperation)
        {
            m_activePdfOperation.Cancel();
            m_sxActivePdfJobToken.clear();
            m_sxJobStatus[jobToken] = -2; // Abandoned
        }
        else if (m_sxActivePrintJobToken == jobToken && m_activePrintOperation)
        {
            m_activePrintOperation.Cancel();
            m_sxActivePrintJobToken.clear();
            m_sxJobStatus[jobToken] = -2; // Abandoned
        }
        else if (m_sxJobStatus.count(jobToken))
        {
            m_sxJobStatus[jobToken] = -2; // Abandoned
        }
        // Remove any pending download for this token
        m_sxPendingDownloads.erase(jobToken);
    }

    return EmulateSXHandleGetJobStatus(jobToken);
}

// ---------------------------------------------------------------------------
// GET /api/v1/printHtml/download/{jobToken}  — binary PDF
// ---------------------------------------------------------------------------
void PrintApiRouter::EmulateSXHandleGetDownload(
    CoreWebView2WebResourceRequestedEventArgs const& args,
    std::string const& jobToken)
{
    std::vector<uint8_t> bytes;
    {
        std::lock_guard lock(m_printMutex);
        auto it = m_sxPendingDownloads.find(jobToken);
        if (it == m_sxPendingDownloads.end())
        {
            SendErrorResponse(args, 404, "No download available for job: " + jobToken);
            return;
        }
        bytes = std::move(it->second);
        m_sxPendingDownloads.erase(it);
        m_sxJobStatus[jobToken] = 100; // Collected
    }
    SendBinaryResponse(args, 200, "OK", bytes, "application/pdf");
}

// ---------------------------------------------------------------------------
// POST /api/v1/printHtml/print  — PrintHtmlDescription → Print response
//
// contentType: 1=Url (not supported here — we print the current page)
//              2=Html, 4=InnerHtml, 8=String (all treated as current page)
// device.printToFileName set → PrintToPdfStreamAsync (QueuedToFile=2)
// device.printToFileName absent → PrintAsync silent (QueuedToDevice=1)
// ---------------------------------------------------------------------------
void PrintApiRouter::EmulateSXHandlePostPrintHtml(
    CoreWebView2WebResourceRequestedEventArgs const& args,
    json const& body)
{
    // Guard against concurrent operations
    {
        std::lock_guard lock(m_printMutex);
        if (m_activePrintOperation || m_activePdfOperation)
        {
            json err;
            err["status"]  = 3; // SoftError
            err["message"] = "A print operation is already in progress";
            SendJsonResponse(args, 200, "OK", err);
            return;
        }
    }

    json const& htmlSettings   = body.contains("settings") ? body["settings"] : json::object();
    json const& deviceSettings = body.contains("device")   ? body["device"]   : json::object();

    bool const printToFile = deviceSettings.contains("printToFileName")
        && deviceSettings["printToFileName"].is_string()
        && !deviceSettings["printToFileName"].get<std::string>().empty();

    auto settings = EmulateSXBuildPrintSettings(htmlSettings, deviceSettings);
    auto jobToken = EmulateSXGenerateJobToken();

    if (printToFile)
    {
        // --- PDF output path (QueuedToFile) ---
        auto deferral = args.GetDeferral();
        {
            std::lock_guard lock(m_printMutex);
            m_activePdfOperation      = m_webView.PrintToPdfStreamAsync(settings);
            m_sxActivePdfJobToken     = jobToken;
            m_sxJobStatus[jobToken]   = 5; // Printing
        }

        m_activePdfOperation.Completed(
            [this, args, deferral, jobToken](
                winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IRandomAccessStream> const& op,
                winrt::Windows::Foundation::AsyncStatus asyncStatus) mutable
            {
                try
                {
                    if (asyncStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
                        throw std::runtime_error("PrintToPdfStreamAsync cancelled or failed");

                    auto stream = op.GetResults();
                    auto size   = static_cast<uint32_t>(stream.Size());
                    stream.Seek(0);

                    DataReader reader(stream);
                    std::async(std::launch::async, [loadOp = reader.LoadAsync(size)]() { loadOp.get(); }).get();

                    std::vector<uint8_t> pdfBytes(size);
                    reader.ReadBytes(pdfBytes);

                    {
                        std::lock_guard lock(m_printMutex);
                        m_sxPendingDownloads[jobToken] = std::move(pdfBytes);
                        m_sxJobStatus[jobToken]        = 102; // CompletedWaitingForCollection
                        m_sxActivePdfJobToken.clear();
                        m_activePdfOperation = nullptr;
                    }

                    // Respond with QueuedToFile — client polls status then calls download
                    json resp;
                    resp["status"]        = 2; // QueuedToFile
                    resp["jobIdentifier"] = jobToken;
                    resp["message"]       = "";
                    SendJsonResponse(args, 200, "OK", resp);
                }
                catch (std::exception const& e)
                {
                    {
                        std::lock_guard lock(m_printMutex);
                        m_sxJobStatus[jobToken] = -1; // ItemError
                        m_sxActivePdfJobToken.clear();
                        m_activePdfOperation = nullptr;
                    }
                    SendErrorResponse(args, 500, e.what());
                }
                catch (...)
                {
                    {
                        std::lock_guard lock(m_printMutex);
                        m_sxJobStatus[jobToken] = -1;
                        m_sxActivePdfJobToken.clear();
                        m_activePdfOperation = nullptr;
                    }
                    SendErrorResponse(args, 500, "Unknown error during PDF generation");
                }
                deferral.Complete();
            });
    }
    else
    {
        // --- Device print path (QueuedToDevice) ---
        {
            std::lock_guard lock(m_printMutex);
            m_activePrintOperation      = m_webView.PrintAsync(settings);
            m_sxActivePrintJobToken     = jobToken;
            m_sxJobStatus[jobToken]     = 5; // Printing
        }

        m_activePrintOperation.Completed(
            [this, jobToken](
                winrt::Windows::Foundation::IAsyncOperation<CoreWebView2PrintStatus> const& op,
                winrt::Windows::Foundation::AsyncStatus asyncStatus)
            {
                CoreWebView2PrintStatus printStatus;
                try
                {
                    printStatus = (asyncStatus == winrt::Windows::Foundation::AsyncStatus::Completed)
                        ? op.GetResults()
                        : CoreWebView2PrintStatus::OtherError;
                }
                catch (...) { printStatus = CoreWebView2PrintStatus::OtherError; }

                std::lock_guard lock(m_printMutex);
                m_lastPrintStatus = printStatus;
                m_sxJobStatus[jobToken] = (printStatus == CoreWebView2PrintStatus::Succeeded)
                    ? 6    // Completed
                    : -1;  // ItemError
                m_sxActivePrintJobToken.clear();
                m_activePrintOperation = nullptr;
            });

        // Respond immediately — client can fire-and-forget or poll status
        json resp;
        resp["status"]        = 1; // QueuedToDevice
        resp["jobIdentifier"] = jobToken;
        resp["message"]       = "";
        SendJsonResponse(args, 200, "OK", resp);
    }
}

std::string PrintApiRouter::EmulateSXGenerateJobToken()
{
    static std::mt19937_64 rng{ std::random_device{}() };
    static std::uniform_int_distribution<uint64_t> dist;
    uint64_t a = dist(rng), b = dist(rng);
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx", a, b);
    return std::string(buf);
}

double PrintApiRouter::EmulateSXParseMargin(std::string const& value, int units)
{
    double v = 0.0;
    try { v = std::stod(value); } catch (...) { return 0.0; }
    // units: 0=default (inches), 1=inches, 2=mm
    if (units == 2)
        return v / 25.4;
    return v;
}

} // namespace CrumbsBrowser
