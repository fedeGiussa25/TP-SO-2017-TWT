#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commons/config.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "socketEze.h"
#include <errno.h>
#include "fs.h"
#include <dirent.h>
#include <sys/stat.h>
#include <commons/bitarray.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#define SIZE_MSG 11
#define EXIST_MSG 12
#define PATH_ARCH "Archivos/"
#define PATH_META "Metadata/"
#define PATH_BLQ "Bloques/"

char *pathArchivos;
char *pathMetadata;
char *pathBloques;
fs_config data_config;
uint32_t tamanioBloques;
uint32_t cantidadBloques;
int main(int argc, char** argv)
{
	t_config *config;

	checkArguments(argc);
	config = config_create(argv[1]);
	data_config.puerto = config_get_string_value(config, "PUERTO");
	data_config.montaje = config_get_string_value(config, "PUNTO_MONTAJE");
	uint32_t puerto = atoi(data_config.puerto);
	char *montaje = malloc(strlen(data_config.montaje));
	strcpy(montaje,data_config.montaje);
	printf("PORT = %d\n", puerto);
	printf("Montaje = %s\n", data_config.montaje);
	config_destroy(config);		//Eliminamos fs_config, liberamos la memoria que utiliza
	//todo el server declaradito aca
	pathMetadata = unir_str(montaje,PATH_META);
	pathBloques= unir_str(montaje,PATH_BLQ);
	pathArchivos= unir_str(montaje,PATH_ARCH);
	int32_t i;
	find_or_create("./",montaje);
	find_or_create(montaje,PATH_ARCH);
	
	find_or_create(montaje,PATH_META);

	char* miMetadata = unir_str(pathMetadata,"Metadata.bin");
	if(!exist("Metadata.bin",pathMetadata))
		createDefaultMetadata(pathMetadata);
	config = config_create(miMetadata);
	tamanioBloques = config_get_int_value(config,"TAMANIO_BLOQUES");
	cantidadBloques = config_get_int_value(config,"CANTIDAD_BLOQUES");
	//char *magicNumber = config_get_string_value(config,"MAGIC_NUMBER");
/*	if(!strcmp(magicNumber,magicNumber))
		exit(4);*/
	config_destroy(config);

	free(miMetadata);
	if(!exist("Bitmap.bin",pathMetadata))
		create_binFile(pathMetadata,"Bitmap",cantidadBloques/8);


	int32_t aux = find_or_create(montaje,PATH_BLQ);
	if(aux == 0){
		printf("%d\n",cantidadBloques);

		for(i=0;i<cantidadBloques;i++){
			char *nro = int_to_str(i+1);
			create_block(pathBloques,nro,tamanioBloques);
			free(nro);
		}
	}

	printf("Cada bloque sera de %d\nHabra %d Bloques \n",tamanioBloques,cantidadBloques);
	//EL ATOI NO ANDA BIEN Y TIRA UN 0 EN VEZ DEL PUERTO
	int32_t miSocket = servidor(puerto,1);
	//end

	//ASI FUNCIONA EL MUNDO FILESYSTEM
//					///								///
//		MENSAJE		///	dimension TEXT	//	TEXT	///
//	exist	size	///								///
	int32_t kernel;
	if((kernel = aceptarCliente(miSocket,1)) == -1)
		exit(5);

	//NO ESTOY SEGURO COMO ANDA EL MOUNT PERO SI LE TIRAS ESTO FUNCA, POR FAVOR MIRALO EZE
	
	char *nameArchRequest;
	uint32_t msg;
	while(1){
		recibir(kernel,(void *)&msg,sizeof(int32_t));
		switch(msg){
			case EXIST_MSG:
				nameArchRequest = obtieneNombreArchivo(kernel);
				msg = exist(nameArchRequest,montaje);
				enviar(kernel,(void *)&msg,sizeof(uint32_t));
				break;
			case SIZE_MSG:
				nameArchRequest = obtieneNombreArchivo(kernel);
				msg = sizeFile(nameArchRequest,montaje);
				enviar(kernel,(void *)&msg,sizeof(uint32_t));
				break;
			default:
				msg= -10;
				enviar(kernel,(void *)&msg,sizeof(uint32_t));
		}

	}
	free(pathMetadata);
	free(pathBloques);
	free(pathArchivos);
	return 0;
}
