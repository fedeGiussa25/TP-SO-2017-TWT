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
#include <pthread.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"


int sockfd_kernel; // estaba en el main, pero tengo que ponerlo aca porque lo tengo que usar tambien en la funcion que le paso al hilo


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

void delete_multiple_spaces(char *str) // lo saqué de stackoverflow -> "http://stackoverflow.com/questions/16790227/replace-multiple-spaces-by-single-space-in-c"
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
       *dest++ = *str++;    // si hay espacios adicionales, 'str' esta mas adelante de 'dest', entonces copia lo que esta mucho despues
       	   	   	   	   	    // (por esos espacios adicionales), a una posicion anterior, sobreescribiendo los espacios adicionales en el proceso;
       	   	   	   	   	    // ambos punteros apuntan al mismo string, pero pueden estar apuntando a posiciones diferentes...
       	   	   	   	   	    // es dificil, pero hay que imaginarse como que 'dest' puede estar apuntando a algo anterior a lo que apunta 'str' o a lo mismo
    }

    /* Make sure the string is properly terminated */
    *dest = '\0';
}


char *clean_script(FILE *file, int *scriptSize)
{
	char *script = malloc(256);
	*script = '\0'; 							// me aseguro que haya un string vacio
	char *line = malloc(51);
	int currentLength = 0;						// el largo, en un momento dado, del script
	int lineLength = 0;


	while(fgets(line, 51, file ) != NULL)
	{
		delete_multiple_spaces(line);
		lineLength = strlen(line);
		if(lineLength == 1 || lineLength == 2) continue;  //asi limpio los saltos de linea, ya que el fgets() lee tambien los saltos de linea; si 'lineLength' es 1, tengo un '\n'; si es 2 tengo ' \n'
		strcat(script+currentLength, line);	//copia el contenido de line desde el ultimo \0 de script (elimina ese \0 y agrega uno al final)
		currentLength += lineLength;
	}
	*scriptSize = currentLength;
	free(line);
	return script;
}


void *process_script(char *path)
{
	FILE *file;
	int scriptLength = 0;
	int codigo = 2;

	if ((file = fopen(path, "r")) == NULL)
	{
		perror("Consola, linea 124, error de archivo: ");
		exit(1);
	}

	printf("El archivo existe y fue abierto\n");
	char *script = clean_script(file, &scriptLength); //le saca las partes del archivo que no me sirven; ya me devuelve el puntero a una posicion de memoria libre con todo el text

	fclose(file);

	printf("Script procesado\n");
	printf("Contenido del script a enviar:\n%s", script);

	void* realbuf = malloc((sizeof(int)*2)+scriptLength);

	memcpy(realbuf,&codigo,sizeof(int));
	memcpy(realbuf+sizeof(int),&scriptLength, sizeof(int));
	memcpy(realbuf+(sizeof(int)*2), script, scriptLength);

	send(sockfd_kernel, realbuf, (sizeof(int)*2)+scriptLength, 0);

	printf("Script enviado!\n");

	free(realbuf);
	free(script);
}




int main(int argc, char** argv) {

		t_config *config;
		consola_config data_config;
		char *buf = malloc(256);
		int codigo;
		int idProceso = 2;
	//	int messageLength;


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

	/*	int codigo2 = 2;
		while(1){
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

		char *path = malloc(30); // no creo que la ruta sea taaan larga como para superar 30 caracteres
				
		printf("Escriba la ruta del script a ejecutar: ");
		
		if(scanf("%s", path) == EOF)	// puse esto porque rompe las bolas al compilar... y por seguridad
		{
			perror("Consola, linea 211, error en scanf: ");
			exit(1);
		}
		
		pthread_t script_tret;
		int tret_value = -1;
		
		if((tret_value = pthread_create(&script_tret, NULL,(void*) process_script, path)) != 0)
		{
			perror("Consola, linea 220, error al crear el hilo: ");
			exit(1);
		}
		else
		{
			printf("Hilo creado satisfactoriamente\n\n");
		}

		pthread_join(script_tret,0);

		config_destroy(config);
		return 0;
}
