@ECHO OFF
SET "comport="
SET "baudrate="
CALL SET "comport=%%1"
CALL SET "baudrate=%%2"
IF defined comport GOTO :baudrate

:nocomport
@ECHO Usage: flash ^[COM_PORT^] ^<BAUDRATE^>
exit /b %ERRORLEVEL%

:baudrate
IF defined baudrate GOTO :flash
CALL SET "baudrate=921600"

:flash
ECHO Flashing to ESP32...
".\esptool.exe" --chip esp32 --port %comport% --baud %baudrate%  --before default_reset --after hard_reset write_flash -e -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 "..\firmware\merged.bin"
exit

