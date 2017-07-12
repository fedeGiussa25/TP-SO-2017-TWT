./makeConfig Kernel/Kernel.config Consola/Consola.config CPU/CPU.config Memoria/Memoria.config FileSystem/FileSystem.config
cd Kernel
gcc -lcommons -lpthread -lparser-ansisop Kernel.c -o Debug/Kernel
cd ../Memoria 
gcc -lcommons -lpthread -lparser-ansisop Memoria.c -o Debug/Memoria
cd ../CPU
gcc -lcommons -lpthread -lparser-ansisop CPU.c -o Debug/CPU
cd ../Consola
gcc -lcommons -lpthread -lparser-ansisop Consola.c -o Debug/Consola
cd ../FileSystem
gcc -lcommons -lpthread -lparser-ansisop FileSystem.c -o FileSystem
cd ..
