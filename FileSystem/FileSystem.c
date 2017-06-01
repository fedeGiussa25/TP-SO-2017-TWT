#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commons/config.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "socketEze.h"

fs_config data_config;

uint32_t sizeFile(char *nombreArchivo){
	FILE *archivo;
	archivo = fopen(nombreArchivo,"r");
	if(!archivo)
		return -1;
	fseek(archivo,0,SEEK_END);
	uint32_t dimension = ftell(archivo);
	fclose(archivo);
	return dimension;
}


uint32_t exist(char *nombreArchivo){
	FILE *archivo;
	archivo = fopen(nombreArchivo,"r");
	uint32_t var = true;
	if(!archivo)
		var = false;
	fclose(archivo);
	return var;
}

int main(int argc, char** argv)
{
	t_config *config;

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../FileSystem/") + strlen(argv[1])+1);
	*cfgPath = '\0';
	strcpy(cfgPath, "../../FileSystem/");

	config = config_create_from_relative_with_check(argv, cfgPath);

	//Leemos los datos
	data_config.puerto = config_get_string_value(config, "PUERTO");
	data_config.montaje = config_get_string_value(config, "PUNTO_MONTAJE");

	printf("PORT = %s\n", data_config.puerto);
	printf("Montaje = %s\n", data_config.montaje);


	config_destroy(config);		//Eliminamos fs_config, liberamos la memoria que utiliza

	//todo el server declaradito aca
	uint32_t miSocket = server(atoi(data_config.puerto),1);
	//end

	aceptarCliente(miSocket,1);
	DIR *mount = opendir(data_config.montaje);
	if(!mount)
		exit(3);

	while(1){


	}

	free(cfgPath);

	return 0;
}
