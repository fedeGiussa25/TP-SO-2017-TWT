/*
 * config_shortcuts.c
 *
 *  Created on: 7/4/2017
 *      Author: utnso
 */
#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include "config_shortcuts.h"

	t_config* config_create_from_relative_with_check(int argc, char** argv){
		
		if(argc == 1){
			printf("Debe ingresar la ruta del archivo de configuracion y su nombre\n");
			exit(1);
		}
	
		if(argc == 2){
			printf("Debe ingresar el nombre del archivo de configuracion\n");
			exit(1);
		}
		
		if(argc != 3){
			printf("Numero incorrecto de argumentos\n");
			exit(1);
		}
		
		char *path, *ruta, *nombre_archivo;
		ruta = argv[1];
		nombre_archivo = argv[2];
		path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
		strcpy(path, "../../");
		strcat(path,ruta);
		strcat(path, nombre_archivo);
		return config_create(path);
	}



