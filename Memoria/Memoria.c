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
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"

int main(int argc, char** argv){

	t_config *config;
	mem_config data_config;

	config = config_create_from_relative_with_check(argc,argv);

	data_config.puerto = config_get_string_value(config, "PUERTO");
	data_config.marcos = config_get_int_value(config, "MARCOS");
	data_config.marco_size = config_get_int_value(config, "MARCO_SIZE");
	data_config.entradas_cache = config_get_int_value(config, "ENTRADAS_CACHE");
	data_config.cache_x_proceso = config_get_int_value(config, "CACHE_X_PROC");
	data_config.reemplazo_cache = config_get_string_value(config, "REEMPLAZO_CACHE");
	data_config.retardo_memoria = config_get_int_value(config, "RETARDO_MEMORIA");

	printf("PORT = %s\n", data_config.puerto);
	printf("MARCOS = %d\n", data_config.marcos);
	printf("MARCO_SIZE = %d\n", data_config.marco_size);
	printf("ENTRADAS_CACHE = %d\n", data_config.entradas_cache);
	printf("CACHE_X_PROCESO = %d\n", data_config.cache_x_proceso);
	printf("REEMPLAZO_CACHE = %s\n", data_config.reemplazo_cache);
	printf("RETARDO_MEMORIA = %d\n", data_config.retardo_memoria);

	int portnum;
	portnum=atoi(data_config.puerto); /*Lo asigno antes de destruir config*/

	config_destroy(config);

	/*Sockets para recibir mensaje del Kernel*/

	/*Funciones*/

	//Backlog es la cantidad maxima que quiero de conexiones pendientes

	void poner_a_escuchar(int sockfd, struct sockaddr* server, int backlog)
	{
		if((bind(sockfd, server, sizeof(struct sockaddr)))==-1)
			{
				perror("bind");
				exit(1);
			}
		if(listen(sockfd,backlog)==-1)
			{
				perror("listen");
				exit(1);
			}
		return;
	}

	struct sockaddr_in crear_estructura_server (int puerto)
	{
		struct sockaddr_in server;
		server.sin_family=AF_INET;
		server.sin_port=htons(puerto);
		server.sin_addr.s_addr=INADDR_ANY;
		memset(&(server.sin_zero),'\0',8);
		return server;
	}

	int aceptar_conexion(int sockfd, struct sockaddr* clien)
	{
		int socknuevo;
		socklen_t clie_len=sizeof(struct sockaddr_in);
		if((socknuevo=accept(sockfd, clien, &clie_len))==-1)
		{
			perror("accept");
		}
		return socknuevo;
	}


	int listener, newfd, bytes_leidos, bytes;
	char buf[256];
	struct sockaddr_in server, cliente;

	int pid = getpid();

	/*inicializo el buffer*/

	memset(buf, 0, sizeof buf);
	memset(&(cliente.sin_zero),'\0',8);

	/*Creo estructura server*/

	server = crear_estructura_server(portnum);

	/*socket()*/

	if((listener=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		perror("socket");
		exit(1);
	}

	/*bind() y listen()*/

	poner_a_escuchar(listener, (struct sockaddr *) &server, 1);

	/*accept()*/

	newfd = aceptar_conexion(listener, (struct sockaddr*) &cliente);

	/*Handshake*/

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
	memset(buf, 0, sizeof buf);
	sprintf(buf, "Te has conectado a memoria %d", pid);
	send(newfd, buf, sizeof buf, 0);

	/*recv()*/

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
