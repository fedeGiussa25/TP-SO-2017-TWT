/*
 ============================================================================
 Name        : kernelproto.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <commons/string.h>

typedef struct {
	int puerto_prog;
	int puerto_cpu;
	char* ip_memoria;
	int puerto_memoria;
	char* ip_fs;
	int puerto_fs;
	int quantum;
	int quantum_sleep;
	char* algoritmo;
	int grado_multiprog;
	char** sem_ids;
	int* sem_init;
	char** shared_vars;
	int stack_size;
}kernel_config;

int main(int argc, char** argv) {
	if(argc == 0){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}

	t_config *config_file;
	kernel_config data_config;
	char *path, *ruta, *nombre_archivo;
	int i=0, k=0, y=0;

	ruta = argv[1];
	nombre_archivo = argv[2];
	path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
	strcpy(path, "../../");
	strcat(path,ruta);
	strcat(path, nombre_archivo);

	config_file = config_create(path);

	//Configuro todo
	data_config.puerto_prog = config_get_int_value(config_file, "PUERTO_PROG");
	data_config.puerto_cpu = config_get_int_value(config_file, "PUERTO_CPU");
	data_config.ip_memoria = config_get_string_value(config_file,"IP_MEMORIA");
	data_config.puerto_memoria = config_get_int_value(config_file, "PUERTO_MEMORIA");
	data_config.ip_fs = config_get_string_value(config_file,"IP_FS");
	data_config.puerto_fs = config_get_int_value(config_file, "PUERTO_FS");
	data_config.quantum = config_get_int_value(config_file, "QUANTUM");
	data_config.quantum_sleep = config_get_int_value(config_file, "QUANTUM_SLEEP");
	data_config.algoritmo = config_get_string_value(config_file,"ALGORITMO");
	data_config.grado_multiprog = config_get_int_value(config_file, "GRADO_MULTIPROG");
	data_config.sem_ids = config_get_array_value(config_file, "SEM_IDS");
	data_config.sem_init = (int*) config_get_array_value(config_file, "SEM_INIT");
	//Sino hago el atoi me los toma como strings por alguna razon
	while(data_config.sem_init[y]!=NULL)
	{
		data_config.sem_init[y] = atoi(data_config.sem_init[y]);
		y++;
	}
	data_config.shared_vars = config_get_array_value(config_file, "SHARED_VARS");
	data_config.stack_size = config_get_int_value(config_file, "STACK_SIZE");

	//Imprimo todo
	printf("Puerto Programas: %i\n", data_config.puerto_prog);
	printf("Puerto CPUs: %i\n", data_config.puerto_cpu);
	printf("IP Memoria: %s\n", data_config.ip_memoria);
	printf("Puerto Memoria: %i\n", data_config.puerto_memoria);
	printf("IP FS: %s\n", data_config.ip_fs);
	printf("Puerto FS: %i\n", data_config.puerto_fs);
	printf("Quantum: %i\n", data_config.quantum);
	printf("Quantum Sleep: %i\n", data_config.quantum_sleep);
	printf("Algoritmo: %s\n", data_config.algoritmo);
	printf("Grado Multiprog: %i\n", data_config.grado_multiprog);
	while(data_config.sem_ids[i]!=NULL){
		printf("Semaforo %s = %i\n",data_config.sem_ids[i],data_config.sem_init[i]);
		i++;
	}
	while(data_config.shared_vars[k]!=NULL){
		printf("Variable Global %i: %s\n",k,data_config.shared_vars[k]);
		k++;
	}
	printf("Tamaño del Stack: %i\n", data_config.stack_size);

	config_destroy(config_file);
	free(path);
	return EXIT_SUCCESS;
}
