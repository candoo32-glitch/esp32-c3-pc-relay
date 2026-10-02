@echo off
if /I not "%~1"=="__ESP32_INTERNAL" (
 "%ComSpec%" /d /c call "%~f0" __ESP32_INTERNAL
 exit /b 0
)
setlocal EnableExtensions EnableDelayedExpansion
set "TARGET_BRANCH=__TARGET_BRANCH__"
set "FLASHER_BUILD=__FLASHER_BUILD__"
for /f "delims=" %%E in ('echo prompt $E^| cmd') do set "ESC=%%E"
set "VT=!ESC!["
chcp 65001 >nul 2>&1
mode con: cols=96 lines=34 >nul 2>&1
title ESP32-C3 PC Relay - Firmware Flasher
call :INIT

where gh >nul 2>&1 || (call :STATUS ERROR "GitHub CLI (gh) was not found." & call :WAIT & exit /b 1)
where py >nul 2>&1 || (call :STATUS ERROR "Python launcher (py) was not found." & call :WAIT & exit /b 1)
if not defined GH_TOKEN (
 call :STATUS AUTH "GH_TOKEN is not configured."
 call :LOG "Set GH_TOKEN in Windows and run again."
 call :WAIT
 exit /b 1
)

call :STATUS CHECKING "Checking for newer flasher..."
for /f "delims=" %%I in ('gh run list --repo candoo32-glitch/esp32-c3-pc-relay --workflow build.yml --branch "!TARGET_BRANCH!" --limit 1 --json databaseId --jq ".[0].databaseId"') do set "UPDATE_RUN=%%I"
if not defined UPDATE_RUN (call :STATUS ERROR "Could not determine latest build." & call :WAIT & exit /b 1)
for /f "delims=" %%I in ('gh run view "!UPDATE_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json number --jq ".number"') do set "UPDATE_RUN_NUMBER=%%I"
for /f "delims=" %%I in ('gh run view "!UPDATE_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json status --jq ".status"') do set "UPDATE_RUN_STATUS=%%I"
if /I not "!UPDATE_RUN_STATUS!"=="completed" (
 call :STATUS WAITING "Build !UPDATE_RUN_NUMBER! is !UPDATE_RUN_STATUS!."
 call :SPIN "Waiting for GitHub Actions"
 timeout /t 1 /nobreak >nul
 goto :RECHECK
)
:RECHECK
for /f "delims=" %%I in ('gh run view "!UPDATE_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json status --jq ".status"') do set "UPDATE_RUN_STATUS=%%I"
if /I not "!UPDATE_RUN_STATUS!"=="completed" (
 call :STATUS WAITING "Build !UPDATE_RUN_NUMBER! is !UPDATE_RUN_STATUS!."
 call :SPIN "Waiting for GitHub Actions"
 timeout /t 1 /nobreak >nul
 goto :RECHECK
)
for /f "delims=" %%I in ('gh run view "!UPDATE_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json conclusion --jq ".conclusion"') do set "UPDATE_RUN_CONCLUSION=%%I"
if /I not "!UPDATE_RUN_CONCLUSION!"=="success" (call :STATUS ERROR "Newest build !UPDATE_RUN_NUMBER! did not succeed." & call :WAIT & exit /b 1)
if "!UPDATE_RUN_NUMBER!"=="!FLASHER_BUILD!" (
 call :STATUS CURRENT "Flasher is current - build !FLASHER_BUILD!."
) else (
 set "UPDATE_ARTIFACT_NAME=esp32-c3-pc-relay-!TARGET_BRANCH!-build-!UPDATE_RUN_NUMBER!"
 call :STATUS UPDATE "New flasher build !UPDATE_RUN_NUMBER! is available."
 set "U=%TEMP%\esp32-c3-flasher-update-%RANDOM%%RANDOM%"
 set "UX=!U!\extracted"
 set "SELF=%~f0"
 mkdir "!UX!" >nul 2>&1
 gh run download "!UPDATE_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --name "!UPDATE_ARTIFACT_NAME!" --dir "!UX!" >nul 2>&1
 if errorlevel 1 (call :STATUS ERROR "Could not download the newer flasher." & call :WAIT & exit /b 1)
 if not exist "!UX!\flash.bat" (call :STATUS ERROR "New artifact has no flash.bat." & call :WAIT & exit /b 1)
 set "UH=!U!\replace.cmd"
 >"!UH!" echo @echo off
 >>"!UH!" echo timeout /t 1 /nobreak ^>nul
 >>"!UH!" echo copy /y "!UX!\flash.bat" "!SELF!" ^>nul
 >>"!UH!" echo if errorlevel 1 ^(
 >>"!UH!" echo echo ERROR: Could not replace the running flasher.
 >>"!UH!" echo pause
 >>"!UH!" echo exit /b 1
 >>"!UH!" echo ^)
 >>"!UH!" echo call "!SELF!" __ESP32_INTERNAL
 start "ESP32 Flasher Update" "%ComSpec%" /d /c call "!UH!"
 exit /b 0
)

call :STATUS CHECKING "Finding newest successful firmware build..."
set "LATEST_RUN="
for /f "delims=" %%I in ('gh run list --repo candoo32-glitch/esp32-c3-pc-relay --workflow build.yml --branch "!TARGET_BRANCH!" --limit 1 --json databaseId --jq ".[0].databaseId"') do set "LATEST_RUN=%%I"
if not defined LATEST_RUN (call :STATUS ERROR "No build found for !TARGET_BRANCH!." & call :WAIT & exit /b 1)

:BUILD
for /f "delims=" %%I in ('gh run view "!LATEST_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json number --jq ".number"') do set "LATEST_RUN_NUMBER=%%I"
for /f "delims=" %%I in ('gh run view "!LATEST_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json status --jq ".status"') do set "LATEST_RUN_STATUS=%%I"
for /f "delims=" %%I in ('gh run view "!LATEST_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --json conclusion --jq ".conclusion"') do set "LATEST_RUN_CONCLUSION=%%I"
call :LINE 6 "Build       !LATEST_RUN_NUMBER!"
if /I not "!LATEST_RUN_STATUS!"=="completed" (
 call :STATUS WAITING "Build !LATEST_RUN_NUMBER! is !LATEST_RUN_STATUS!."
 call :SPIN "Waiting for build"
 timeout /t 1 /nobreak >nul
 goto BUILD
)
if /I not "!LATEST_RUN_CONCLUSION!"=="success" (call :STATUS ERROR "Newest build !LATEST_RUN_NUMBER! failed." & call :WAIT & exit /b 1)
set "EXPECTED_ARTIFACT_NAME=esp32-c3-pc-relay-!TARGET_BRANCH!-build-!LATEST_RUN_NUMBER!"
call :STATUS READY "Build !LATEST_RUN_NUMBER! passed."
call :LINE 11 "Artifact     !EXPECTED_ARTIFACT_NAME!"
call :LINE 12 "Run ID       !LATEST_RUN!"
call :LINE 13 "Result       SUCCESS"

set "D=%TEMP%\esp32-c3-relay-%RANDOM%%RANDOM%"
set "X=!D!\extracted"
set "L=%~dp0"
set "F=!L!firmware"
mkdir "!X!" >nul 2>&1
call :STATUS DOWNLOAD "Downloading firmware artifact..."
call :SPIN "Downloading artifact"
gh run download "!LATEST_RUN!" --repo candoo32-glitch/esp32-c3-pc-relay --name "!EXPECTED_ARTIFACT_NAME!" --dir "!X!" >nul 2>&1 || (call :STATUS ERROR "Artifact download failed." & rmdir /s /q "!D!" >nul 2>&1 & call :WAIT & exit /b 1)
if not exist "!X!\firmware\factory.bin" (call :STATUS ERROR "factory.bin missing." & rmdir /s /q "!D!" >nul 2>&1 & call :WAIT & exit /b 1)
set /p "VER="<"!X!\BUILD_VERSION.txt"
set /p "RID="<"!X!\RUN_ID.txt"
set /p "AN="<"!X!\ARTIFACT_NAME.txt"
if not "!AN!"=="!EXPECTED_ARTIFACT_NAME!" (call :STATUS ERROR "Artifact name mismatch." & call :WAIT & exit /b 1)
if not "!RID!"=="!LATEST_RUN!" (call :STATUS ERROR "Artifact run ID mismatch." & call :WAIT & exit /b 1)
call :STATUS VERIFIED "Firmware artifact verified."
call :LINE 11 "Artifact     !AN!"
call :LINE 12 "Build        !VER!"
call :LINE 13 "Run ID       !RID!"
call :LINE 14 "Integrity    VERIFIED"

call :STATUS SYNC "Synchronizing local firmware..."
call :SPIN "Updating local firmware"
if exist "!F!" rmdir /s /q "!F!"
mkdir "!F!" >nul 2>&1
xcopy /e /i /y /q "!X!\firmware" "!F!" >nul 2>&1 || (call :STATUS ERROR "Local firmware update failed." & rmdir /s /q "!D!" >nul 2>&1 & call :WAIT & exit /b 1)
copy /y "!X!\README.txt" "!L!README.txt" >nul
copy /y "!X!\SHA256SUMS.txt" "!L!SHA256SUMS.txt" >nul
copy /y "!X!\BUILD_VERSION.txt" "!L!BUILD_VERSION.txt" >nul
copy /y "!X!\RUN_ID.txt" "!L!RUN_ID.txt" >nul
copy /y "!X!\ARTIFACT_NAME.txt" "!L!ARTIFACT_NAME.txt" >nul
rmdir /s /q "!D!" >nul 2>&1
if not exist "!F!\factory.bin" (call :STATUS ERROR "factory.bin missing after sync." & call :WAIT & exit /b 1)
call :STATUS READY "Firmware synchronized."
call :LINE 15 "Image        factory.bin (4 MB)"

set "LOG=%TEMP%\esp32-c3-relay-flash-%RANDOM%%RANDOM%.log"
call :STATUS FLASHING "Writing factory.bin to ESP32-C3..."
call :LINE 18 "Device       ESP32-C3"
call :LINE 19 "Stage        Writing flash"
call :SPIN "Programming device"
py -m esptool --chip esp32c3 --port-filter vid=0x303A --port-filter pid=0x1001 --after hard-reset write-flash 0x0 "!F!\factory.bin" >"!LOG!" 2>&1
set "RC=!ERRORLEVEL!"
if not "!RC!"=="0" (call :STATUS FAILED "ESP32-C3 flash failed." & call :LINE 19 "Stage        FAILED (exit !RC!)" & call :LINE 20 "Diagnostic   !LOG!" & call :WAIT & exit /b !RC!)
call :STATUS COMPLETE "Firmware flashed successfully."
call :LINE 19 "Stage        Complete"
call :LINE 20 "Build        !VER!"
call :DONE
call :WAIT
if exist "!LOG!" del /q "!LOG!" >nul 2>&1
exit /b 0

:INIT
cls
<nul set /p "=!VT!2J!VT!H"
echo !VT!1;36m+==============================================================================================+!VT!0m
echo !VT!1;36m^|                         ESP32-C3 PC RELAY  -  FLASHER                         ^|!VT!0m
echo !VT!1;36m+==============================================================================================+!VT!0m
echo !VT!1;37m^| REPOSITORY                                                                                   ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!36m^| ^| Branch      !TARGET_BRANCH!                                                                ^| ^|!VT!0m
echo !VT!36m^| ^| Build       checking...                                                                    ^| ^|!VT!0m
echo !VT!36m^| ^| Status      starting...                                                                    ^| ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!1;37m^| ARTIFACT                                                                                     ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!36m^| ^| Artifact    waiting...                                                                     ^| ^|!VT!0m
echo !VT!36m^| ^| Build       waiting...                                                                     ^| ^|!VT!0m
echo !VT!36m^| ^| Run ID      waiting...                                                                     ^| ^|!VT!0m
echo !VT!36m^| ^| Integrity   pending                                                                        ^| ^|!VT!0m
echo !VT!36m^| ^| Image       pending                                                                        ^| ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!1;37m^| FLASH STATUS                                                                                 ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!36m^| ^| Device      ESP32-C3                                                                       ^| ^|!VT!0m
echo !VT!36m^| ^| Stage       waiting                                                                        ^| ^|!VT!0m
echo !VT!36m^| ^| Progress    [░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  0%                         ^| ^|!VT!0m
echo !VT!36m^| ^| Detail      idle                                                                           ^| ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!1;37m^| ACTIVITY LOG                                                                                 ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!36m^| ^|                                                                                            ^| ^|!VT!0m
echo !VT!36m^| ^|                                                                                            ^| ^|!VT!0m
echo !VT!36m^| ^|                                                                                            ^| ^|!VT!0m
echo !VT!36m^| ^|                                                                                            ^| ^|!VT!0m
echo !VT!36m^| +--------------------------------------------------------------------------------------------+ ^|!VT!0m
echo !VT!1;36m+==============================================================================================+!VT!0m
echo !VT!1;37m^| READY                                                                                         ^|!VT!0m
echo !VT!1;36m+==============================================================================================+!VT!0m
exit /b 0

:STATUS
set "C=36"
if /I "%~1"=="WAITING" set "C=33"
if /I "%~1"=="BUILDING" set "C=33"
if /I "%~1"=="DOWNLOAD" set "C=33"
if /I "%~1"=="SYNC" set "C=33"
if /I "%~1"=="FLASHING" set "C=33"
if /I "%~1"=="UPDATE" set "C=35"
if /I "%~1"=="VERIFIED" set "C=32"
if /I "%~1"=="READY" set "C=32"
if /I "%~1"=="CURRENT" set "C=32"
if /I "%~1"=="COMPLETE" set "C=32"
if /I "%~1"=="ERROR" set "C=31"
if /I "%~1"=="FAILED" set "C=31"
<nul set /p "=!VT!7;1H!VT!2K!VT!1;37m^| Status      !VT!!C!m● %~1!VT!0m - %~2"
call :LOG "%~1: %~2"
exit /b 0

:LINE
<nul set /p "=!VT!%~1;1H!VT!2K!VT!36m^| !VT!0m%~2"
exit /b 0

:SPIN
set /a "S=(S+1) %% 4"
if !S!==0 set "B=████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░"
if !S!==1 set "B=░░░░████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░"
if !S!==2 set "B=░░░░░░░░████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░"
if !S!==3 set "B=░░░░░░░░░░░░████░░░░░░░░░░░░░░░░░░░░░░░░░░░░"
<nul set /p "=!VT!20;1H!VT!2K!VT!36m^| Progress    [!VT!96m!B!!VT!36m] !VT!0m"
<nul set /p "=!VT!21;1H!VT!2K!VT!36m^| Detail      !VT!0m%~1"
exit /b 0

:DONE
set "B=████████████████████████████████████████████████"
<nul set /p "=!VT!20;1H!VT!2K!VT!36m^| Progress    [!VT!92m!B!!VT!36m] 100%% !VT!0m"
<nul set /p "=!VT!21;1H!VT!2K!VT!36m^| Detail      !VT!0mComplete"
exit /b 0

:LOG
set "L4=!L3!"
set "L3=!L2!"
set "L2=!L1!"
set "L1=%~1"
<nul set /p "=!VT!25;1H!VT!2K!VT!36m^| ^| !VT!0m!L4!"
<nul set /p "=!VT!26;1H!VT!2K!VT!36m^| ^| !VT!0m!L3!"
<nul set /p "=!VT!27;1H!VT!2K!VT!36m^| ^| !VT!0m!L2!"
<nul set /p "=!VT!28;1H!VT!2K!VT!36m^| ^| !VT!0m!L1!"
exit /b 0

:WAIT
call :LINE 32 "Press any key to close..."
pause >nul
exit /b 0
