@echo off
rem Modul abfragen
choice /c 12 /n /m "Welches Modul? [1=ESP_230V_IO_UP_1O; 2=ESP_230V_IO_UP_2O]"
if %ERRORLEVEL% == 1 set modul=ESP_230V_IO_UP_1O
if %ERRORLEVEL% == 2 set modul=ESP_230V_IO_UP_2O

rem Pfade setzen
set tmpDir=tmp
set binDir=bin
set dataDir=data
set dataModulDir=data_%modul%

set mkspiffs=mkspiffs.exe
set vmBuildBin=Debug\ESP8266_Firmware.bin

echo Gewähltes Modul: %modul%
echo.

rem prüfen ob bin-Ordner vorhanden ist...
if not exist %vmBuildBin% GOTO Error

rem prüfen ob bin-Ordner vorhanden ist...
if not exist %binDir% mkdir %binDir% 

echo SPIFFS binary wird erstellt...

rem Daten in tmp Verzeichnis zusammenkopieren
mkdir %tmpDir%
@for %%i in (%dataDir%\*.*) do @( 
  copy %%i %tmpDir%\%%~nxi /Y
)
@for %%i in (%dataModulDir%\*.*) do @( 
  copy %%i %tmpDir%\%%~nxi /Y
)

rem SPIFFS binary erstellen und tmp Verzeichnis löschen
%mkspiffs% -p 256 -b 4096 -s 0x30000 -c %tmpDir%\ %binDir%\%modul%_SPIFFS.bin
rmdir /s /q %tmpDir%

echo.
echo Firmware binary wird kopiert...
copy %vmBuildBin% %binDir%\%modul%.bin /Y

echo.
pause

goto Ende
:Error
echo Bitte erst 'Build' in Visual Micro fuer das entsprechende Modul ausfuerhren!
pause
:Ende