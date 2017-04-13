#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORTNUM 5004 //Esto lo levanta del archivo de config en realidad, es solo para probar


typedef struct{
	char* puerto;
	int marcos;
	int marco_size;
	int entradas_cache;
	int cache_x_proceso;
	char *reemplazo_cache;
	int retardo_memoria;
} mem_config;


int main(int argc, char** argv){

	if(argc == 0){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}

	t_config *config;
	mem_config data_config;
	char *path, *ruta, *nombre_archivo;

	ruta = argv[1];
	nombre_archivo = argv[2];

	path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
	strcpy(path, "../../");
	strcat(path,ruta);
	strcat(path, nombre_archivo);

	char *key1 = "PUERTO";
	char *key2 = "MARCOS";
	char *key3 = "MARCO_SIZE";
	char *key4 = "ENTRADAS_CACHE";
	char *key5 = "CACHE_X_PROC";
	char *key6 = "REEMPLAZO_CACHE";
	char *key7 = "RETARDO_MEMORIA";

	config = config_create(path);

	data_config.puerto = config_get_string_value(config, key1);
	data_config.marcos = config_get_int_value(config, key2);
	data_config.marco_size = config_get_int_value(config, key3);
	data_config.entradas_cache = config_get_int_value(config, key4);
	data_config.cache_x_proceso = config_get_int_value(config, key5);
	data_config.reemplazo_cache = config_get_string_value(config, key6);
	data_config.retardo_memoria = config_get_int_value(config, key7);

	printf("PORT = %s\n", data_config.puerto);
	printf("MARCOS = %d\n", data_config.marcos);
	printf("MARCO_SIZE = %d\n", data_config.marco_size);
	printf("ENTRADAS_CACHE = %d\n", data_config.entradas_cache);
	printf("CACHE_X_PROCESO = %d\n", data_config.cache_x_proceso);
	printf("REEMPLAZO_CACHE = %s\n", data_config.reemplazo_cache);
	printf("RETARDO_MEMORIA = %d\n", data_config.retardo_memoria);

	config_destroy(config);
	free(path);

	/*Sockets para recibir mensaje del Kernel*/

	int listener, newfd, i, bytes_leidos, bytes;
	char buf[256];
	struct sockaddr_in server, cliente;

	/*inicializo el buffer*/

	memset(buf, 0, sizeof buf);

	server.sin_family=AF_INET;
	server.sin_port=htons(PORTNUM);
	server.sin_addr.s_addr=INADDR_ANY;
	memset(&(server.sin_zero),'\0',8);
	memset(&(cliente.sin_zero),'\0',8);

	/*socket()*/

	if((listener=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		perror("socket");
		exit(1);
	}

	/*bind()*/

	if((bind(listener,(struct sockaddr *) &server,sizeof(struct sockaddr)))==-1)
	{
		perror("bind");
		exit(1);
	}

	/*listen()*/

	if(listen(listener,1)==-1)
	{
		perror("listen");
		exit(1);
	}

	/*accept()*/

	socklen_t clie_len=sizeof(struct sockaddr_in);

	if((newfd=accept(listener,(struct sockaddr *) &cliente,&clie_len))==-1)
	{
		perror("accept");
	}

	/*recv()*/
	/*while(1){
		if((bytes_leidos=recv(newfd,buffer,sizeof buffer,0))<=0)
		{
			if(bytes_leidos==0)
			{
				printf("Se desconecto el cliente");
				return 1;
			}
			else
			{
				perror("recv");
				exit(1);
			}
		}
	}*/

	while(1)
		{
			memset(buf, 0, sizeof buf);
			bytes = recv(newfd,buf,sizeof buf,0);
			if(bytes > 0){
						printf("%s\n",buf);
			}else{
				if(bytes == -1){
					perror("recieve");
					exit(3);
					}
				if(bytes == 0){
					printf("Se desconecto el socket: %d\n", newfd);
					close(newfd);
				}
			}
		}

	/*Mostramos el mensaje recibido*/

	buf[bytes_leidos]='\0';
	printf("%s",buf);

	return 0;
}
