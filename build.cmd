@echo off
setlocal

set "APP_VERSION_TEXT="
if not "%GITHUB_REF_NAME%"=="" (
  set "APP_VERSION_TEXT=%GITHUB_REF_NAME%"
) else (
  for /f "usebackq delims=" %%i in (`git describe --tags --always --dirty 2^>nul`) do set "APP_VERSION_TEXT=%%i"
)
if "%APP_VERSION_TEXT%"=="" set "APP_VERSION_TEXT=dev"

call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

cl /std:c++20 /utf-8 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
  /DAPP_VERSION_W=L\"%APP_VERSION_TEXT%\" ^
  src\main.cpp ^
  src\AppBarWindow.cpp ^
  src\AppFeatures.cpp ^
  src\CodexUsageFetcher.cpp ^
  src\AccountLibrary.cpp ^
  src\AppUpdate.cpp ^
  src\BrowserSignIn.cpp ^
  src\GrokBilling.cpp ^
  src\ProxyConfig.cpp ^
  src\QuotaEstimate.cpp ^
  src\ResetStatus.cpp ^
  src\SessionIndex.cpp ^
  src\JsonLite.cpp ^
  /Fe:CodexUsageBar.exe ^
  /link advapi32.lib bcrypt.lib crypt32.lib ole32.lib secur32.lib shell32.lib shlwapi.lib comdlg32.lib winhttp.lib user32.lib gdi32.lib d2d1.lib dwrite.lib ws2_32.lib
if errorlevel 1 exit /b 1

cl /std:c++20 /utf-8 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
  src\FeatureTests.cpp ^
  src\CodexUsageFetcher.cpp ^
  src\AccountLibrary.cpp ^
  src\AppUpdate.cpp ^
  src\BrowserSignIn.cpp ^
  src\GrokBilling.cpp ^
  src\ProxyConfig.cpp ^
  src\QuotaEstimate.cpp ^
  src\ResetStatus.cpp ^
  src\SessionIndex.cpp ^
  src\JsonLite.cpp ^
  /Fe:CodexFeatureTests.exe ^
  /link advapi32.lib bcrypt.lib crypt32.lib ole32.lib secur32.lib shell32.lib shlwapi.lib winhttp.lib user32.lib ws2_32.lib

exit /b %errorlevel%
