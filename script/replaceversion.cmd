@ECHO OFF
SET FILENAME=%1
FOR /F %%a IN ('..\script\version.cmd ..\src\Version.h . -extra') DO SET DOTTED_VERSION=%%a
FOR /F %%a IN ('..\script\version.cmd ..\src\Version.h -comma -extra') DO SET COMMA_VERSION=%%a
FOR /F %%a IN ('..\script\rcversion.cmd %FILENAME%') DO SET RCVERSION=%%a
IF "%COMMA_VERSION%"=="%RCVERSION%" GOTO END
PowerShell -ExecutionPolicy RemoteSigned -File ..\script\replaceversion.ps1 %FILENAME% %COMMA_VERSION% %DOTTED_VERSION% > %FILENAME%.temp
DEL %FILENAME%
REN %FILENAME%.temp %FILENAME%
:END