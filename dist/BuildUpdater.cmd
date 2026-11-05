@echo off
setlocal
cd /d "%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
  echo ERROR: .NET Framework C# compiler not found.
  echo Use Visual Studio Developer Command Prompt and compile Updater.cs with csc.
  pause
  exit /b 1
)
"%CSC%" /nologo /target:winexe /platform:anycpu /optimize+ /win32icon:"%~dp0Customgram.ico" /out:"%~dp0Updater.exe" /reference:System.dll /reference:System.Windows.Forms.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Web.Extensions.dll "%~dp0Updater.cs"
if errorlevel 1 (
  echo Updater.exe compilation FAILED.
  pause
  exit /b 1
)
echo Updater.exe compiled successfully.
pause
