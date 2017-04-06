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

#define PUERTO 5000 //5000 para probar, esto en realidad lo saca de su archivo de configuracion

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


	//Sockets para recibir mensaje del Kernel

	//variables
	int listener, newfd, i, bytes_leidos, fdmax;
	int yes=1;
	struct sockaddr_in server;
	struct sockaddr_in cliente;
	server.sin_addr.s_addr=INADDR_ANY;
	server.sin_port=htons(PUERTO);
	server.sin_family=AF_INET;
	memset(&(server.sin_zero),'\0',8);
	char buffer[256];
	fd_set master, ready;

	for(i=0;i<256;i++)
	{
		buffer[i]='\0';
	}
	FD_ZERO(&master);
	FD_ZERO(&ready);

	//socket()
	if((listener=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		perror("socket");
		exit(1);
	}

	//Prevenimos error de addres already in use blablabla..
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes,sizeof(int)) == -1)
	{
		perror("setsockopt");
		exit(1);
	}
	//bind()
	if((bind(listener,(struct sockaddr *)&server,sizeof(struct sockaddr_in)))==-1)
	{
		perror("bind");
		exit(1);
	}

	//listen()
	if((listen(listener,5))==-1)
	{
		perror("listen");
		exit(1);
	}
	FD_SET(listener, &master); //Se agrega listener al conjunto maestro de file descriptors
	fdmax=listener;
	ready = master;

	//select() para listener
	if (select(fdmax+1, &ready, NULL, NULL, NULL) == -1)
	{
		perror("select");
		exit(1);
	}

	//Luego del select preguntamos si listener esta listo para gestionar alguna conexion
	if (FD_ISSET(listener,&ready))
	{
		//accept()
		socklen_t cliesize = sizeof(struct sockaddr_in);
		if((newfd=accept(listener,(struct sockaddr*)&cliente,&cliesize))==-1)
		{
			perror("accept");
		}
		else
		{
			FD_SET(newfd,&master);
			FD_ZERO(&ready);
			FD_SET(newfd,&ready); //En ready dejo solo el newfd que es quien me interesa saber si esta listo para recibir

			if (newfd > fdmax)
			{
				fdmax=newfd;
			}
		}

		//select() para newfd
		if (select(fdmax+1, &ready, NULL, NULL, NULL) == -1)
		{
			perror("select");
			exit(1);
		}

		//Luego del select pregunto si newfd esta listo para recibir un mensaje
		if(FD_ISSET(newfd,&ready))
		{
			if((bytes_leidos=recv(newfd,buffer,255,0))<=0)
			{
				if(bytes_leidos==0)
				{
					printf("Conexion cerrada\n");
				}
				else
				{
					perror("recv");
				}
				close(newfd);
			}

			buffer[bytes_leidos]='\0';
			printf("%s\n",buffer);
			close(newfd);
		}

	close(listener);
	}

	return 0;
}
