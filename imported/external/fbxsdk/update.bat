@echo off

SET /p VERSION=<version.txt

IF "%RED_EXTERNALS_DEV%" EQU "" GOTO SET_EXTERNALS_FROM_ARG
SET EXTERNALS=%RED_EXTERNALS_DEV%
GOTO SET_UPDATE_SCRIPTDIR

:SET_EXTERNALS_FROM_ARG
SET EXTERNALS=%~1

:SET_UPDATE_SCRIPTDIR
SET UPDATE_SCRIPTDIR=%~dp0

IF "%EXTERNALS%" EQU "" GOTO ERROR1
IF NOT EXIST %EXTERNALS% GOTO ERROR2

SET SOURCE_LIBS=%EXTERNALS%\%VERSION%\lib\vs2015\x64\
SET DESTINATION_LIBS=%UPDATE_SCRIPTDIR%lib\

xcopy "%SOURCE_LIBS%debug\libfbxsdk.dll" "%DESTINATION_LIBS%\x64.Debug\" /I /D /Y
xcopy "%SOURCE_LIBS%debug\libfbxsdk.lib" "%DESTINATION_LIBS%\x64.Debug\" /I /D /Y
xcopy "%SOURCE_LIBS%release\libfbxsdk.dll" "%DESTINATION_LIBS%\x64.Release\" /I /D /Y
xcopy "%SOURCE_LIBS%release\libfbxsdk.lib" "%DESTINATION_LIBS%\x64.Release\" /I /D /Y

SET SOURCE_INCLUDES="%EXTERNALS%\%VERSION%\include\*.*" 
SET DESTINATION_INCLUDES="%UPDATE_SCRIPTDIR%include\"

xcopy %SOURCE_INCLUDES% %DESTINATION_INCLUDES% /S /I /D /Y

xcopy "%EXTERNALS%\%VERSION%\License.rtf" "%UPDATE_SCRIPTDIR%license\" /S /I /D /Y

EXIT /B 0

:ERROR1
ECHO +++++++++++++++++++++++++++++++++++++++++++++++++++++
ECHO +                                                   +
ECHO + Usage: update.bat "c:\path\to\external\directory" +
ECHO +                                                   + 
ECHO +++++++++++++++++++++++++++++++++++++++++++++++++++++
ECHO +                                                   + 
ECHO +   Or set Environment Variable RED_EXTERNALS_DEV   +
ECHO +       to "c:\path\to\external\directory"          +
ECHO +                                                   + 
ECHO +++++++++++++++++++++++++++++++++++++++++++++++++++++
EXIT /B 2

:ERROR2
ECHO Can not find directory "%EXTERNALS%"
EXIT /B 2