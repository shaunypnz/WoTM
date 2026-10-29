@echo off
rem War of the Monsters, @MODE@ version. Copied into the game folder by play-wotm.sh.
rem Under Wine it hands over to the Linux launcher beside it (start /unix); on Windows it runs that
rem launcher inside WSL2 (WSLg shows the window). The native runner itself is a Linux program.
setlocal
set "HERE=%~dp0"
set "MODE=@MODE@"
if "%MODE:~0,1%"=="@" set "MODE=ps2"
reg query "HKCU\Software\Wine" >nul 2>nul
if %ERRORLEVEL%==0 goto wine
where wsl.exe >nul 2>nul || (
  echo WSL2 is required on Windows: run "wsl --install" once, reboot, then start this again.
  exit /b 1
)
for /f "usebackq delims=" %%p in (`wsl.exe wslpath -a "%HERE:~0,-1%"`) do set "UNIX_HERE=%%p"
wsl.exe -- bash "%UNIX_HERE%/play-wotm.sh" --mode %MODE% %*
exit /b %ERRORLEVEL%

:wine
rem Wine maps the Linux filesystem root to Z:, so this folder is the same folder under Z: minus its drive letter.
set "UNIX_HERE=%HERE:~2%"
set "UNIX_HERE=%UNIX_HERE:\=/%"
rem start /unix passes quotes through literally, so the path and the arguments travel in the environment
set "WOTM_LAUNCH=%UNIX_HERE%play-wotm.sh"
set "WOTM_BAT_ARGS=--mode %MODE% %*"
start /wait /unix /bin/bash -c "$WOTM_LAUNCH"
exit /b %ERRORLEVEL%
