# Project Overview

This project is a packaged WinUI app. The app is a single frame window hosting the WebView2 component and a typical browser toolbar.

The browser is intended to allow developers to easily customize the app experience for their users using a JSON configuration file and environment variables. 

## Folder structure 

- `/CrumbsBrowser` - The main project folder containing the source code for the browser.
- `/CrumbsBrowser/Assets` - Contains digital assets for the browser, such as icons and images.
- `/CrumbsBrowser/Strings/EN-GB` - Contains text assets for the (default) English (United Kingdom) locale.
- `/CrumbsBrowser (Package)` - Contains the packaging project for the browser, which creates an MSIX package for distribution.

## Libraries used

- JSON for modern C++ (https://github.com/nlohmann/json) - `/CrumbsBrowser/nlohmann` - a C++ library for working with JSON data.

## Coding Standards

- Code should be written in modern C++ (C++17 or later).
- Code should be well-documented and follow best practices for readability and maintainability.
- Code should be organized into logical modules and classes, with clear separation of concerns.
- Error handling should be implemented using exceptions, and all exceptions should be properly caught and handled to prevent crashes.
- Unit tests should be written for all major components of the application, and should be run regularly to ensure code quality and prevent regressions.

## UI guidelines

- Application should have a modern and clean design.
