@echo off
setlocal
set CL=/utf-8 %CL%
if not defined VSCMD_VER (
  echo Run from an x64 Visual Studio Developer Command Prompt.
  exit /b 2
)
cd /d "%~dp0../.."
if not exist Tests\Ethernet\build mkdir Tests\Ethernet\build
cl /nologo /W4 /WX /std:c11 /IPlatform\Ethernet Platform\Ethernet\eth_phy.c Tests\Ethernet\test_phy.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_phy.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_phy.exe
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c11 /IPlatform\Ethernet Platform\Ethernet\eth_dma.c Tests\Ethernet\test_dma.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_dma.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_dma.exe
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c11 /DETH_MAC_HOST_TEST /IPlatform\Ethernet /ITests\Ethernet Platform\Ethernet\eth_phy.c Platform\Ethernet\eth_dma.c Platform\Ethernet\eth_mac.c Platform\Ethernet\eth_board_stm32h747.c Tests\Ethernet\test_mac.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_mac.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_mac.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_mac.exe init_missing
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_mac.exe init_reset
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_mac.exe mdio_timeout
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c11 /IPlatform\Ethernet Platform\Ethernet\eth_dma.c Tests\Ethernet\test_engine.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_engine.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_engine.exe
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c11 Tests\Ethernet\eth_test_rx.c Tests\Ethernet\test_rx_check.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_rx_check.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_rx_check.exe
if errorlevel 1 exit /b 1
python "%~dp0test_mdio_clock.py"
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c11 Tests\Ethernet\e35_frame.c Tests\Ethernet\test_e35_frame.c /FoTests\Ethernet\build\ /FeTests\Ethernet\build\test_e35_frame.exe
if errorlevel 1 exit /b 1
Tests\Ethernet\build\test_e35_frame.exe
if errorlevel 1 exit /b 1
python "%~dp0test_e35_peer.py"
exit /b %errorlevel%
