:: Using the nuwen build of MinGW
call C:\mingw\set_distro_paths.bat
echo on
g++ -c -std=c++17 -O2 dbpf-recompress.cpp
g++ dbpf-recompress.o -fopenmp -lboost_nowide -o dbpf-recompress.exe
del dbpf-recompress.o
pause