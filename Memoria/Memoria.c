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

#define PORT 5000 //Esto lo levanta del archivo de config en realidad, es solo para probar
#define BACKLOG 5 //Cantidad maxima de conexiones pendientes en listener

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

	//Sockets para hacer que la Memoria sea Servidor

	//variables

		fd_set master;   					//conjunto maestro de descriptores de fichero (contiene todos)
		fd_set read_fds;					//conjunto de descriptores de fichero para select()
		struct sockaddr_in myaddr; 			//Mis datos
		struct sockaddr_in remoteaddr; 		//Datos del cliente
		int fdmax, bytes_leidos;
		int listener, newfd, j;
		char buffer[256];
		int yes = 1;
		socklen_t addrlen = sizeof(struct sockaddr_in);
		FD_ZERO(&master); 					//Me aseguro de que no tengan basura
		FD_ZERO(&read_fds);
		/*Inicializo el buffer*/
		for(j=0;j<256;j++)
		{
			buffer[j]='\0';
		}

		//socket()
		if ((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
		}
		//Para evitar problemas de address already in use:
		if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes,
		sizeof(int)) == -1) {
		perror("setsockopt");
		exit(1);
		}
		/*Aca relleno la estructura de esta manera, si no les gusta avisenme
		 y lo hago con struct sockaddr hints*/
		myaddr.sin_family=AF_INET;
		myaddr.sin_addr.s_addr = INADDR_ANY;
		myaddr.sin_port = htons(PORT);
		memset(&(myaddr.sin_zero), '\0', 8);  /*Estos \0 agregados son para que tenga el mismo tamaño que
												sockaddr y despues la puedo castear*/

		//bind()
		//&myaddr casteado porque bind recibe (struct sockaddr *)
		if (bind(listener, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1) {
		perror("bind");
		exit(1);
		}

		// listen()
		if (listen(listener, BACKLOG) == -1) {
		perror("listen");
		exit(1);
		}
		FD_SET(listener, &master);
		fdmax = listener;
		read_fds = master;

		//select() para listener
			if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(1);
			}
			//Si el listener esta listo, hago accept
			if (FD_ISSET(listener,&read_fds))
			{
				//accept()
				if ((newfd = accept(listener, (struct sockaddr *)&remoteaddr,
				&addrlen)) == -1)
				{
					perror("accept error");
				}
				else
				{
					FD_SET(newfd, &master);
					FD_ZERO(&read_fds);
					FD_SET(newfd,&read_fds);
					if (newfd > fdmax)
					{
						fdmax=newfd;
					}
					printf("Se recibio conexion de: %s\n",inet_ntoa(remoteaddr.sin_addr));
				}

				//select() para newfd
				if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
					perror("select");
					exit(1);
					}
				else
				{
					//Si newfd esta listo, hago recv
					if (FD_ISSET(newfd,&read_fds))
					{
						if ((bytes_leidos = recv(newfd, buffer, sizeof(buffer)-1, 0)) <= 0)
						{
							if (bytes_leidos == 0)
							{
								printf("Se desconecto");
							}
							else {
							perror("recv");
							}
							close(newfd);
						}
						else
						{
							int k;
							buffer[bytes_leidos]='\0';
							printf("Se recibio:\n");
							for(k=0;k<bytes_leidos;k++)
							{
								//Imprimo asi el buffer porque si no me imprime solo 4 bytes, no se por que
							printf("%c",buffer[k]);
							}
						}
					}
					close(newfd);
				}

			 }
			close(listener);


	return 0;
}
