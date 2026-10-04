@echo off
REM drivertesti.hpp mutlak yolu: C:\Users\asdas\Desktop\USERMODE\UM\...
REM Bu script (YONETICI olarak) C:\Users\asdas -> Expectional\_asdas_mirror baglar.
set "D=%~dp0"
set "EXROOT=%D%.."
for %%I in ("%EXROOT%") do set "EXROOT=%%~fI"
set "TARGET=%EXROOT%\_asdas_mirror"
if exist "C:\Users\asdas" (
  echo C:\Users\asdas zaten var. Silmeden devam edemem.
  exit /b 1
)
mklink /J "C:\Users\asdas" "%TARGET%"
if errorlevel 1 (
  echo mklink basarisiz. CMD yi Yonetici Olarak Calistirin.
  exit /b 1
)
echo Tamam: C:\Users\asdas artik su klasore yonlendirildi:
echo   %TARGET%
exit /b 0
