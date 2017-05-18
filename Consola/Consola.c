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
#include <commons/collections/list.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"


int closeAllThreads = 0;		// si esta en uno, tengo que cerrar todos los hilos
int idProceso = 2;

consola_config data_config;

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


int handshake_kernel()
{
	int codigo = 1;
	int sockfd_kernel;

	//Handshake

	sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

	void* codbuf = malloc(sizeof(int)*2);
	memcpy(codbuf,&codigo,sizeof(int));
	memcpy(codbuf + sizeof(int),&idProceso, sizeof(int));

	send(sockfd_kernel, codbuf, sizeof(int)*2, 0);

	free(codbuf);
	return sockfd_kernel;
}


void delete_multiple_spaces(char *str)
{
	char *dest = str;  //puntero que guarda los cambios

    //loop hasta que termine el string
    while (*str != '\0')
    {
        //hace while mientras el caracter actual y el siguiente sean espacios

        while (*str == ' ' && *(str + 1) == ' ')
            str++;  // apunto al siguiente caracter


       //copio el caracter desde 'str' a 'dest', luego aumento los punteros

       *dest++ = *str++;    // si hay espacios adicionales, 'str' esta mas adelante de 'dest', entonces copia lo que esta mucho despues
       	   	   	   	   	    // (por esos espacios adicionales), a una posicion anterior, sobreescribiendo los espacios adicionales en el proceso;
       	   	   	   	   	    // ambos punteros apuntan al mismo string, pero pueden estar apuntando a posiciones diferentes...
       	   	   	   	   	    // es dificil, pero hay que imaginarse como que 'dest' puede estar apuntando a algo anterior a lo que apunta 'str' o a lo mismo
    }

    //me aseguro de terminar el string

    *dest = '\0';
}


void clean_script(FILE *file, int *scriptSize, char *script)
{
	*script = '\0'; 							// me aseguro que haya un string vacio
	char *line = malloc(51);
	int currentLength = 0;						// el largo, en un momento dado, del script
	int lineLength = 0;


	while(fgets(line, 51, file ) != NULL)
	{
		delete_multiple_spaces(line);
		lineLength = strlen(line);
		if(lineLength == 1 || lineLength == 2) continue;  //asi limpio los saltos de linea, ya que el fgets() lee tambien los saltos de linea; si 'lineLength' es 1, tengo un '\n'; si es 2 tengo ' \n'

		if(line[0] == '#' || line[1] == '#') continue;	 //elimina las lineas que empiezan con '#' (comentarios); la linea puede tener un ' ' ó '\t', por eso verifico los 2 primeros char de la linea

		strcat(script, line);							//copia el contenido de line desde el ultimo \0 de script (elimina ese \0 y agrega uno al final)
		currentLength += lineLength;
	}
	*scriptSize = currentLength;
	free(line);
}


void avisar_desconexion_kernel(int socket)
{
	int codigo = 8;

	send(socket, &codigo, sizeof(int), 0);
}


void *script_thread(char *scriptName)
{
	int threadId = pthread_self();
	int codigo;
	int sockfd_kernel;
	FILE *file;
	int scriptLength = 0;
	struct stat st;
	off_t fileSize;	//uso esto para despues determinar el tamaño de la variable script
					// sabiendo que 1 char = 1 byte, al sacar el nro de bytes del archivo, se cuantos char tiene
					// "off_t" = unsigned int 64

	sockfd_kernel = handshake_kernel();

	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}

	//Manejo el script

	char *filePath = malloc(51); // dudo que la ruta sea taaan larga como para superar 50 caracteres
	strcpy(filePath, "../../Files/Scripts/"); // aca hay 20 chars, asi que el nombre del archivo puede contener 30 mas

	strcat(filePath, scriptName);
	strcat(filePath, ".ansisop");  	// corri estas partes de la funcion "iniciar_programa" a acá para poder indicar de que script viene el msj que recibo de kernel
									// asi no se me mezcla cuando haya varios mensajes cayendo de distintos hilos

	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}


	if ((file = fopen(filePath, "r")) == NULL)
	{
		printf("\n\nHilo %d, error al abrir el archivo", threadId);
		perror("");
		printf("\nIntente abrir el script nuevamente\n");
		goto end;
	}

	printf("\n\nHilo %d: \n\t El archivo existe y fue abierto\n", threadId);

	stat(filePath, &st); //setea la info del archivo en la estructura 'st'
	fileSize = st.st_size;

	//char *script = clean_script(file, & scriptLength);  //con esto creo otra variable que come memoria y no se puede liberar... genera memory leaks
														  //en la funcion se hacia el malloc() para el script, pero asi nunca lo podia liberar


	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}


	char *script = malloc(fileSize);
	clean_script(file, &scriptLength, script); //le saca las partes del archivo que no me sirven; ya me devuelve el puntero a una posicion de memoria libre con todo el text
	fclose(file);


	/*	Lo siguiente me libera el viejo puntero 'script'
	 * y devuelve un puntero a otra estructura del mismo tipo y con los mismos datos que la que le paso ('script')
	 * pero que tiene el tamaño especificado en el 2do argumento ('scriptLength')
	 *
	 * El puntero 'script' tiene el tamaño del archivo (equivalente a su cantidad de chars), pero despues de limpiarlo
	 * tiene menos texto, asi que necesita menos espacio... por eso creo esta nueva estructura (asi limpio memoria)
	*/

	char *cleanScript = realloc(script, scriptLength+1); //es 'scriptLength+1' porque sin ese '+1' el valgrind dice que hay error de escritura


	printf("\n\n Hilo %d: Script \"%s\" procesado\n", threadId, scriptName);
	printf("\n\nHilo %d:\n\t Contenido del script a enviar:\n\n%s", threadId, cleanScript);


	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}


	void* realbuf = malloc((sizeof(int)*2)+scriptLength+1);

	codigo = 2;
	int respuesta = 0;
	int pid = 0;		//el ID del proceso en el que ejecuto el script

	memcpy(realbuf,&codigo,sizeof(int));
	memcpy(realbuf+sizeof(int),&scriptLength, sizeof(int));		//serializo codigo (de mensaje), tamaño de script y script
	memcpy(realbuf+(sizeof(int)*2), cleanScript, scriptLength);


	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}


	send(sockfd_kernel, realbuf, (sizeof(int)*2)+scriptLength, 0);

	printf("\n\nHilo %d: Script \"%s\" enviado!\n", threadId, scriptName);


	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}


	recv(sockfd_kernel, &respuesta, sizeof(int), 0);

	if(respuesta < 0)
	{
		printf("\n\nHilo %d: No se pudo ejecutar el programa\n", threadId);
		goto end;
	}
	else
	{
		printf("\n\nHilo %d: Se esta ejecutando el programa!\n", threadId);
		recv(sockfd_kernel, &pid, sizeof(int), 0); 		// recibe el ID del proceso que ejecuta el script
	}


	if(closeAllThreads)
	{
		printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
		avisar_desconexion_kernel(sockfd_kernel);
		goto end;
	}


	int messageSize;
	char *messageToPrint = malloc(65); // el mensaje no tiene mas de 64 chars

	while(1)
	{
		recv(sockfd_kernel, &respuesta, sizeof(int), 0); //recibo el codigo que indica si llego al final del programa (del script enviado) o no

		if(respuesta == 5)	// el kernel quiere imprimir algo
		{
			recv(sockfd_kernel, &messageSize, sizeof(int), 0);
			recv(sockfd_kernel, messageToPrint, messageSize, 0);

			printf("\n\n Mensaje del script \"%s\": %s \n\n", scriptName, messageToPrint);
		}

		if(respuesta == 6)	// el kernel indica que llego al fin del script
		{
			printf("\n\n Mensaje del script \"%s\" (PID: %d): Finalizo el programa satisfactoriamente! \n\n", scriptName, pid);
			break;
		}

		if(respuesta == 7) //el kernel aborta el programa
		{
			printf("\n\n La ejecucion del script \"%s\" (PID: %d) ha sido abortada \n\n", scriptName, pid);		//deberia hacer algo mas por aca?
			break;
		}


		if(closeAllThreads)
		{
			printf("\n\nEl hilo %d asignado al script \"%s\" ha sido desconectado!\n", threadId, scriptName);
			avisar_desconexion_kernel(sockfd_kernel);
			goto end;
		}
	}

end:				// puse los goto para evitar repetir esto que sigue varias veces
	free(filePath);
	free(realbuf);
	free(cleanScript);
	close(sockfd_kernel);
	pthread_exit(NULL);
}


void iniciar_programa()
{
	char *fileName = malloc(31);

	printf("Escriba el nombre del script a ejecutar: ");

	if(scanf("%s", fileName) == EOF)	//verifica que no falle el scanf()
	{
		perror("Consola, linea 211, error en scanf: ");
		return;
	}

	int arrayLength = strlen(fileName)+1;
	char fileNamee[arrayLength];		// pongo esto por las dudas de que se haga el free() antes que el hilo use la variable; el array es una variable estatica, se libera sola
	strcpy(fileNamee, fileName);
	fileNamee[arrayLength] = '\0';

	pthread_t script_tret;
	int tret_value = -1;

	if((tret_value = pthread_create(&script_tret, NULL,(void*) script_thread, fileNamee)) != 0)
	{
		perror("Consola, linea 220, error al crear el hilo: ");
		goto end;
	}
	else
	{
		printf("Hilo creado satisfactoriamente\n\n");
		pthread_detach(script_tret);
	}

end:					// puse los goto para evitar repetir esto que sigue varias veces
	free(fileName);
}


void finalizar_programa()
{
	int sockfd_kernel;
	int *pid;
	int codigo = 3;

	sockfd_kernel = handshake_kernel();

	printf("\nIngrese el ID del proceso a terminar: ");

	if(scanf("%d", pid) == EOF)
	{
		perror("Error en linea 317, scanf: ");
		goto end;
	}


	void * buffer = malloc(sizeof(int)*2);

	memcpy(buffer, &codigo, sizeof(int));
	memcpy(buffer+sizeof(int), pid, sizeof(int));

	printf("\nEl proceso sera terminado pronto!");

	send(sockfd_kernel, buffer, sizeof(int)*2, 0);

end:
	free(buffer);
}


void desconectar_consola()
{
	printf("\nLos hilos se desconectaran en breve...");

	closeAllThreads = 1;			// pense en hacerlo con 'pthread_cancel', pero no me dejaba liberar variables dinamicas
									// tambien pense en cerrar todos los hilos desde acá, de alguna forma, teniendo una lista global con los hilos y sus sockets
									// pero al final se me ocurrio esto que era menos engorroso (aunque tuve que llenar la funcion de los hilos de 'if')
}


void print_commands()
{
	printf("\nComandos\n");
	printf("\t init   - Iniciar Programa\n");
	printf("\t end    - Finalizar Programa\n");
	printf("\t dcon   - Desconectar Consola\n");
	printf("\t cls    - Limpiar Mensajes\n");
	printf("\t exit   - Salir\n");
	printf("\nIngrese un comando: ");
}


int main(int argc, char** argv) {

	t_config *config;
	//char *buf = malloc(256);
	//int codigo;
	//int messageLength;

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../Consola/") + strlen(argv[1])+1); //el programa se ejecuta en la carpeta 'Debug'; '../' hace que vaya un directorio arriba -> '../../' va 2 directorios arriba
	strcpy(cfgPath, "../../Consola/");

	config = config_create_from_relative_with_check(argv, cfgPath);

	data_config.ip_kernel = config_get_string_value(config, "IP_KERNEL");
	data_config.puerto_kernel = config_get_string_value(config, "PUERTO_KERNEL");

	printf("IP_KERNEL = %s\n", data_config.ip_kernel);
	printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

	//Nos conectamos
	//sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

	//memset(buf,0,256);
	/*codigo = 2;
	if(send(sockfd_kernel,&codigo,sizeof(int),0)==-1)
		{
			perror("send");
			exit(3);
		}*/

	/*void* codbuf = malloc(sizeof(int)*2);
	codigo =1;
	memcpy(codbuf,&codigo,sizeof(int));
	memcpy(codbuf + sizeof(int),&idProceso, sizeof(int));
	send(sockfd_kernel, codbuf, sizeof(int)*2, 0);
	free(codbuf);*/

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

	char *command = malloc(20);

	print_commands();

	while(1)
	{
		scanf("%s", command);
		
		if((strcmp(command, "init")) == 0) // no es seguro comparar con '=='
		{
			iniciar_programa();
			printf("\nIngrese un comando: ");
		}
		else if((strcmp(command, "end")) == 0)
		{
			finalizar_programa();
			printf("\nIngrese un comando: ");
		}
		else if((strcmp(command, "dcon")) == 0)
		{
			desconectar_consola();
			printf("\nIngrese un comando: ");
		}
		else if((strcmp(command, "cls")) == 0)
		{
			system("clear");
			print_commands();
		}
		else if((strcmp(command, "exit")) == 0)
		{
			desconectar_consola();		// como esta funcion mata los hilos y libera todas sus variables, me aseguro de eso ejecutandola a la salida
			goto end;
		}
		else
		{
			printf("\nComando incorrecto. Ingrese otro comando: ");
			continue;
		}
	}


end:
	config_destroy(config);
	free(cfgPath);
	free(command);
	return 0;
}
