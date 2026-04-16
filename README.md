# Crumbs Browser

This is a project to create a web browser for Windows 11 and later with Crumbs - yet another browser with useful features for delivering web based apps to users.

The project is still in the early stages of development, but the goal is to create a browser that allows developers to easily customize the app experience for their users.

## Configuration

The browser can be configured using the JSON file / environment variables pattern used by .NET applications. 

`appsettings.json` is the default configuration file for the browser. It can be used to set the default URL, arguments, and security settings for the browser.

The configuration heirarchy (lowest to highest) is:

`<.exe folder>\appsettings.json`.

`C:\Users\<username>\AppData\Roaming\Crumbs\appsettings.json`.

`C:\Users\<username>\AppData\Local\Crumbs\appsettings.json`.

### JSON File Illustration

#### JSON Structure
```json
{
	"Startup" : 
	{
		"Url": "https://myapp.company.com",
		"Arguments": [{
			"Name": "username",
			"Value": "[USERNAME]"
		},
		{
			"Name": "keyCode",
			"Value": "999"
		}]
	},
	"UI": {
		"AddressBar": true,
		"NavigationButtons": true,
		"HomeButton": true
	},
	"Security": {
		"RequireHttps": false
	}
}
```

#### Environment Variables

Environment variables with the prefix `CRUMBS_`, for example `CRUMBS_STARTUP__URL` would override the `Startup:Url` setting in the JSON file.

Other examples include:

- `CRUMBS_STARTUP__ARGUMENTS__0__NAME` would override the name of the first argument in the Arguments array.
- `CRUMBS_STARTUP__ARGUMENTS__0__VALUE` would override the value of the first argument in the Arguments array.
- `CRUMBS_UI__ADDRESSBAR=false` would override the AddressBar to hidden.
- `CRUMBS_UI__NAVIGATIONBUTTONS=false` would override the NavigationButtons to hidden.
- `CRUMBS_UI__HOMEBUTTON=false` would override the HomeButton to hidden.
- `CRUMBS_SECURITY__REQUIREHTTPS=false` would set the Security:RequireHttps setting to false.

### Explanation of Keys

#### Startup
Contains settings for starting the application.

- **Url**: The endpoint opened at application launch.
- **Arguments**: A list of parameter objects passed during startup.
  - **Name**: The parameter name.
  - **Value**: The assigned value. This can be a placeholder (e.g., [USERNAME]) or a specific value (e.g., 999).

Place holders are replaced at runtime with actual values. Supported place holders are:

- [USERNAME]: The current user's username.
- [OS]: The operating system (typically Windows_NT).

#### UI
UI configuration settings.
- **AddressBar**: Determines whether the address bar is visible.
  - true → The address bar is visible.
  - false → The address bar is hidden.
- **NavigationButtons**: Determines whether the navigation buttons (back and forwards) are visible.
  - true → The navigation buttons are visible.
  - false → The navigation buttons are hidden.
- **HomeButton**: Determines whether the home button is visible.
  - true → The home button is visible.
  - false → The home button is hidden.

If a setting is not specified, the default value is true, meaning the UI element will be visible.

If address bar is hidden, the user can still navigate to a different URL by using the home button or navigation buttons (if they are visible) or by clicking on links within the web page. If the navigation buttons are shown they are folded into the title bar.
#### Security
Security-related configuration.
- **RequireHttps**: Determines whether HTTPS is required.
  - false → HTTPS is not required.

## Printing

First up is printing support. The WebView2 control does not have built in printing support, but it does have the WebResourceRequested event which can be used to intercept requests to a local REST API and implement printing that way. This approach has several advantages:

- **No custom JavaScript bridge needed.** The web page can call `fetch('http://127.0.0.1:41190/print/html/view')`. A javascript bridge would require a custom API surface and marshalling code, whereas `fetch()` is universally supported and works out of the box. This can quickly become a maintenance burden as the API evolves, so using standard HTTP verbs and JSON payloads keeps things simple.

- **No real network server required.** Requests are intercepted inside the WebView2 process boundary before they reach the network stack. No TCP port is bound, so there are no firewall rules to configure, no port-conflict risk, and no elevated permissions needed.

- **MSIX sandbox compatible.** A real localhost HTTP listener would require the `privateNetworkClientServer` capability and a loopback exemption. Because `WebResourceRequested` never opens a socket, the packaged app sandbox imposes no extra restrictions.

- **Isolated to this browser instance.** A genuine HTTP server is reachable by any process on the machine. The `WebResourceRequested` handler only fires for the specific `CoreWebView2` instance that registered the filter, so no other application can call the print API.

- **Standard `fetch()` on the client side.** The web page calls the API exactly as it would call any REST endpoint — no custom JavaScript bridge, no `window.chrome.webview.postMessage`, no Electron-specific IPC. This makes web apps straightforward to develop and test in a regular browser before deploying inside Crumbs.

- **Electron print API compatibility.** The endpoint signatures mirror Electron's `webContents.print()` and `webContents.printToPDF()` options, so web apps already written against Electron's printing model can be adapted with minimal changes.

- **Full `CoreWebView2PrintAsync` control.** `window.print()` always shows the system print dialog. Going through the REST API gives access to the full WebView2 silent-print path with per-job settings: printer selection, copies, duplex, colour mode, page ranges, margins, scale, and more.

- **Non-blocking by design.** Print jobs return `202 Accepted` immediately. The page can poll for status or fire-and-forget, keeping the UI responsive during long spooling operations.


## Where to next

- complete support for electron API: https://www.electronjs.org/docs/latest/api/web-contents
    - Implement support for electron printToPDF 

- review suport for this:

```javascript
    win.webContents.on('did-finish-load', () => {
        win.webContents.print(options, (success, failureReason) => {
            if (!success) console.log(failureReason);
            console.log('Print Initiated');
        });
    });
```


You already confirmed earlier that WebResourceRequested works for http://127.0.0.1:41190. This API is a natural REST API:

```
POST   /print/html/view            → 202 { "jobId": "j1" }   (print current HTML)
POST   /print/html/document        → 202 { "jobId": "j2" }   (body: { url, printerName, ... })
POST   /print/html/string          → 202 { "jobId": "j3" }   (body: { html, printerName, ... })
POST   /print/pdf                  → 202 { "jobId": "j4" }   (body: { url, printerName, ... })
GET    /print/jobs/{id}            → { status, progress, error }
GET    /print/jobs                 → [{ jobId, status }, ...]
GET    /print/spooling/complete    → blocks until queue empty (long-poll)

GET    /printers                   → [{ name, isDefault, mediaSize[], duplex, color, ... }]
GET    /printers/{name}            → full printer properties

POST   /zpl/print                  → 202 { "jobId": "j5" }
GET    /zpl/jobs/{id}              → { status, ... }`
```

```javascript
// No special wrappers needed - standard fetch() works directly
const res  = await fetch('http://127.0.0.1:41190/print/html/document', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ url: 'https://example.com/report', printerName: 'HP LaserJet', copies: 2 })
});
const { jobId } = await res.json();

// Poll for completion
let status;
do {
    await new Promise(r => setTimeout(r, 500));
    status = await (await fetch(`http://127.0.0.1:41190/print/jobs/${jobId}`)).json();
} while (status.status === 'printing');`
```

## Evidence

Yes, confirmed. WebResourceRequested works for http:// URLs including 127.0.0.1 and localhost, and fires for both GET and POST fetch() calls. Since your project uses WinRT C++/WinRT style (not COM), here is the correct pattern matching your existing codebase:

```cpp
// Register two filters — one for each host/port
m_webView.CoreWebView2().AddWebResourceRequestedFilter(
    L"http://127.0.0.1:41190/*",
    CoreWebView2WebResourceContext::All);

m_webView.CoreWebView2().AddWebResourceRequestedFilter(
    L"http://localhost:41191/*",
    CoreWebView2WebResourceContext::All);

m_webView.CoreWebView2().WebResourceRequested(
    [this](CoreWebView2 const&, CoreWebView2WebResourceRequestedEventArgs const& args)
    {
        auto request = args.Request();
        auto uri     = request.Uri();       // e.g. L"http://127.0.0.1:41190/api/data"
        auto method  = request.Method();    // L"GET" or L"POST"

        // Build JSON response body as an IRandomAccessStream
        std::string json = R"({"status":"ok","source":"CrumbsBrowser"})";
        auto stream = Windows::Storage::Streams::InMemoryRandomAccessStream();
        Windows::Storage::Streams::DataWriter writer(stream);
        writer.WriteBytes(winrt::array_view<const uint8_t>(
            reinterpret_cast<const uint8_t*>(json.data()),
            reinterpret_cast<const uint8_t*>(json.data() + json.size())));
        writer.StoreAsync().get();
        writer.DetachStream();
        stream.Seek(0);

        auto response = m_webView.CoreWebView2().Environment().CreateWebResourceResponse(
            stream,
            200,
            L"OK",
            L"Content-Type: application/json\r\nAccess-Control-Allow-Origin: *");

        args.Response(response);
    });
```