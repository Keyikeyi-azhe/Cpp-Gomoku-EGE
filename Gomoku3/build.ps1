$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & 'D:\Dev-Cpp\MinGW64\bin\g++.exe' server_main.cpp -o GomokuServer.exe -std=c++11 -m64 -O2 -Wall -Wextra -finput-charset=GBK -lws2_32 -static-libgcc -static-libstdc++
    if ($LASTEXITCODE -ne 0) { throw 'Server compilation failed.' }
    & 'D:\Dev-Cpp\MinGW64\bin\g++.exe' main.cpp -o Gomoku.exe -std=c++11 -m64 -O2 -Wall -Wextra -finput-charset=GBK -mwindows -lws2_32 -liphlpapi -lgraphics -luuid -lmsimg32 -lgdi32 -limm32 -lole32 -loleaut32 -lwinmm -lgdiplus -static-libgcc -static-libstdc++
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    Write-Output 'Built Gomoku.exe'
} finally {
    Pop-Location
}
