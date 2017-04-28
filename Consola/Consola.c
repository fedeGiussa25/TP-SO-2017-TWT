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

int delete_multiple_spaces(char *str) // lo saqué de stackoverflow -> "http://stackoverflow.com/questions/16790227/replace-multiple-spaces-by-single-space-in-c"
{
	char *dest = str;  /* Destination to copy to */

    /* While we're not at the end of the string, loop... */
    while (*str != '\0')
    {
        /* Loop while the current character is a space, AND the next
         * character is a space
         */
        while (*str == ' ' && *(str + 1) == ' ')
            str++;  /* Just skip to next character */

       /* Copy from the "source" string to the "destination" string,
        * while advancing to the next character in both
        */
       *dest++ = *str++;
    }

    /* Make sure the string is properly terminated */    
    *dest = '\0';
	
	return strlen(str);		// falta verificar si todo esto funciona bien o hay que cambiar algo, pero ya me dejó medio loco
}


char *clean_script(FILE *file, int *scriptSize)
{
	char *script = malloc(256*sizeof(char)); 	
	*script = '\0'; 							// me aseguro que haya un string vacio
	char *line = malloc(51*sizeof(char)); 		
	int currentLength = 0;						// el largo, en un momento dado, del script
	int lineLength = 0;
	
	
	while(fgets(line, 500, file ) != NULL)
	{	
		lineLength = delete_multiple_spaces(line);	//primero limpio; de paso me devuelve la longitud de la linea limpia
		if(lineLength == 1) continue;  //asi limpio los saltos de linea, ya que el fgets() lee tambien los saltos de linea
		strcat(script+currentLength, line);	//copia el contenido de line desde el ultimo \0 de script (elimina ese \0 y agrega uno al final)
		currentLenght += lineLength	
	}
	
	return script;
}

int main(int argc, char** argv) {

		t_config *config;
		consola_config data_config;
		char *buf = malloc(256);
		int sockfd_kernel, codigo, codigo2;
		int idProceso = 2;
		int messageLength;


		config = config_create_from_relative_with_check(argc,argv);

		data_config.ip_kernel = config_get_string_value(config, "IP_KERNEL");
		data_config.puerto_kernel = config_get_string_value(config, "PUERTO_KERNEL");

		printf("IP_KERNEL = %s\n", data_config.ip_kernel);
		printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

		//Nos conectamos
		sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

		memset(buf,0,256);
		/*codigo = 2;
		if(send(sockfd_kernel,&codigo,sizeof(int),0)==-1)
			{
				perror("send");
				exit(3);
			}*/
		void* codbuf = malloc(sizeof(int)*2);
		codigo =1;
		memcpy(codbuf,&codigo,sizeof(int));
		memcpy(codbuf + sizeof(int),&idProceso, sizeof(int));
		send(sockfd_kernel, codbuf, sizeof(int)*2, 0);
		free(codbuf);

		codigo2 =2;

		/*while(1){
			memset(buf,0,256);
			fgets(buf,256,stdin);
			messageLength = strlen(buf)-1;
			void* realbuf = malloc((sizeof(int)*2)+messageLength);
			memcpy(realbuf,&codigo2,sizeof(int));
			memcpy(realbuf+sizeof(int),&messageLength, sizeof(int));
			memcpy(realbuf+sizeof(int)+sizeof(int),buf,messageLength);
			send(sockfd_kernel, realbuf, messageLength+(sizeof(int)*2), 0);
			memset(buf,0,256);
			free(realbuf);
		}*/
		
				// A partir de aca me encargo de los scripts a ejecutar
				
		FILE *file;
		int scriptLength = 0;
		char *path = malloc(64*sizeof(char)); 
		pthread_t tret;
				
		printf("Escriba la ruta del script a ejecutar: ");
		scanf("%s", path);
		
		if ((file == fopen(path, "r")) != NULL)
		{
		    printf("El archivo existe y fue abierto");
			char *script = clean_script(file, &scriptLength); //le saca las partes del archivo que no me sirven; ya me devuelve el puntero a una posicion de memoria libre con todo el text
			fclose(file);
		}
		else
		{
			perror("Error de archivo: ");
			exit(1);
		}
		codigo =2;
		void* realbuf = malloc((sizeof(int)*2)+scriptLength);

		memcpy(realbuf,&codigo,sizeof(int));
		memcpy(realbuf+sizeof(int),&scriptLength, sizeof(int));
		memcpy(realbuf+(sizeof(int)*2), script, scriptLength);
		send(sockfd_kernel, realbuf, (sizeof(int)*2)+scriptLength, 0); // falta hacerle un hilo a este send
		free(realbuf);
		free(script);

		config_destroy(config);
		return 0;
}
