# Exemplar script to create system environment variables for Crumbs Browser. Please run this script as an administrator.

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Please run this script as an administrator." -ForegroundColor Red
    Exit
}

[System.Environment]::SetEnvironmentVariable("CRUMBS_Startup__Url", "https://github.com/Pete-G-Cole", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_Startup__Arguments__0__name", "os", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_Startup__Arguments__0__value", "[OS]", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_Startup__Arguments__1__name", "keyCode", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_Startup__Arguments__1__value", "84", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_UI__ADDRESSBAR", "false", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_UI__NAVIGATIONBUTTONS", "true", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_UI__HOMEBUTTON", "false", [System.EnvironmentVariableTarget]::Machine)
[System.Environment]::SetEnvironmentVariable("CRUMBS_Security__RequireHttps", "true", [System.EnvironmentVariableTarget]::Machine)
