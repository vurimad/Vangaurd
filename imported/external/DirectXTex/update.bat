@echo off

SET /p VERSION=<version.txt

IF "%RED_EXTERNALS_DEV%" EQU "" GOTO SET_EXTERNALS_FROM_ARG
SET EXTERNALS=%RED_EXTERNALS_DEV%
GOTO SET_UPDATE_SCRIPTDIR

:SET_EXTERNALS_FROM_ARG
SET EXTERNALS=%~1

:SET_UPDATE_SCRIPTDIR
SET UPDATE_SCRIPTDIR=%~dp0

xcopy "%EXTERNALS%\%VERSION%\DirectXTex\Bin\Desktop_2015\x64\Debug\DirectXTex_MT.*" "%UPDATE_SCRIPTDIR%lib\x64.Debug\" /I /D /Y
xcopy "%EXTERNALS%\%VERSION%\DirectXTex\Bin\Desktop_2015\x64\Debug\DirectXTex_MD.*" "%UPDATE_SCRIPTDIR%lib\x64_DLL.Debug\" /I /D /Y

xcopy "%EXTERNALS%\%VERSION%\DirectXTex\Bin\Desktop_2015\x64\Release\DirectXTex_MT.*" "%UPDATE_SCRIPTDIR%lib\x64.Release\" /I /D /Y
xcopy "%EXTERNALS%\%VERSION%\DirectXTex\Bin\Desktop_2015\x64\Release\DirectXTex_MD.*" "%UPDATE_SCRIPTDIR%lib\x64_DLL.Release\" /I /D /Y

xcopy "%EXTERNALS%\%VERSION%\DirectXTex\*.h" "%UPDATE_SCRIPTDIR%include\" /I /D /Y
xcopy "%EXTERNALS%\%VERSION%\DirectXTex\*.inl" "%UPDATE_SCRIPTDIR%include\" /I /D /Y

xcopy "%EXTERNALS%\%VERSION%\MIT.txt" "%UPDATE_SCRIPTDIR%license\" /I /D /Y

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