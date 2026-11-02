@echo off
setlocal
rem Builds src\tripal_cuda.cu with nvcc. nvcc needs the MSVC host compiler on PATH;
rem if cl.exe is not already there, this loads it from any Visual Studio 2022 edition.
rem Set CUDA_ARCH to override the GPU architecture (default: native, the GPU in this machine).
rem Pass check to also build and run the focused GPU regressions.

where cl >nul 2>nul
if not errorlevel 1 goto :build

for /d %%d in ("%ProgramFiles%\Microsoft Visual Studio\2022\*") do (
    if exist "%%d\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%d\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    echo MSVC not found: install Visual Studio 2022 with the C++ workload, or run from a developer prompt.
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

:build
if not defined CUDA_ARCH set "CUDA_ARCH=native"
if not exist bin mkdir bin
nvcc -O3 -arch=%CUDA_ARCH% -o bin\tripal_cuda.exe src\tripal_cuda.cu
if errorlevel 1 exit /b 1
echo built bin\tripal_cuda.exe
if /i not "%~1"=="check" exit /b 0
nvcc -O3 -arch=%CUDA_ARCH% -o bin\tripal_cuda_test.exe tests\tripal_cuda_test.cu
if errorlevel 1 exit /b 1
bin\tripal_cuda_test.exe
if errorlevel 1 exit /b 1
