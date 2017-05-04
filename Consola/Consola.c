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
#include <sys/stat.h>
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
		if(line[0] == '#' || line[1] == '#') continue; //elimina las lineas que empiezan con '#' (comentarios); la linea puede tener un ' ' ó '\t', por eso verifico los 2 primeros char de la linea
		strcat(script, line);	//copia el contenido de line desde el ultimo \0 de script (elimina ese \0 y agrega uno al final)
		currentLength += lineLength;
	}
	*scriptSize = currentLength;
	free(line);
}


void *process_script(char *filePath)
{
	FILE *file;
	int scriptLength = 0;
	int codigo = 2;
	struct stat st;
	off_t fileSize;	//uso esto para despues determinar el tamaño de la variable script
					// sabiendo que 1 char = 1 byte, al sacar el nro de bytes del archivo, se cuantos char tiene
					// "off_t" = unsigned int 64

	if ((file = fopen(filePath, "r")) == NULL)
	{
		perror("Consola, linea 124, error de archivo: ");
		exit(1);
	}

	printf("El archivo existe y fue abierto\n");

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


	printf("Script procesado\n");
	printf("Contenido del script a enviar:\n%s", cleanScript);

	void* realbuf = malloc((sizeof(int)*2)+scriptLength+1);

	memcpy(realbuf,&codigo,sizeof(int));
	memcpy(realbuf+sizeof(int),&scriptLength, sizeof(int));		//serializo codigo (de mensaje), tamaño de script y script
	memcpy(realbuf+(sizeof(int)*2), cleanScript, scriptLength);

	send(sockfd_kernel, realbuf, (sizeof(int)*2)+scriptLength, 0);

	printf("\nScript enviado!\n");

	free(realbuf);
	free(cleanScript);

	pthread_exit(NULL);
}


void iniciar_programa()
{
	char *filePath = malloc(50); // no creo que la ruta sea taaan larga como para superar 50 caracteres
	strcpy(filePath, "../../Files/Scripts/");

	char *fileName = malloc(20);

	printf("Escriba la ruta del script a ejecutar: ");

	if(scanf("%s", fileName) == EOF)	// puse esto porque rompe las bolas al compilar... y por seguridad
	{
		perror("Consola, linea 211, error en scanf: ");
		exit(1);
	}

	strcat(filePath, fileName);

	pthread_t script_tret;
	int tret_value = -1;

	if((tret_value = pthread_create(&script_tret, NULL,(void*) process_script, filePath)) != 0)
	{
		perror("Consola, linea 220, error al crear el hilo: ");
		exit(1);
	}
	else
	{
		printf("Hilo creado satisfactoriamente\n\n");
	}

	pthread_join(script_tret,0);
	free(filePath);
	free(fileName);

	printf("\nIngrese un comando: ");
}


void finalizar_programa()
{
	//le tengo que madnar un mensaje al kernel serializado con un codigo y un pid	

	printf("Funcionalidad no implementada!\n\n");
	printf("Ingrese un comando: ");
}


void desconectar_consola()
{
	//Cada hilo maneja una conexion, asi que tendria que tener una lista de los hilos de este proceso para ir matandolos

	printf("Funcionalidad no implementada!\n\n");
	printf("Ingrese un comando: ");
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
	consola_config data_config;
	//char *buf = malloc(256);
	int codigo;
	int idProceso = 2;
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
	sockfd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

	//memset(buf,0,256);
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

	char *command = malloc(20);

	print_commands();

	while(1)
	{
		scanf("%s", command);
		
		if((strcmp(command, "init")) == 0) // si no le tiro el strcmp(), el compilador tira advertencia
		{
			iniciar_programa();
		}
		else if((strcmp(command, "end")) == 0)
		{
			finalizar_programa(); //falta implementar
		}
		else if((strcmp(command, "dcon")) == 0)
		{
			desconectar_consola(); //falta implementar
		}
		else if((strcmp(command, "cls")) == 0)
		{
			system("clear");
			print_commands();
		}
		else if((strcmp(command, "exit")) == 0)
		{
			exit(0);	//EXIT_SUCCSESS
		}
		else
		{
			printf("Comando incorrecto. Ingrese otro comando: ");
			continue;
		}
	}



	config_destroy(config);
	free(cfgPath);
	free(command);
	return 0;
}
