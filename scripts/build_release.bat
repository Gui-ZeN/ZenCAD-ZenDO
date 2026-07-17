@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "VSCMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VSNINJA=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "PATH=%VSCMAKE%;%VSNINJA%;%PATH%"
set "SRC=C:\Users\M7QO\Desktop\Meus Projetos\CadCore"
set "BLD=%SRC%\build-app-rel"
set "QTDIR=C:\Qt\6.8.3\msvc2022_64"

echo ===== CONFIGURE (app ON, RELEASE) =====
cmake -G Ninja -S "%SRC%" -B "%BLD%" -DCADCORE_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%" || exit /b 1
echo ===== BUILD cadapp + zendo (Release) =====
cmake --build "%BLD%" --target cadapp zendo || exit /b 1
echo ===== DEPLOY Qt DLLs (release) =====
"%QTDIR%\bin\windeployqt.exe" --release "%BLD%\src\app\cadapp.exe" >nul 2>&1 || exit /b 1
"%QTDIR%\bin\windeployqt.exe" --release "%BLD%\src\zendo\zendo.exe" >nul 2>&1 || exit /b 1
echo ===== INSTALADORES (os DOIS, sempre juntos) =====
rem R60: os dois ISCC moram AQUI de propósito. Com um instalador por produto,
rem recompilar só um depois de um fix deixa o irmão com o nome novo e o binário
rem velho — e ninguém percebe, porque o arquivo TEM a versão certa no nome. É a
rem mesma classe de erro da revisão pós-R30 (8 releases num dia indistinguíveis),
rem agora dobrada. Um comando gera a leva inteira, ou falha.
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
"%ISCC%" "%~dp0..\installer\zencad.iss" >nul || exit /b 1
"%ISCC%" "%~dp0..\installer\zendo.iss"  >nul || exit /b 1

echo ===== DONE =====
dir "%BLD%\src\app\cadapp.exe" | findstr cadapp
dir "%BLD%\src\zendo\zendo.exe" | findstr zendo
dir "%~dp0..\dist\*Setup*.exe" | findstr Setup
