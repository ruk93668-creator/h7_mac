@echo off
setlocal
cd /d "%~dp0"
"C:\Users\29062\AppData\Local\Programs\Python\Python311\python.exe" -B "h1_cm7_only.py" --program
set "H1_RESULT=%ERRORLEVEL%"
echo H1 exit code: %H1_RESULT%
pause
exit /b %H1_RESULT%
