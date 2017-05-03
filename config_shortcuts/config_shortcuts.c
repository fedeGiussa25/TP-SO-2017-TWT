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

t_config* config_create_from_relative_with_check(char **argv, char *cfgPath)
{
	char *nombre_archivo;
	nombre_archivo = argv[1];
	strcat(cfgPath, nombre_archivo);
	return config_create(cfgPath);
}

void checkArguments(argc)
{
	if(argc == 1)
	{
	printf("Debe ingresar el nombre del archivo de configuracion\n");
	exit(1);
	}

	if(argc != 2)
	{
	printf("Numero incorrecto de argumentos\n");
	exit(1);
	}	
}

