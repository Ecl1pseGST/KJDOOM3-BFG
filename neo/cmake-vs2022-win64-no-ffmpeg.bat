cd ..
del /s /q build
mkdir build
cd build
cmake -G "Visual Studio 17" -A x64 -DFFMPEG=OFF -DBINKDEC=ON -DDXC_PATH="C:/dxc/bin/x64/dxc.exe" -DDXC_SPIRV_PATH="C:/dxc/bin/x64/dxc.exe" ../neo 
pause