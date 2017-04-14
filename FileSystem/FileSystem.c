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

#define PORTNUM 5003

typedef struct{
	char* puerto;
	char *montaje;
}fs_config;

int main(int argc, char** argv)
{

	if(argc == 0){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}

	t_config *config;
	fs_config data_config;
	char *montaje, *path, *ruta, *nombre_archivo, *puerto;

	ruta = argv[1];		//Guardamos el primer parametro que es la ruta del archivo
	nombre_archivo = argv[2];		//Guardamos el segundo parametro que es el nombre del archivo

	//Formamos la path del config
	path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
	strcpy(path, "../../");
	strcat(path,ruta);
	strcat(path, nombre_archivo);

	char *key1 = "PUERTO";
	char *key2 = "PUNTO_MONTAJE";

	config = config_create(path);		//Creamos el t_config

	//Leemos los datos
	data_config.puerto = config_get_string_value(config, key1);
	data_config.montaje = config_get_string_value(config, key2);

	printf("PORT = %s\n", data_config.puerto);
	printf("Montaje = %s\n", data_config.montaje);

	config_destroy(config);		//Eliminamos fs_config, linberamos la memoria que utiliza
	free(path);		//liberamos lo que alocamos previamente


	/*Sockets para recibir mensaje del Kernel*/

	int listener, newfd, i, bytes_leidos, bytes;
	char buf[256];
	struct sockaddr_in server, cliente;
	int pid = getpid();

	/*inicializo el buffer*/

	for(i=0;i<256;i++)
	{
		buf[i]='\0';
	}

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
	sprintf(buf, "Te has conectado a FS %d", pid);
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
