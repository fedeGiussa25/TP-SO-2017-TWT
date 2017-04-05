/*
 ============================================================================
 Name        : consolaproto2.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <string.h>

typedef struct{
	char* ip_kernel;
	char* puerto_kernel;
}consola_config;


int main(int argc, char** argv) {
	if(argc == 0){
			printf("Debe ingresar ruta de .config y archivo\n");
			exit(1);
		}

		t_config *config;
		consola_config data_config;
		char *path, *ruta, *nombre_archivo;

		ruta = argv[1];
		nombre_archivo = argv[2];

		path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
		strcpy(path, "../../");
		strcat(path,ruta);
		strcat(path, nombre_archivo);

		char *key1 = "IP_KERNEL";
		char *key2 = "PUERTO_KERNEL";

		config = config_create(path);

		data_config.ip_kernel = config_get_string_value(config, key1);
		data_config.puerto_kernel = config_get_string_value(config, key2);

		printf("IP_KERNEL = %s\n", data_config.ip_kernel);
		printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

		config_destroy(config);
		free(path);
		return 0;
}
