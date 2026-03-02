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
	"NoUI": true,
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
- `CRUMBS_NOUI=false` would override the NoUI setting.
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

#### NoUI
A Boolean flag determining whether the UI such as address bar, back and forward buttons are hidden.
- true → The application runs without visible UI elements.

#### Security
Security-related configuration.
- **RequireHttps**: Determines whether HTTPS is required.
  - false → HTTPS is not required.



