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
#include <time.h>
#include <netdb.h>
#include <pthread.h>
#include "../config_shortcuts/config_shortcuts.h"
#include <signal.h>
//#include "../config_shortcuts/config_shortcuts.c"


bool closeAllThreads = false;
int idProceso = 2;
int threadCounter = 0;	//nro de hilos aparte del principal
t_list* thread_list;

pthread_mutex_t mutex;
pthread_mutex_t thlist_mutex;

typedef struct{
	int pid;
	int thread;
}hilo_t;

typedef struct{
	char* script;
	int threadID;
}thread_setup;

consola_config data_config;


//Pasas la ip y el puerto para la conexion y devuelve el fd del servidor correspondiente
int get_fd_server(char* ip, char* puerto, char *thread){

	struct addrinfo hints;
	struct addrinfo *servinfo, *p;
	int sockfd, result;

	//Vaciamos hints para usarlo en la funcion getaddrinfo() y le setteamos el tipo de socket y la familia
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((result = getaddrinfo(ip, puerto, &hints, &servinfo)) != 0)
	{
		printf("\n\nHilo %s - ", thread);
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
		return 1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			printf("\n\nHilo %s - error socket", thread);
			perror("");
			continue;
		}
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			printf("\n\nHilo %s - error connect", thread);
			perror("");
			continue;
		}
		break;
	}
	if (p == NULL)
	{
		fprintf(stderr, "\n\nHilo %s - error al conectar\n", thread);
		return 2;
	}

	freeaddrinfo(servinfo);

	return sockfd;
}


int handshake_kernel(char* thread)
{
	int codigo = 1;
	int sockfd_kernel;

	//Handshake

	sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel, thread);
	if(sockfd_kernel == 2)
	{
		return -1;
	}

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
	uint32_t codigo = 9;
	send(socket, &codigo, sizeof(uint32_t), 0);
}


void printData(struct tm *start, struct tm *end, int printCount, int pid)
{
	char date1[80];
	char date2[80];

	strftime(date1, 80, "%d/%m/%Y %T", start);
	strftime(date2, 80, "%d/%m/%Y %T", end);

	int startTime = ((start->tm_hour)*3600) + ((start->tm_min)*60) + (start->tm_sec);
	int endTime = ((end->tm_hour)*3600) + ((end->tm_min)*60) + (end->tm_sec);

	printf("Informacion sobre el programa %d:", pid);
	printf("\n\n\tLa fecha de inicio del programa es: %s", date1);
	printf("\n\tLa fecha de finalizacion del programa es: %s", date2);
	printf("\n\tLa cantidad de impresiones por pantalla es: %d", printCount);
	printf("\n\tEl tiempo total de ejecucion del programa en segundos es: %d\n", (endTime-startTime));
}

void remover_de_lista(hilo_t* hilo)
{
	int i = 0, dimension = list_size(thread_list);
	bool encontrado = false;

	pthread_mutex_lock(&thlist_mutex);

	while(i < dimension && !encontrado)
	{
		hilo_t* aux = list_get(thread_list,i);
		if(aux->thread == hilo->thread)
		{
			list_remove(thread_list,i);
			encontrado = true;
		}
		i++;
	}
	pthread_mutex_unlock(&thlist_mutex);
}

void script_thread(thread_setup* ts)
{
	struct tm *startTime, *endTime;
	time_t t;
	int codigo;
	int sockfd_kernel;
	FILE *file;
	int scriptLength = 0;
	struct stat st;
	off_t fileSize;	//uso esto para despues determinar el tamaño de la variable script
					// sabiendo que 1 char = 1 byte, al sacar el nro de bytes del archivo, se cuantos char tiene
					// "off_t" = unsigned int 64

	int printCounter = 0;		//para controlar cuantas impresiones hizo el programa a ejecutar

	char id_string[15];
	sprintf(id_string,"%d",ts->threadID);
	sockfd_kernel = handshake_kernel(id_string);

	if(sockfd_kernel == -1)
	{
		printf("\nATENCION: No hay conexion con el kernel. El hilo se anulará. Vuelva a intentarlo luego de solucionar el problema\n");
		pthread_exit(-1);
	}


	//Manejo el script

	char *filePath = malloc(51); // dudo que la ruta sea taaan larga como para superar 50 caracteres
	strcpy(filePath, "../../Files/Scripts/"); // aca hay 20 chars, asi que el nombre del archivo puede contener 30 mas

	strcat(filePath, ts->script);
	strcat(filePath, ".ansisop");  	// corri estas partes de la funcion "iniciar_programa" a acá para poder indicar de que script viene el msj que recibo de kernel
									// asi no se me mezcla cuando haya varios mensajes cayendo de distintos hilos


	if ((file = fopen(filePath, "r")) == NULL)
	{
		printf("\nHilo %d, error al abrir el archivo",ts->threadID);
		perror("");
		printf("\nIntente abrir el script nuevamente\n");
		free(filePath);
		pthread_exit(-2);
	}

	printf("\nHilo %d: \n\t El archivo existe y fue abierto\n", ts->threadID);

	stat(filePath, &st); //setea la info del archivo en la estructura 'st'
	fileSize = st.st_size;

	//char *script = clean_script(file, & scriptLength);  //con esto creo otra variable que come memoria y no se puede liberar... genera memory leaks
														  //en la funcion se hacia el malloc() para el script, pero asi nunca lo podia liberar


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


	printf("\nHilo %d: Script \"%s\" procesado\n", ts->threadID, ts->script);
	printf("\nHilo %d:\n\t Contenido del script a enviar:\n\n%s\n", ts->threadID, cleanScript);

	void *realbuf = malloc((sizeof(int)*2)+scriptLength+1);

	codigo = 2;
	int respuesta = 0;

	memcpy(realbuf,&codigo,sizeof(int));
	memcpy(realbuf+sizeof(int),&scriptLength, sizeof(int));		//serializo codigo (de mensaje), tamaño de script y script
	memcpy(realbuf+(sizeof(int)*2), cleanScript, scriptLength);


	if(send(sockfd_kernel, realbuf, (sizeof(int)*2)+scriptLength, 0) == -1)
	{
		printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		free(filePath);
		free(realbuf);
		free(cleanScript);
		pthread_exit(-1);
	}

	printf("\nHilo %d: Script \"%s\" enviado!\n", ts->threadID, ts->script);


	if(recv(sockfd_kernel, &respuesta, sizeof(int), 0) == 0)		// devuelve -1 si hay error y 0 si el socket se desconecta
	{
		printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		free(filePath);
		free(realbuf);
		free(cleanScript);
		pthread_exit(-1);
	}

	if(respuesta <= 0)
	{
		printf("\nHilo %d: No se pudo ejecutar el programa\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		free(filePath);
		free(realbuf);
		free(cleanScript);
		pthread_exit(-1);
	}

	printf("\nHilo %d: Se esta ejecutando el programa %d\n", ts->threadID, respuesta);

	hilo_t* esteHilo = malloc(sizeof(hilo_t));
	esteHilo->thread = ts->threadID;
	esteHilo->pid = respuesta;

	pthread_mutex_lock(&thlist_mutex);
	list_add(thread_list,esteHilo);
	pthread_mutex_unlock(&thlist_mutex);

	t = time(NULL);
	startTime = localtime(&t);

	/*
	if(recv(sockfd_kernel, &pid, sizeof(int), 0) == 0) 		// el recv recibe el ID del proceso que ejecuta el script
	{
		printf("\n\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		free(filePath);
		free(realbuf);
		free(cleanScript);
		exit(-1);
	}

	printf("\n\nHilo %d - El id del proceso es: %d\n", ts->threadID, pid);
	*/


	int messageSize, respuesta2 = 0;
	char *messageToPrint = malloc(65); // el mensaje no tiene mas de 64 chars

	//int counter = 0;		//para probar que mate el hilo

	while(!closeAllThreads)
	{
		if(recv(sockfd_kernel, &respuesta2, sizeof(int), 0) == 0) 	//recibe el codigo que indica si llego al final del programa (del script enviado) o no
		{
			printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
			break;
		}

		if(respuesta2 == 5)	// el kernel quiere imprimir algo
		{
			if(recv(sockfd_kernel, &messageSize, sizeof(int), 0) == 0)
			{
				printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
				break;
			}
			if(recv(sockfd_kernel, messageToPrint, messageSize, 0) == 0)
			{
				printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
				break;
			}

			printCounter++;

			printf("\nnMensaje del script \"%s\" (PID: %d): %s\n", ts->script, esteHilo->pid, messageToPrint);
		}

		if(respuesta2 == 6)	// el kernel indica que llego al fin del script
		{
			printf("\nMensaje del script \"%s\" (PID: %d): Finalizo el programa satisfactoriamente!\n", ts->script, esteHilo->pid);
			endTime = time(NULL);
			printData(startTime, endTime, printCounter, esteHilo->pid);
			break;
		}

		if(respuesta2 == 7) //el kernel aborta el programa
		{
			printf("\nLa ejecucion del script \"%s\" (PID: %d) ha sido abortada\n", ts->script, esteHilo->pid);
			break;
		}
		/*
		sleep(2);
		printf("\n\nHilo %d: vuelta %d del loop\n", esteHilo->thread, counter);		//para probar que mate el hilo
		counter++;*/
	}

	if(closeAllThreads)
	{
		printf("\nEl hilo %d asignado al script \"%s\" (PID: %d) ha sido desconectado!\n", ts->threadID, ts->script, esteHilo->pid);
		avisar_desconexion_kernel(sockfd_kernel);
	}

	t = time(NULL);
	endTime = localtime(&t);

	printData(startTime, endTime, printCounter, esteHilo->pid);

	free(ts->script);
	remover_de_lista(esteHilo);
	free(esteHilo);
	free(ts);
	free(filePath);
	free(realbuf);
	free(cleanScript);
	printf("\nEl hilo %d ha pasado a mejor vida\n", esteHilo->thread);
	close(sockfd_kernel);
}


void iniciar_programa()
{
	char *fileName = malloc(31);

	//printf("Escriba el nombre del script a ejecutar: "); 	// leer 1er comentario en funcion "finalizar_programa"

	if(scanf("%s", fileName) == EOF)	//verifica que no falle el scanf()
	{
		perror("\nConsola, linea 211, error en scanf");
		return;
	}

	int arrayLength = strlen(fileName)+1;
	char fileNamee[arrayLength];		// pongo esto por las dudas de que se haga el free() antes que el hilo use la variable; el array es una variable estatica, se libera sola
	strcpy(fileNamee, fileName);
	fileNamee[arrayLength] = '\0';

	pthread_mutex_lock(&mutex);
	pthread_t script_tret = ++threadCounter;
	int tret_value = -1;

	thread_setup* ts = malloc(sizeof(thread_setup));
	ts->script = fileName;
	ts->threadID = threadCounter;
	pthread_mutex_unlock(&mutex);

	if((tret_value = pthread_create(&script_tret, NULL,(void*) script_thread, ts)) != 0)
	{
		perror("\nConsola, linea 220, error al crear el hilo");
	}
	else
	{
		printf("\nHilo creado satisfactoriamente\n");
		pthread_detach(script_tret);
	}
}


void finalizar_programa()
{
	int sockfd_kernel;
	//uint32_t respuesta;
	uint32_t *pid = malloc(sizeof(uint32_t));
	uint32_t codigo = 3;

	sockfd_kernel = handshake_kernel("main");

	//printf("\nIngrese el ID del proceso a terminar: ");  //saco esto porque antes de llamar a esta funcion puedo tirar "end 4" (por tanto, no es necesario pedir el id);
														   //la funcion antes de llamar a esto lee el "end" (en su scanf), y el scanf en esta funcion lee el "4"

	if(scanf("%d", pid) == EOF)
	{
		perror("\nError en linea 317, scanf");
	}


	void *buffer = malloc(sizeof(uint32_t)*2);

	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), pid, sizeof(uint32_t));

	printf("\nEl proceso sera terminado!\n");

	if(send(sockfd_kernel, buffer, sizeof(uint32_t)*2, 0) == -1)
		printf("\nEl kernel esta desconectado, no se pudo finalizar el programa (o ya ha sido finalizado al cerrar el kernel)\n");

	free(buffer);
	free(pid);
	close(sockfd_kernel);
}


void desconectar_consola()
{
	printf("\nSe desconectaran los hilos\n");

	closeAllThreads = true;			// pense en hacerlo con 'pthread_cancel', pero no me dejaba liberar variables dinamicas
									// tambien pense en cerrar todos los hilos desde acá, de alguna forma, teniendo una lista global con los hilos y sus sockets
									// pero al final se me ocurrio esto que era menos engorroso (aunque tuve que llenar la funcion de los hilos de 'if')
}


void mostrar_hilos()
{
	int i = 0, dimension = list_size(thread_list);

	if(dimension > 0)
	{
		while(i < dimension)
		{
			hilo_t* aux = list_get(thread_list,i);
			printf("\nHilo: %d ejecutando proceso: %d\n",aux->thread,aux->pid);
			i++;
		}
	}
	else printf("\nNo hay hilos abiertos\n");
}


void print_commands()
{
	printf("\nComandos\n");
	printf("\t init   - Iniciar Programa\n");
	printf("\t end    - Finalizar Programa\n");
	printf("\t dcon   - Desconectar Consola\n");
	printf("\t cls    - Limpiar Mensajes\n");
	printf("\t thlist  - Mostrar hilos abiertos\n");
	printf("\t thnum  - Mostrar cantidad de hilos abiertos\n");
	printf("\t exit   - Salir\n");
	printf("\nIngrese un comando: ");
}


int main(int argc, char** argv)
{
	thread_list = list_create();
	closeAllThreads = false;

	t_config *config;

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../Consola/") + strlen(argv[1])+1); //el programa se ejecuta en la carpeta 'Debug'; '../' hace que vaya un directorio arriba -> '../../' va 2 directorios arriba
	strcpy(cfgPath, "../../Consola/");

	config = config_create_from_relative_with_check(argv, cfgPath);

	data_config.ip_kernel = config_get_string_value(config, "IP_KERNEL");
	data_config.puerto_kernel = config_get_string_value(config, "PUERTO_KERNEL");

	printf("IP_KERNEL = %s\n", data_config.ip_kernel);
	printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

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
			closeAllThreads = 0;
			desconectar_consola();
			printf("\nIngrese un comando: ");
		}
		else if((strcmp(command, "cls")) == 0)
		{
			system("clear");
			print_commands();
		}
		else if((strcmp(command, "thlist")) == 0)
		{
			pthread_mutex_lock(&thlist_mutex);
			mostrar_hilos();
			pthread_mutex_unlock(&thlist_mutex);
		}
		else if((strcmp(command, "thnum")) == 0)
		{
			printf("\nCantidad de hilos abiertos: %d", threadCounter);
			printf("\nIngrese un comando: ");
		}
		else if((strcmp(command, "exit")) == 0)
		{
			if(list_size(thread_list) == 0)
			{
				printf("\nNo hay hilos para cerrar\n");
				break;
			}

			desconectar_consola();		// como esta funcion mata los hilos y libera todas sus variables, me aseguro de eso ejecutandola a la salida
			printf("\nSaliendo del programa...\n");
			break;
		}
		else
		{
			printf("\nComando incorrecto. Ingrese otro comando: ");
			continue;
		}
	}


	config_destroy(config);
	free(cfgPath);
	free(command);
	return 0;
}
