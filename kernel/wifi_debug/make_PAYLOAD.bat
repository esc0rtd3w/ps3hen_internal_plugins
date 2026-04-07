@echo off
cls
set PS3SDK=/c/PSDK3v2
set WIN_PS3SDK=C:/PSDK3v2
set PS3DEV=%PS3SDK%/ps3dev2
set PATH=%WIN_PS3SDK%/mingw/msys/1.0/bin;%WIN_PS3SDK%/mingw/bin;%PS3DEV%/ppu/bin;
set CYGWIN=C:\PSDK3v2\MinGW\msys\1.0\bin

set SUPPORTED_FIRMWARES=480C 481C 482C 482D 483C 484C 484D 485C 486C 487C 488C 489C 490C 491C 492C 493C
set FIRMWARE=%~1

cd payload

if "%FIRMWARE%"=="" goto build_all
if /I "%FIRMWARE%"=="all" goto build_all

echo Building wifi_debug_%FIRMWARE%.bin...
make -f Makefile clean-objs
make -f Makefile single FIRMWARE=%FIRMWARE% OUTPUT=payload_%FIRMWARE%.bin
if errorlevel 1 goto fail
if exist ..\wifi_debug_%FIRMWARE%.bin del /f /q ..\wifi_debug_%FIRMWARE%.bin
move /Y payload_%FIRMWARE%.bin ..\wifi_debug_%FIRMWARE%.bin >nul
make -f Makefile clean-objs
goto done

:build_all
echo Building all supported firmwares...
make -f Makefile all
if errorlevel 1 goto fail
for %%F in (%SUPPORTED_FIRMWARES%) do (
    if exist payload_%%F.bin (
        if exist ..\wifi_debug_%%F.bin del /f /q ..\wifi_debug_%%F.bin
        move /Y payload_%%F.bin ..\wifi_debug_%%F.bin >nul
    )
)
make -f Makefile clean-objs
goto done

:fail
echo.
echo Build failed.
goto end

:done
echo.
echo Done.
echo.

:end
cd ..
pause
