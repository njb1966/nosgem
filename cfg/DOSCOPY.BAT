@echo off
REM NOSgem transfer helper
REM Usage: DOSCOPY.BAT [linux-ip]

if "%1"=="" goto defaultip
set LINUXIP=%1
goto haveip

:defaultip
set LINUXIP=192.168.0.97
:haveip

REM Ensure target directory exists
if not exist C:\NOSGEM\NUL mkdir C:\NOSGEM

REM Fetch files
C:\NET\HTGET.EXE -o C:\NOSGEM\NOSGEM.EXE http://%LINUXIP%:8080/NOSGEM.EXE
C:\NET\HTGET.EXE -o C:\NOSGEM\HOMEPAGE.GEM http://%LINUXIP%:8080/cfg/HOMEPAGE.GEM
C:\NET\HTGET.EXE -o C:\NOSGEM\BOOKMARKS.GEM http://%LINUXIP%:8080/cfg/BOOKMARKS.GEM
C:\NET\HTGET.EXE -o C:\NOSGEM\NOSGEM.CFG http://%LINUXIP%:8080/cfg/NOSGEM.CFG

echo.
echo Done. Run NOSGEM.EXE from C:\NOSGEM

goto end

:end
