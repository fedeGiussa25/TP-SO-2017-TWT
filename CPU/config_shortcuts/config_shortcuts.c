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
		if(argc == 0){
			printf("Debe ingresar ruta de .config y archivo\n");
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



