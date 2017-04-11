/*
 ============================================================================
 Name        : CPU.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <commons/config.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"

int main(int argc, char **argv) {

	// SETTEO DESDE ARCHIVO DE CONFIGURACION

	//Variables para config
	t_config *config_file;
	cpu_config data_config;

	config_file = config_create_from_relative_with_check(argc,argv);

	data_config.ip_kernel = config_get_string_value(config_file, "IP_KERNEL");
	data_config.puerto_kernel = config_get_string_value(config_file, "PUERTO_KERNEL");

	printf("IP Kernel: %s\n",data_config.ip_kernel);
	printf("Puerto Kernel: %s\n\n",data_config.puerto_kernel);

	// CONEXION A KERNEL

	char buf[256];
	int statusgetaddrinfo, fd, bytes;
	struct addrinfo hints, *sockinfo, *aux;

	//Me aseguro que hints este vacio, lo necesito limpito o el getaddrinfo se puede poner chinchudo
	memset(&hints,0,sizeof(hints));

	//Setteo (como dice el TP) un addrinfo con los datos del socket que quiero
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	//Esta funcion settea sockinfo con una lista de addrinfos que voy a usar despues.
	//statusgetaddrinfo solo me sirve para saber si se ejecuto la funcion correctamente.
	statusgetaddrinfo = getaddrinfo(data_config.ip_kernel,data_config.puerto_kernel,&hints,&sockinfo);
	if(statusgetaddrinfo != 0)
	{
		//Si me da distinto de 0 => Hubo un error y lo printeo, gai_strerror me indica el tipo de error.
		printf("getaddrinfo: %s\n",gai_strerror(statusgetaddrinfo));
		exit(1);
	}

	//Como sockinfo tiene varios addrinfo tengo que sacar el que me deje conectarme.
	for(aux = sockinfo; aux != NULL; aux = aux->ai_next)
	{
		//Paso la info de uno de los sockaddr para recibir el file descriptor (FD) de un socket.
		fd = socket(aux->ai_family, aux->ai_socktype, aux->ai_protocol);
		if(fd==-1)
		{
			perror("socket");
	        continue;
	    }

		//Si el FD esta bien, trato de conectarme.
		//Si esto falla no necesariamente esta mal, itero la funcion porque tal vez otro de los
		//sockaddr si me conecta.
	    if (connect(fd, aux->ai_addr, aux->ai_addrlen) == -1)
	    {
	        close(fd);
	        perror("connect");
	        continue;
	    }

	    break;
	}

	//Si aux es NULL entonces no me pude conectar con ninguno de los sockaddr y aborto el programa.
	if(aux == NULL)
	{
		printf("connection status: failed");
		exit(2);
	} else fprintf(stderr,"connection status: success\n");

	//Sockinfo debe irse, su planeta lo necesita
	free(sockinfo);

	//Esto es el handshake, solo envia basura
	char basura[256];
	memcpy(basura, "krgr", strlen("krgr"));
	if(send(fd,basura,sizeof buf,0)==-1)
	{
		perror("send");
		exit(3);
	}

	//Y aqui termina la CPU, esperando e imprimiendo mensajes hasta el fin de los tiempos
	//O hasta que cierres el programa
	//Lo que pase primero
	//while(1)
	//{
		bytes = recv(fd,buf,sizeof buf,0);
		if(bytes == -1)
		{
			perror("recieve");
			exit(3);
		}
		printf("%s\n",buf);
	//}

	close(fd);

	return 0;
}
