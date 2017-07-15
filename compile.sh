cd Kernel
gcc Kernel.c -o Debug/Kernel -lcommons -lpthread -lparser-ansisop 
cd ../Memoria 
gcc Memoria.c -o Debug/Memoria -lcommons -lpthread -lparser-ansisop 
cd ../CPU
gcc CPU.c -o Debug/CPU -lcommons -lpthread -lparser-ansisop 
cd ../Consola
gcc Consola.c -o Debug/Consola -lcommons -lpthread -lparser-ansisop 
cd ../FileSystem
gcc FileSystem.c -o Debug/FileSystem -lcommons -lpthread -lparser-ansisop 
cd ..
