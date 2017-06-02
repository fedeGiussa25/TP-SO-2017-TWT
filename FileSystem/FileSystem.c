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
#define SIZE_MSG 11
#define EXIST_MSG 12

fs_config data_config;

uint32_t sizeFile(char *nombreArchivo,char *rutaBase){
	FILE *archivo;
	char *fullPath = malloc(sizeof(nombreArchivo) + sizeof(rutaBase));
	strcpy(fullPath,rutaBase);
	strcat(fullPath,nombreArchivo);
	archivo = fopen(fullPath,"r");
	if(!archivo)
		return -1;
	fseek(archivo,0,SEEK_END);
	uint32_t dimension = ftell(archivo);
	fclose(archivo);
	free(fullPath);
	return dimension;
}

uint32_t exist(char *nombreArchivo,char *rutaBase){
	FILE *archivo;
	char *fullPath = malloc(sizeof(nombreArchivo) + sizeof(rutaBase));
	strcpy(fullPath,rutaBase);
	strcat(fullPath,nombreArchivo);
	archivo = fopen(fullPath,"r");
	uint32_t var = true;
	if(!archivo)
		var = false;
	fclose(archivo);
	free(fullPath);
	return var;
}

char* obtieneNombreArchivo(uint32_t socketReceiver){
	uint32_t size_name;
	char *nombre;
	recibir(socketReceiver,(void *)&size_name,sizeof(uint32_t));
	nombre = malloc(size_name);
	recibir(socketReceiver,(void *)nombre,size_name);
	return nombre;
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

	//ASI FUNCIONA EL MUNDO FILESYSTEM
//					///								///
//		MENSAJE		///	dimension TEXT	//	TEXT	///
//	exist	size	///								///
	uint32_t kernel;
	if(kernel = aceptarCliente(miSocket,1) == -1)
		exit(5);

	DIR *mount = opendir(data_config.montaje);
	if(!mount)
		exit(3);
	char *nameArchRequest;
	uint32_t msg,size_name;
	while(1){
		int flag=1;
		recibir(kernel,(void *)&msg,sizeof(uint32_t));
		switch(msg){
			case EXIST_MSG:
				nameArchRequest = obtieneNombreArchivo(kernel);
				msg = exist(nameArchRequest,data_config.montaje);
				enviar(kernel,(void *)&msg,sizeof(uint32_t));
				break;
			case SIZE_MSG:
				nameArchRequest = obtieneNombreArchivo(kernel);
				msg = sizeFile(nameArchRequest,data_config.montaje);
				enviar(kernel,(void *)&msg,sizeof(uint32_t));
				break;
			default:
				flag = 0;
				msg= -10;
				enviar(kernel,(void *)&msg,sizeof(uint32_t));
		}
		if(flag)
			free(nameArchRequest);
			
	}

	free(cfgPath);

	return 0;
}
