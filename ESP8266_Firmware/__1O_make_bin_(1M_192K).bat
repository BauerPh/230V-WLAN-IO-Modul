@ECHO OFF
echo SPIFFS binary wird erstellt...

REM Daten in tmp Verzeichnis zusammenkopieren
mkdir tmp
@FOR %%i IN (data\*.*) do @( 
  copy %%i tmp\%%~nxi /Y
)
@FOR %%i IN (data_1O\*.*) do @( 
  copy %%i tmp\%%~nxi /Y
)

REM SPIFFS binary erstellen und tmp Verzeichnis löschen
mkspiffs.exe -p 256 -b 4096 -s 0x30000 -c tmp/ bin/ESP_230V_IO_UP_1O_SPIFFS.bin
rmdir /s /q tmp

echo.
echo Firmware binary wird kopiert...
copy Debug\ESP8266_Firmware.bin bin\ESP_230V_IO_UP_1O.bin /Y

echo.
Pause