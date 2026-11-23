@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d <win-home>\dev\apichk\stallprobe
cl /nologo /O2 /EHsc /std:c++17 stallprobe.cpp /link /out:stallprobe.exe
