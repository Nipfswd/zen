mkdir dockerfiles/bin
robocopy /mir %~dp0x64\release dockerfiles\bin

docker build -t ucache dockerfiles
