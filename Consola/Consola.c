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
#include <commons/log.h>
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


bool closeAllThreads;
int idProceso = 2;
t_list* thread_list;
t_log *messagesLog;

pthread_mutex_t mutex;
pthread_mutex_t thlist_mutex;
pthread_mutex_t finalizador_mutex;

typedef struct{
	int hours;
	int minutes;
	int seconds;
	char *dateTime;
}dateTime;

typedef struct{
	int pid;
	int thread;
	int socket_kernel;
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
		log_error(messagesLog, "Hilo %s - Error getaddrinfo: %s\n", thread);
		printf("Hilo %s: Error de getaddrinfo, linea 65 - %s\n", thread, gai_strerror(result));
		return 1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			printf("Hilo %s: Error de socket, linea 74 - ", thread);
			perror("");
			log_error(messagesLog, "Hilo %s - Error socket\n", thread);
			continue;
		}
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			log_error(messagesLog, "Hilo %s - Error connect\n", thread);
			printf("Hilo %s: Error de connect, linea 83 - ", thread);
			perror("");
			continue;
		}
		break;
	}
	if (p == NULL)
	{
		printf("Hilo %s: Error al conectar\n", thread);
		log_error(messagesLog, "Hilo %s: Error al conectar\n", thread);
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


void printData(dateTime *start, dateTime *end, int printCount, int pid)
{
	int startTimeSeconds = ((start->hours)*3600) + ((start->minutes)*60) + (start->seconds);
	int endTimeSeconds = ((end->hours)*3600) + ((end->minutes)*60) + (end->seconds);
	int elapsedTime = endTimeSeconds - startTimeSeconds;

	printf("Imprimiendo datos de inicio y fin del proceso %d en el log\n", pid);

	log_info(messagesLog, "Informacion sobre el programa %d:", pid);
	log_info(messagesLog, "\n\tLa fecha de inicio del programa es: %s", start->dateTime);
	log_info(messagesLog, "\n\tLa fecha de finalizacion del programa es: %s", end->dateTime);
	log_info(messagesLog, "\n\tLa cantidad de impresiones por pantalla es: %d", printCount);
	log_info(messagesLog, "\n\tEl tiempo total de ejecucion del programa en segundos es: %d\n", elapsedTime);

	free(start->dateTime);
	free(end->dateTime);
	free(start);
	free(end);
}

void remover_de_lista(hilo_t* hilo)
{
	pthread_mutex_lock(&thlist_mutex);
	int i = 0, dimension = list_size(thread_list);
	pthread_mutex_unlock(&thlist_mutex);
	bool encontrado = false;
	while(i < dimension && !encontrado)
	{
		pthread_mutex_lock(&thlist_mutex);
		hilo_t* aux = list_get(thread_list,i);
		pthread_mutex_unlock(&thlist_mutex);
		if(aux->thread == hilo->thread)
		{
			pthread_mutex_lock(&thlist_mutex);
			//free(aux->startDateTime);
			list_remove(thread_list,i);
			pthread_mutex_unlock(&thlist_mutex);
			encontrado = true;
		}
		i++;
	}
}

dateTime* getTime()
{
	time_t t1 = time(NULL);
	struct tm* local1 = localtime(&t1);
	char *date1 = malloc(35);
	sprintf(date1, "%d/%d/%d  %d:%d:%d", local1->tm_mday, local1->tm_mon + 1, local1->tm_year + 1900, local1->tm_hour, local1->tm_min, local1->tm_sec);

	dateTime* time = malloc(sizeof(dateTime));
	time->hours = local1->tm_hour;
	time->minutes = local1->tm_min;
	time->seconds = local1->tm_sec;
	time->dateTime = date1;

	return time;
}

void script_thread(thread_setup* ts)
{
	int codigo;
	int sockfd_kernel;
	FILE *file;
	int scriptLength = 0;
	struct stat st;
	off_t fileSize;

	char id_string[15];
	sprintf(id_string,"%d",ts->threadID);
	sockfd_kernel = handshake_kernel(id_string);

//	setsockopt(sockfd_kernel, SOL_SOCKET, NULL, NULL, NULL);  //Se usaba esto para ponerle timer a los recv, asi no se colgaban cuando trataba de matar hilos con la variable global
															 //Convierte los recv y send en no bloqueantes, puede llevar a problemas si no se esperaba eso

	if(sockfd_kernel == -1)
	{
		log_error(messagesLog, "El hilo %d se anulara debido a que no hay conexion con el kernel\n", ts->threadID);
		printf("ATENCION: No hay conexion con el kernel. El hilo %d se anulara. Vuelva a intentarlo luego de solucionar el problema\n", ts->threadID);
		pthread_exit(NULL);
	}


	//Manejo el script

	char *filePath = malloc(51); // dudo que la ruta sea taaan larga como para superar 50 caracteres
	strcpy(filePath, "../../Files/Scripts/"); // aca hay 20 chars, asi que el nombre del archivo puede contener 30 mas

	strcat(filePath, ts->script);
	strcat(filePath, ".ansisop");  	// corri estas partes de la funcion "iniciar_programa" a acá para poder indicar de que script viene el msj que recibo de kernel
									// asi no se me mezcla cuando haya varios mensajes cayendo de distintos hilos


	if ((file = fopen(filePath, "r")) == NULL)
	{
		printf("Hilo %d, error al abrir el archivo\n",ts->threadID);
		log_error(messagesLog, "Error de fopen, linea 265\n");
		printf("Intente abrir el script nuevamente\n");
		free(filePath);
		pthread_exit(NULL);
	}

	log_info(messagesLog, "Hilo %d: El archivo existe y fue abierto\n", ts->threadID);

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


	log_info(messagesLog, "Hilo %d: Script \"%s\" procesado\n", ts->threadID, ts->script);
	log_info(messagesLog, "Hilo %d:\n\t Contenido del script a enviar:\n\n%s\n", ts->threadID, cleanScript);

	void *realbuf = malloc((sizeof(int)*2)+scriptLength+1);

	codigo = 2;
	int respuesta = 0;

	memcpy(realbuf,&codigo,sizeof(int));
	memcpy(realbuf+sizeof(int),&scriptLength, sizeof(int));		//serializo codigo (de mensaje), tamaño de script y script
	memcpy(realbuf+(sizeof(int)*2), cleanScript, scriptLength);

	free(cleanScript);
	free(filePath);


	if(send(sockfd_kernel, realbuf, (sizeof(int)*2)+scriptLength, 0) == -1)
	{
		printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
		log_error(messagesLog, "Se terminara el hilo %d debido a que el kernel se encuentra desconectado\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		pthread_exit(NULL);
	}

	free(realbuf);

	printf("\nHilo %d: Script \"%s\" enviado!\n", ts->threadID, ts->script);


	if(recv(sockfd_kernel, &respuesta, sizeof(int), 0) == 0)		// devuelve -1 si hay error y 0 si el socket se desconecta
	{
		printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", ts->threadID);
		log_error(messagesLog, "Se terminara el hilo %d debido a que el kernel se encuentra desconectado\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		pthread_exit(NULL);
	}

	if(respuesta <= 0)
	{
		log_info(messagesLog, "No se pudo ejecutar un programa para el hilo %d\n", ts->threadID);
		printf("\nHilo %d: No se pudo ejecutar el programa\n", ts->threadID);
		close(sockfd_kernel);
		free(ts->script);
		pthread_exit(NULL);
	}

	log_info(messagesLog, "Hilo %d: Se esta ejecutando el programa %d\n", ts->threadID, respuesta);

	hilo_t* esteHilo = malloc(sizeof(hilo_t));
	esteHilo->thread = ts->threadID;
	esteHilo->pid = respuesta;
	esteHilo->socket_kernel = sockfd_kernel;
	//esteHilo->printCount = 0;*/


	pthread_mutex_lock(&thlist_mutex);
	list_add(thread_list,esteHilo);
	pthread_mutex_unlock(&thlist_mutex);

	//Defino la estructura para tomar el tiempo de inicio del programa
	time_t t1 = time(NULL);
	struct tm* local1 = localtime(&t1);
	char *date1 = malloc(35);
	sprintf(date1, "%d/%d/%d  %d:%d:%d", local1->tm_mday, local1->tm_mon + 1, local1->tm_year + 1900, local1->tm_hour, local1->tm_min, local1->tm_sec);

	dateTime* startTime = malloc(sizeof(dateTime));
	startTime->hours = local1->tm_hour;
	startTime->minutes = local1->tm_min;
	startTime->seconds = local1->tm_sec;
	startTime->dateTime = date1;


	int messageSize = 0, respuesta2 = 0, printCounter = 0;

	//int counter = 0;		//para probar que mate el hilo

	while(!closeAllThreads)
	{
		int bytes_recv = recv(sockfd_kernel, &respuesta2, sizeof(int), 0);
		if(bytes_recv == 0) 	//recibe el codigo que indica si llego al final del programa (del script enviado) o no
		{
			printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", esteHilo->thread);
			log_error(messagesLog, "Se terminara el hilo %d debido a que el kernel se encuentra desconectado\n", esteHilo->thread);
			remover_de_lista(esteHilo);
			break;
		}

		if(respuesta2 == 5)	// el kernel quiere imprimir algo
		{
			if(recv(sockfd_kernel, &messageSize, sizeof(int), 0) == 0) //esto pasa si se desconectan kernel o consola, o si le hago shutdown al socket
			{
				printf("\nHilo %d: este hilo fue desconectado del kernel, el hilo sera terminado\n", esteHilo->thread);
				log_error(messagesLog, "Se terminara el hilo %d debido a que el kernel se encuentra desconectado\n", ts->threadID);
				remover_de_lista(esteHilo);
				break;
			}

			char *messageToPrint = malloc(messageSize); // el mensaje no tiene mas de 64 chars

			if(recv(sockfd_kernel, messageToPrint, messageSize, 0) == 0)
			{
				printf("\nHilo %d: el kernel esta desconectado, el hilo sera terminado\n", esteHilo->thread);
				log_error(messagesLog, "Se terminara el hilo %d debido a que el kernel se encuentra desconectado\n", ts->threadID);
				remover_de_lista(esteHilo);
				break;
			}

			printCounter++;
			printf("Mensaje del script \"%s\" (PID: %d): %s\n", ts->script, esteHilo->pid, messageToPrint);

			free(messageToPrint);
		}

		if(respuesta2 == 6)	// el kernel indica que llego al fin del script
		{
			log_info(messagesLog, "Mensaje del script \"%s\" (PID: %d): Finalizo el programa satisfactoriamente!\n", ts->script, esteHilo->pid);
			remover_de_lista(esteHilo);
			break;
		}

		if(respuesta2 == 7) //el kernel aborta el programa
		{
			log_info(messagesLog, "La ejecucion del script \"%s\" (PID: %d) ha sido abortada\n", ts->script, esteHilo->pid);
			remover_de_lista(esteHilo);
			break;
		}
		/*
		sleep(2);
		printf("\n\nHilo %d: vuelta %d del loop\n", esteHilo->thread, counter);		//para probar que mate el hilo
		counter++;*/
	}

	if(closeAllThreads)
	{
		log_info(messagesLog, "El hilo que ejecutaba el script %s (PID: %d) ha sido terminado por desconexion de la consola\n", ts->script, esteHilo->pid);
		remover_de_lista(esteHilo);
	}

	//Seteo estructura para tiempo de finalizacion del programa
	time_t t2 = time(NULL);
	struct tm* local2 = localtime(&t2);
	char *date2 = malloc(35);
	sprintf(date2, "%d/%d/%d  %d:%d:%d", local2->tm_mday, local2->tm_mon + 1, local2->tm_year + 1900, local2->tm_hour, local2->tm_min, local2->tm_sec);

	dateTime* endTime = malloc(sizeof(dateTime));
	endTime->hours = local2->tm_hour;
	endTime->minutes = local2->tm_min;
	endTime->seconds = local2->tm_sec;
	endTime->dateTime = date2;

	//Imprimo los tiempos de ejecucion del programa
	printData(startTime, endTime, printCounter, esteHilo->pid);

	//Libero variables
	log_info(messagesLog, "El hilo %d ha pasado a mejor vida\n", esteHilo->thread);
	free(ts->script);
	free(ts);
	free(esteHilo);
	close(sockfd_kernel);
	pthread_exit(NULL);
}


void iniciar_programa()
{
	char *fileName = malloc(31);

	//printf("Escriba el nombre del script a ejecutar: "); 	// leer 1er comentario en funcion "finalizar_programa"

	if(scanf("%s", fileName) == EOF)	//verifica que no falle el scanf()
	{
		log_error(messagesLog, "Se ha producido un error de scanf\n");
		printf("Error de scanf, linea 473\n");
		return;
	}

	int arrayLength = strlen(fileName)+1;
	char fileNamee[arrayLength];		// pongo esto por las dudas de que se haga el free() antes que el hilo use la variable; el array es una variable estatica, se libera sola
	strcpy(fileNamee, fileName);
	fileNamee[arrayLength] = '\0';

	pthread_mutex_lock(&mutex);
	pthread_t script_tret = list_size(thread_list) + 1;
	int tret_value = -1;

	thread_setup* ts = malloc(sizeof(thread_setup));
	ts->script=fileName;
	ts->threadID = (int)script_tret;
	pthread_mutex_unlock(&mutex);

	//Genero un atributo de pthread para que lo cree como "detached"
	pthread_attr_t attribute;
	pthread_attr_init(&attribute);
	pthread_attr_setdetachstate(&attribute, PTHREAD_CREATE_DETACHED);

	if((tret_value = pthread_create(&script_tret, &attribute, (void*) script_thread, ts)) != 0)
	{
		log_error(messagesLog, "Se ha producido un error al crear un hilo\n");
		printf("Error en pthread_create, linea 490\n");
	}
	else
	{
		log_info(messagesLog, "Hilo creado satisfactoriamente\n");
		pthread_detach(script_tret);
	}

	pthread_attr_destroy(&attribute);
}


void finalizar_programa(int pid)
{
	printf("Se finalizara el proceso seleccionado\n");

	pthread_mutex_lock(&finalizador_mutex);
	int sockfd_kernel;
	uint32_t respuesta;
	uint32_t codigo_finalizacion = 3;

	sockfd_kernel = handshake_kernel("main");

	void *buffer = malloc(sizeof(uint32_t)*2);

	memcpy(buffer, &codigo_finalizacion, sizeof(uint32_t));

	memcpy(buffer+sizeof(uint32_t), &pid, sizeof(uint32_t));

	if(send(sockfd_kernel, buffer, sizeof(uint32_t)*2, 0) == -1)
		log_error(messagesLog, "El kernel esta desconectado, no se pudo finalizar el programa (o ya ha sido finalizado al cerrar el kernel)\n");

	recv(sockfd_kernel,&respuesta, sizeof(uint32_t),0);

	if(respuesta == 0)
		log_info(messagesLog, "El proceso seleccionado no ha sido creado o ya ha sido borrado\n");

	free(buffer);
	close(sockfd_kernel);
	pthread_mutex_unlock(&finalizador_mutex);
}


void desconectar_consola()
{
	//Documentacion de ideas de desconexion (dejarlo, es info util)
	//1- Desconectar por variable global 'closeAllThreads' -> un hilo podia colgarse en un recv y nunca morir
	//2- Guardar algunas variables dinamicas de cada hilo en la estructura que va en la lista de hilos, con tal de liberar esas variables por fuera y matarlo -> nunca se pudo hacer porque rompian 'pthread_cancel' y 'pthread_kill'
	//3- Idea final, guardar el socket de cada hilo en la estructura que va en la lista y hacer shutdown de cada socket -> obliga a salir de los recv

	printf("Se desconectaron los hilos\n");
	log_info(messagesLog, "Se desconectaron los hilos\n");

	int i = 0, dimension = list_size(thread_list);

	if(dimension > 0)
	{
		//Busco la estructura de cada hilo y mato el socket de cada uno
		while(i < dimension)
		{
			hilo_t* aux = list_get(thread_list,i);
			shutdown(aux->socket_kernel, SHUT_RDWR); //SHUT_RDWR evita que se puedan hacer send y recv con ese socket
													 //Si el shutdown falla es porque el socket es invalido o no esta conectado; eso es lo que busco, asi que no miro errores
			log_info(messagesLog, "Se desconecto el hilo: %d que ejecutaba el proceso: %d\n", aux->thread, aux->pid);

			i++;
		}
	}
	else log_info(messagesLog, "No hay hilos abiertos para desconectar\n");

	/*Dejo esto por las dudas
	 *
	 * closeAllThreads = true;
	 * sleep(2);
	 * closeAllThreads = false;	 *
	 */

}


void mostrar_hilos()
{
	printf("Imprimiendo en el log los hilos abiertos\n");
	int i = 0, dimension = list_size(thread_list);

	if(dimension > 0)
	{
		while(i < dimension)
		{
			hilo_t* aux = list_get(thread_list,i);
			log_info(messagesLog, "Hilo: %d ejecutando proceso: %d\n",aux->thread,aux->pid);
			i++;
		}
	}
	else log_info(messagesLog, "No hay hilos abiertos para mostrar\n");
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

	t_config *config;

	// Se fija si le pasaron los argumentos correctos al main

	checkArguments(argc);
	/*char *cfgPath = malloc(sizeof("../../Consola/") + strlen(argv[1])+1); //el programa se ejecuta en la carpeta 'Debug'; '../' hace que vaya un directorio arriba -> '../../' va 2 directorios arriba
	strcpy(cfgPath, "../../Consola/");

	//Cargo archivo config e imprimo datos
	config = config_create_from_relative_with_check(argv, cfgPath);*/

	config = config_create(argv[1]);

	data_config.ip_kernel = config_get_string_value(config, "IP_KERNEL");
	data_config.puerto_kernel = config_get_string_value(config, "PUERTO_KERNEL");

	printf("IP_KERNEL = %s\n", data_config.ip_kernel);
	printf("PUERTO_KERNEL = %s\n", data_config.puerto_kernel);

	//Creo logs (Los logs pueden mostrar por pantalla, asi que reemplazo los printf por esto)
	messagesLog = log_create("../../Files/Logs/ConsoleMessages.log", "Consola", false, LOG_LEVEL_INFO);	//false para que no muestre por pantalla los msj
	log_info(messagesLog, "\n\n////////////////////////////////////////////////\n\n");

	char *command = malloc(20);

	//Muestro lista de comandos
	print_commands();
	
	closeAllThreads = false;

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
			int* pid = malloc(sizeof(uint32_t));
			if(scanf("%d", pid) == EOF)
			{
				log_info(messagesLog, "Se ha producido un error en un scanf\n");
				printf("Error en scanf, linea 660\n");
			}
			finalizar_programa(*pid);
			free(pid);
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
		else if((strcmp(command, "thlist")) == 0)
		{
			pthread_mutex_lock(&thlist_mutex);
			mostrar_hilos();
			pthread_mutex_unlock(&thlist_mutex);
		}
		else if((strcmp(command, "thnum")) == 0)
		{
			log_info(messagesLog, "Cantidad de hilos abiertos: %d\n", list_size(thread_list));
			printf("\nIngrese un comando: ");
		}
		else if((strcmp(command, "exit")) == 0)
		{
			if(list_size(thread_list) == 0)
			{
				printf("No hay hilos para cerrar\n");
				break;
			}

			desconectar_consola();		// como esta funcion mata los hilos y libera todas sus variables, me aseguro de eso ejecutandola a la salida
			printf("Saliendo del programa...\n");
			break;
		}
		else
		{
			printf("\nComando incorrecto. Ingrese otro comando: ");
			continue;
		}
	}


	log_destroy(messagesLog);
	config_destroy(config);
	//free(cfgPath);
	free(command);
	return 0;
}
