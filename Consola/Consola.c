/*
 ============================================================================
 Name        : consolaproto2.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"


//Pasas la ip y el puerto para la conexion y devuelve el fd del servidor correspondiente
int get_fd_server(char* ip, char* puerto){

	struct addrinfo hints;
	struct addrinfo *servinfo, *p;
	int sockfd, result;

	//Vaciamos hints para usarlo en la funcion getaddrinfo() y le setteamos el tipo de socket y la familia
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((result = getaddrinfo(ip, puerto, &hints, &servinfo)) != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
			return 1;
		}

	for(p = servinfo; p != NULL; p = p->ai_next) {
			if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				perror("client: socket");
				continue;
				}
				if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
					close(sockfd);
					perror("client: connect");
					continue;
					}
			break;
			}
	if (p == NULL) {
			fprintf(stderr, "client: failed to connect\n");
			return 2;
		}

	freeaddrinfo(servinfo);

	return sockfd;
}


int main(int argc, char** argv) {

		t_config *config;
		consola_config data_config;
		char buf[256];
		int sockfd_kernel, codigo;

		config = config_create_from_relative_with_check(argc,argv);

		data_config.ip_kernel = config_get_string_value(config, "IP_KERNEL");
		data_config.puerto_kernel = config_get_string_value(config, "PUERTO_KERNEL");

		printf("IP_KERNEL = %s\n", data_config.ip_kernel);
		printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

		//Nos conectamos
		sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

		codigo = 2;
		if(send(sockfd_kernel,&codigo,sizeof(int),0)==-1)
			{
				perror("send");
				exit(3);
			}

		while(1){
			memset(buf, 0, 256*sizeof(char));	//limpiamos nuestro buffer
			fgets(buf, 256*sizeof(char), stdin);	//Ingresamos nuestro mensaje
			send(sockfd_kernel, buf, sizeof buf,0);
		}

		config_destroy(config);
		return 0;
}
