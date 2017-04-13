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

typedef struct{
	char* ip_kernel;
	char* puerto_kernel;
}consola_config;

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
	if(argc == 0){
			printf("Debe ingresar ruta de .config y archivo\n");
			exit(1);
		}

		t_config *config;
		consola_config data_config;
		char *path, *ruta, *nombre_archivo;

		//Variables para la conexion con kernel
		char buf[256];
		int sockfd_kernel;

		ruta = argv[1];
		nombre_archivo = argv[2];

		path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
		strcpy(path, "../../");
		strcat(path,ruta);
		strcat(path, nombre_archivo);

		char *key1 = "IP_KERNEL";
		char *key2 = "PUERTO_KERNEL";

		config = config_create(path);

		data_config.ip_kernel = config_get_string_value(config, key1);
		data_config.puerto_kernel = config_get_string_value(config, key2);

		printf("IP_KERNEL = %s\n", data_config.ip_kernel);
		printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

		//Nos conectamos
		sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

		while(1){
			memset(buf, 0, 256*sizeof(char));	//limpiamos nuestro buffer
			fgets(buf, 256*sizeof(char), stdin);	//Ingresamos nuestro mensaje
			send(sockfd_kernel, buf, sizeof buf,0);
		}

		config_destroy(config);
		free(path);
		return 0;
}
