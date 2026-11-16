@echo off
setlocal
if not defined VSCMD_VER (
  echo Run from an x64 Visual Studio Developer Command Prompt.
  exit /b 2
)
cd /d "%~dp0../.."
if not exist Tests\Ethernet\build mkdir Tests\Ethernet\build
cl /nologo /W4 /WX /std:c11 /IPlatform\Ethernet Platform\Ethernet\eth_phy.c Tests\Ethernet\test_phy.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_phy.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_phy.exe
exit /b %errorlevel%
