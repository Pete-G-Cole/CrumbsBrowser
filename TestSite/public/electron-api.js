/**
 * electron-api.js
 *
 * Wraps the CrumbsBrowser local REST API to provide an interface that mirrors
 * Electron's webContents printing API (https://www.electronjs.org/docs/latest/api/web-contents).
 *
 * The API is only available when the page is running inside CrumbsBrowser.
 * Call `window.electronAPI.isAvailable()` first to guard against other browsers.
 *
 * Port resolution order:
 *   1. window.CrumbsBrowserApiPort  (set this before loading the script to override)
 *   2. 41190                        (default, matches appsettings.json Api.Port)
 *
 * Usage:
 *   if (await window.electronAPI.isAvailable()) {
 *       const printers = await window.electronAPI.getPrinters();
 *       await window.electronAPI.print({ silent: false });
 *   }
 */
(function (global) {
    'use strict';

    const port    = global.CrumbsBrowserApiPort ?? 41190;
    const apiBase = `http://localhost:${port}`;

    // -------------------------------------------------------------------------
    // Guard — checks whether the CrumbsBrowser REST API is reachable
    // -------------------------------------------------------------------------

    let _available = null; // null = unchecked, true/false = cached result

    /**
     * Returns true if the page is running inside CrumbsBrowser and the local
     * REST API is reachable.  Result is cached after the first call.
     * @returns {Promise<boolean>}
     */
    async function isAvailable() {
        if (_available !== null) return _available;
        try {
            const res  = await fetch(`${apiBase}/ping`, { signal: AbortSignal.timeout(2000) });
            const data = res.ok ? await res.json() : null;
            _available = data?.app === 'CrumbsBrowser';
        } catch {
            _available = false;
        }
        return _available;
    }

    /**
     * Throws if not running inside CrumbsBrowser.
     */
    async function requireAvailable() {
        if (!await isAvailable()) {
            throw new Error('electronAPI is only available inside CrumbsBrowser.');
        }
    }

    // -------------------------------------------------------------------------
    // API implementation
    // -------------------------------------------------------------------------

    /**
     * Returns a list of available system printers.
     * Equivalent to Electron's webContents.getPrinters().
     *
     * @returns {Promise<Array<{
     *   name: string,
     *   isDefault: boolean,
     *   status: string,
     *   portName: string,
     *   driverName: string,
     *   defaults: object
     * }>>}
     */
    async function getPrinters() {
        await requireAvailable();
        const res = await fetch(`${apiBase}/printers`);
        if (!res.ok) throw new Error(`getPrinters failed: HTTP ${res.status}`);
        return res.json();
    }

    /**
     * Returns detail for a single printer including its capabilities.
     * Equivalent to Electron's webContents.getPrinterByName() (Electron 22+).
     *
     * @param {string} name  Printer name as returned by getPrinters().
     * @returns {Promise<object>}
     */
    async function getPrinterByName(name) {
        await requireAvailable();
        const res = await fetch(`${apiBase}/printers/${encodeURIComponent(name)}`);
        if (res.status === 404) throw new Error(`Printer not found: ${name}`);
        if (!res.ok) throw new Error(`getPrinterByName failed: HTTP ${res.status}`);
        return res.json();
    }

    /**
     * Prints the current page.
     * Equivalent to Electron's webContents.print(options, callback).
     *
     * When silent is false (the default) the browser print dialog is shown.
     * When silent is true the page is sent directly to the specified printer.
     *
     * @param {object}   [options]
     * @param {boolean}  [options.silent=false]           Print without showing a dialog.
     * @param {boolean}  [options.printBackground=false]  Print CSS background graphics.
     * @param {string}   [options.deviceName='']          Target printer name.
     * @param {boolean}  [options.color=true]             Print in colour.
     * @param {object}   [options.margins]                Margin settings.
     * @param {string}   [options.margins.marginType]     'default'|'none'|'printableArea'|'custom'
     * @param {number}   [options.margins.top]            Top margin in microns (custom only).
     * @param {number}   [options.margins.bottom]         Bottom margin in microns (custom only).
     * @param {number}   [options.margins.left]           Left margin in microns (custom only).
     * @param {number}   [options.margins.right]          Right margin in microns (custom only).
     * @param {boolean}  [options.landscape=false]        Landscape orientation.
     * @param {number}   [options.scaleFactor=100]        Scale factor as a percentage (10–200).
     * @param {boolean}  [options.collate=false]          Collate copies.
     * @param {number}   [options.copies=1]               Number of copies.
     * @param {string}   [options.pageRanges='']          Page ranges e.g. "1-5, 8, 11-13".
     * @param {string}   [options.duplexMode='simplex']   'simplex'|'shortEdge'|'longEdge'.
     * @param {string}   [options.header='']              Page header text.
     * @param {string}   [options.footer='']              Page footer text.
     * @param {string|{width:number,height:number}} [options.pageSize='A4']
     *                                                    Named size or {width,height} in microns.
     * @param {function(success:boolean, failureReason:string)} [callback]
     *   Optional callback for Electron API compatibility.
     * @returns {Promise<{status: string}>}
     */
    async function print(options = {}, callback) {
        await requireAvailable();
        try {
            const res = await fetch(`${apiBase}/print`, {
                method:  'POST',
                headers: { 'Content-Type': 'application/json' },
                body:    JSON.stringify(options)
            });
            if (!res.ok) {
                const err = await res.json().catch(() => ({ message: res.statusText }));
                const msg = err?.message ?? res.statusText;
                if (callback) callback(false, msg);
                throw new Error(`print failed: ${msg}`);
            }
            const result = await res.json();
            if (callback) callback(true, '');
            return result;
        } catch (e) {
            if (callback) callback(false, e.message);
            throw e;
        }
    }

    // -------------------------------------------------------------------------
    // Expose as window.electronAPI  (mirrors Electron's contextBridge pattern)
    // -------------------------------------------------------------------------

    global.electronAPI = {
        isAvailable,
        getPrinters,
        getPrinterByName,
        print
    };

}(window));
