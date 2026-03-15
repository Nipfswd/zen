@echo off

rem This script can be used to add the build output folder (Release) to the path for easier
rem ad hoc testing from the command line

CALL :NORMALIZEPATH "%~dp0..\x64\Release"

SET _BINPATH=%RETVAL%

path|find /i "%_BINPATH%" >nul || set PATH=%PATH%;%_BINPATH%

SET _BINPATH=

EXIT /B

:NORMALIZEPATH
  SET RETVAL=%~f1
  EXIT /B