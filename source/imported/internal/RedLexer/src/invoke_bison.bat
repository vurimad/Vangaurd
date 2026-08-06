setlocal
set PATH=..\..\..\..\external\bison\bin;%PATH%

bison --defines="%1" -o "%2" "%3"