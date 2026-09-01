@echo off
setlocal

where cl >NUL 2>&1
if errorlevel 1 (
  echo ERROR: Microsoft C compiler ^(cl.exe^) not found.
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)

cl /nologo /W4 /O2 /MT /TC p2_tool.c /Fe:p2_tool.exe
if errorlevel 1 exit /b 1

echo.
echo Built p2_tool.exe successfully.
endlocal
