#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <commons/config.h>
#include <commons/log.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "socketEze.h"
#include <errno.h>
#include "fs.h"
#include <dirent.h>
#include <sys/stat.h>
#include <commons/bitarray.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#define SIZE_MSG 15
#define VALIDAR_MSG 11
#define CREAR_MSG 12
#define READ_MSG 13
#define WRITE_MSG 14
#define DEL_MSG 15
#define PATH_ARCH "Archivos"
#define PATH_META "Metadata/"
#define PATH_BLQ "Bloques/"

char *pathArchivos;
char *pathMetadata;
char *pathBloques;
fs_config data_config;
int32_t tamanioBloques;
int32_t cantidadBloques;



t_bitarray *ready_to_work(char *pathBitmap,void* data,t_log *log){
	int32_t fd = open(pathBitmap,O_RDWR);
	struct stat sbuf;
	fstat(fd,&sbuf);
	if(fd <0)
		exit(8);
	data = mmap((caddr_t)0,sbuf.st_size,PROT_READ|PROT_WRITE, MAP_SHARED, fd,0);
	if(data == -1){
        log_error(log,"ERROR AL MAPEAR BITMAP");
        exit(3);
    }
	if(msync(data,sbuf.st_size,MS_ASYNC)==-1)
    {
        log_error(log,"ERROR AL SINCRONIZAR BITMAP");
        exit(4);
    }
	t_bitarray *dataB = bitarray_create_with_mode(data,sbuf.st_size,MSB_FIRST);
	if(dataB == -1){
        log_error(log,"ERROR AL CREAR BITARRAY");
        exit(10);
    }
	close(fd);
	return dataB;

}

int main(int argc, char** argv)
{
	t_config *config;
    t_log *miLog = log_create("out.log","FileSystem",true,LOG_LEVEL_INFO);
	checkArguments(argc);
	config = config_create(argv[1]);
	if(config ==NULL)
		exit(-1);

    if(!config_has_property(config,"PUERTO")|| !config_has_property(config,"PUNTO_MONTAJE")){
        log_error(miLog,"ARCHIVO DE CONFIGURACION INVALIDO -- SALIENDO");
        exit(-2);
    }

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
	config = config_create(miMetadata);
	if(config == NULL){
		createDefaultMetadata(pathMetadata);
		config = config_create(miMetadata);
	}
	tamanioBloques = config_get_int_value(config,"TAMANIO_BLOQUES");
	cantidadBloques = config_get_int_value(config,"CANTIDAD_BLOQUES");
	//char *magicNumber = config_get_string_value(config,"MAGIC_NUMBER");
/*	if(!strcmp(magicNumber,magicNumber))
		exit(4);*/
	config_destroy(config);

	free(miMetadata);
	if(!exist("Bitmap.bin",pathMetadata) || sizeFile("Bitmap.bin",pathMetadata)!= cantidadBloques/8)
		create_binFile(pathMetadata,"Bitmap",cantidadBloques/8);
	char *miBitmap = unir_str(pathMetadata,"Bitmap.bin");
	void* dataArchivo;
	t_bitarray *bitmap = ready_to_work(miBitmap,dataArchivo,miLog);
	free(miBitmap);
	int32_t aux = find_or_create(montaje,PATH_BLQ);
	if(aux == 0 || !exist("0.bin",pathBloques)){
		printf("%d\n",cantidadBloques);

		for(i=0;i<cantidadBloques;i++){
			char *nro = int_to_str(i);
			create_block(pathBloques,nro,tamanioBloques);
			free(nro);
		}
	}

	log_info(miLog,"Cada bloque sera de %d bytes\nHabra %d Bloques \n",tamanioBloques,cantidadBloques);
	int32_t miSocket = servidor(puerto,1);
	//end

	//ASI FUNCIONA EL MUNDO FILESYSTEM
//					///								///
//		MENSAJE		///	dimension TEXT	//	TEXT	///
//	exist	size	///								///
	int32_t kernel;
	if((kernel = aceptarCliente(miSocket,1)) == -1)
		exit(5);
	
	char *nameArchRequest;
	int32_t msg,offset,size;
	while(1){
		log_info(miLog,"ESPERANDO INSTRUCCION");
		if(recibir(kernel,&msg,sizeof(int32_t))==NULL)
			break;
		log_info(miLog,"INSTRUCCION RECIBIDA -- %d",msg);

		switch(msg){
			case VALIDAR_MSG:
				nameArchRequest = obtieneNombreArchivo(kernel);
				msg = validar_archivo(nameArchRequest,pathArchivos,miLog);
                if(msg == false)
                    delete_archivo(nameArchRequest,pathArchivos,bitmap,miLog);
				free(nameArchRequest);
				enviar(kernel,(void *)&msg,sizeof(int32_t));
				break;
			case CREAR_MSG:
				nameArchRequest = obtieneNombreArchivo(kernel);
				msg = create_archivo(nameArchRequest,pathArchivos,bitmap,miLog);
				free(nameArchRequest);
				enviar(kernel,(void *)&msg,sizeof(int32_t));
				break;
            case READ_MSG:
                nameArchRequest = obtieneNombreArchivo(kernel);

                if(recibir(kernel,&offset,sizeof(int32_t)) == NULL)
                    exit(6);
                if(recibir(kernel,&size,sizeof(int32_t)) == NULL)
                    exit(6);

                log_info(miLog,"Quieren Leer %d desde %d para %s",size,offset,nameArchRequest);
                void *data = obtener_datos(nameArchRequest,pathArchivos,offset,size,tamanioBloques,pathBloques,miLog,&msg);
                //log_info(miLog,"EL CODIGO DE LECTURA ES %d y su size %d",msg,sizeof(msg));
                printf("El valor de codigo que mandamos es %d\n", msg);
                printf("ENVIANDO %d BYTES\n",send(kernel,(void*)&msg,sizeof(int32_t),0));
                if(data!=NULL && msg == 1){
                	log_info(miLog,"ENVIANDO BUFFER LEIDO");
                    enviar(kernel,data,size);
                    free(data);
                }
                free(nameArchRequest);
                break;
            case WRITE_MSG:
                nameArchRequest = obtieneNombreArchivo(kernel);
                int32_t offset,size;
                void* buffer;
                if(recibir(kernel,&offset,sizeof(int32_t)) == NULL)
                    exit(6);
                if(recibir(kernel,&size,sizeof(int32_t)) == NULL)
                    exit(6);
                buffer = malloc(size);
                if(recibir(kernel,buffer,size) == NULL)
                    exit(6);

                log_info(miLog,"Quieren escribir %d desde %d para %s",size,offset,nameArchRequest);

                msg = guardar_datos(nameArchRequest,pathArchivos,offset,size,buffer,bitmap,tamanioBloques,pathBloques,miLog);
                log_info(miLog,"EL CODIGO DE OPERACION ES %d",msg);
                enviar(kernel,(void *)&msg,sizeof(int32_t));
                free(buffer);
                free(nameArchRequest);
                break;
            case DEL_MSG:
                nameArchRequest = obtieneNombreArchivo(kernel);
				log_info(miLog,"ARCHIVO A Borrar %s",nameArchRequest);
                msg = delete_archivo(nameArchRequest,pathArchivos,bitmap,miLog);
                free(nameArchRequest);
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
	free(dataArchivo);
	bitarray_destroy(bitmap);
	return 0;
}
