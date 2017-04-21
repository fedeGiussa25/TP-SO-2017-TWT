/*
 ============================================================================
 Name        : kernelproto.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"

pthread_mutex_t mutex_fd_cpus;
pthread_mutex_t mutex_fd_consolas;

// Structs de conexiones
typedef struct{
	int sock_fd;
}cpu_conexion;

typedef struct{
	int sock_fd;
}consola_conexion;

// STRUCTS DE PCB - TAL VEZ DEBERIAMOS PONERLOS EN UN .h

typedef struct{
	int offset;
	int size;
}code_index_line;

typedef struct{
	int page;
	int offset;
	int size;
}pagoffsize;

typedef struct{
	char* id;
	int page;
	int offset;
	int size;
}idpagoffsize;

typedef struct{
	idpagoffsize args;
	idpagoffsize vars;
	int ret_pos;
	pagoffsize ret_var;
}stack_index_line;

//Si, agregue las otras cosas que va a tener el PCB como comentarios porque me da paja hacerlo
//despues. Asi que lo hago ahora ¯\_(ツ)_/¯

typedef struct{
	u_int32_t pid;
	//int ip;
	int page_counter;
	//aca iria una referencia a la tabla de archivos del proceso
	//code_index_line code_index[];
	//char* tag_index;
	//stack_index_line stack_index[];
	//int exit_code;
}PCB;

// SI, HAY VARIOS PARECIDOS PERO VA A SER MUY MOLESTO USARLOS SI LOS ANIDO

//VARIABLES GLOBALES
int pid = 0;
int mem_sock;
int listener_cpu;
int fdmax_cpu;

fd_set fd_procesos;

kernel_config data_config;

//Lista de conexiones (Cpus y Consolas)
t_list* lista_cpus;
t_list* lista_consolas;

//FUNCIONES
PCB* create_PCB(char* a_whole_bunch_of_serialized_code){
	PCB* nuevo_PCB = malloc(sizeof(PCB));
	/* M A G I A */
	/* A         */  //Aca sacaria toda la posta del PCB pero todavia no lo necesitamos.
	/* G         */  //Asi que no lo hice.
	/* I         */
	/* A / / / / */
	nuevo_PCB->pid = ++pid;
	nuevo_PCB->page_counter = 0; //Se updatearia cuando pedimos espacio a memoria
	return nuevo_PCB;
}

void *get_in_addr(struct sockaddr *sa)
{
if (sa->sa_family == AF_INET) {
return &(((struct sockaddr_in*)sa)->sin_addr);
}
return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void remove_by_fd_socket(t_list *lista, int sockfd){
	int i, max;
	max = list_size(lista);
	cpu_conexion *unaConexion, *conexion_encontrada;

	for(i=0; i < max; i++){
		unaConexion = list_get(lista, i);
		if(unaConexion->sock_fd == sockfd){
			conexion_encontrada = list_remove(lista, i);
			free(conexion_encontrada);
		}
	}
}

int sock_accept_new_connection(int listener, int *fdmax, fd_set *master){
	int newfd, addrlen;
	struct sockaddr_in direcServ;
	char remoteIP[INET6_ADDRSTRLEN];

	// manejamos las conexiones
	addrlen = sizeof direcServ;
	newfd = accept(listener,(struct sockaddr *)&direcServ,&addrlen);
	if (newfd == -1) {
		perror("accept");
			} else {
				FD_SET(newfd, master);
				if (newfd > *fdmax) {
				*fdmax = newfd;
					}
			printf("selectserver: new connection from %s on ""socket %d\n",inet_ntop(direcServ.sin_family,get_in_addr((struct sockaddr*)&direcServ),remoteIP, INET6_ADDRSTRLEN),newfd);
			}
	return newfd;
}

//Recibe como parametros la ip y el puerto de la conexion y crea el socket que escuchara las nuevas conexiones.
int get_fd_listener(char* puerto){

	struct addrinfo hints, *ai, *p;
	int listener, result;
	int yes=1;

	//configuramos el tipo de socket
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((result = getaddrinfo(NULL, puerto, &hints, &ai)) != 0) {
	fprintf(stderr, "selectserver: %s\n", gai_strerror(result));
	exit(1);
	}

	for(p = ai; p != NULL; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
				}
		//Para ignorar el caso de socket en uso
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
			}
		break;
	}

	if (p == NULL) {
	fprintf(stderr, "selectserver: failed to bind\n");
	exit(2);
	}

	freeaddrinfo(ai); // all done with this shit

	if (listen(listener, 10) == -1) {
	perror("listen");
	exit(3);
	}

	return listener;
}

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

void cargar_config(){

}

int main(int argc, char** argv) {
	if(argc == 0){
	printf("Debe ingresar ruta de .config y archivo\n");
	exit(1);
	}
	//Variables para config
	t_config *config_file;
	int i=0, k=0, y=0;

	//Variables para conexiones con servidores
	char buf[256];
	int sockfd_memoria, sockfd_fs;	//File descriptors de los sockets correspondientes a la memoria y al filesystem
	int bytes_mem, bytes_fs;

	//variiables para conexiones con clientes
	int listener, fdmax, newfd, nbytes;
	fd_set read_fds;
	int codigo;
	int list_cpu_size, list_consola_size;

	//consolas y cpus

	lista_cpus = list_create();
	lista_consolas = list_create();
	cpu_conexion *nueva_conexion_cpu;
	consola_conexion *nueva_conexion_consola;

	FD_ZERO(&fd_procesos);
	FD_ZERO(&read_fds);

	struct sockaddr_in direcServ;

	config_file = config_create_from_relative_with_check(argc,argv);

	//Configuro al kernel
	data_config.puerto_prog = config_get_string_value(config_file, "PUERTO_PROG");
	data_config.puerto_cpu = config_get_string_value(config_file, "PUERTO_CPU");
	data_config.ip_memoria = config_get_string_value(config_file,"IP_MEMORIA");
	data_config.puerto_memoria = config_get_string_value(config_file, "PUERTO_MEMORIA");
	data_config.ip_fs = config_get_string_value(config_file,"IP_FS");
	data_config.puerto_fs = config_get_string_value(config_file, "PUERTO_FS");
	data_config.quantum = config_get_int_value(config_file, "QUANTUM");
	data_config.quantum_sleep = config_get_int_value(config_file, "QUANTUM_SLEEP");
	data_config.algoritmo = config_get_string_value(config_file,"ALGORITMO");
	data_config.grado_multiprog = config_get_int_value(config_file, "GRADO_MULTIPROG");
	data_config.sem_ids = config_get_array_value(config_file, "SEM_IDS");
	data_config.sem_init = (int*) config_get_array_value(config_file, "SEM_INIT");
	//Sino hago el atoi me los toma como strings por alguna razon

	while(data_config.sem_init[y]!=NULL)
	{
		data_config.sem_init[y] = atoi(data_config.sem_init[y]);
		y++;
	}

	data_config.shared_vars = config_get_array_value(config_file, "SHARED_VARS");
	data_config.stack_size = config_get_int_value(config_file, "STACK_SIZE");

	//Imprimo los datos
	printf("Puerto Programas: %s\n", data_config.puerto_prog);
	printf("Puerto CPUs: %s\n", data_config.puerto_cpu);
	printf("IP Memoria: %s\n", data_config.ip_memoria);
	printf("Puerto Memoria: %s\n", data_config.puerto_memoria);
	printf("IP FS: %s\n", data_config.ip_fs);
	printf("Puerto FS: %s\n", data_config.puerto_fs);
	printf("Quantum: %i\n", data_config.quantum);
	printf("Quantum Sleep: %i\n", data_config.quantum_sleep);
	printf("Algoritmo: %s\n", data_config.algoritmo);
	printf("Grado Multiprog: %i\n", data_config.grado_multiprog);
	while(data_config.sem_ids[i]!=NULL){
		printf("Semaforo %s = %i\n",data_config.sem_ids[i],data_config.sem_init[i]);
		i++;
	}
	while(data_config.shared_vars[k]!=NULL){
		printf("Variable Global %i: %s\n",k,data_config.shared_vars[k]);
		k++;
	}
	printf("Tamaño del Stack: %i\n", data_config.stack_size);

//********************************Conexiones***************************************//
	//Servidores

	//sockfd_memoria = get_fd_server(data_config.ip_memoria,data_config.puerto_memoria);		//Nos conectamos a la memoria
	//sockfd_fs= get_fd_server(data_config.ip_fs,data_config.puerto_fs);		//Nos conectamos al fs

	//memset(buf, 0, 256*sizeof(char));	//limpiamos nuestro buffer
	//sprintf(buf, "Kernel %d conectado!", pid);
	//send(sockfd_memoria, buf, sizeof buf, 0);
	//send(sockfd_fs, buf, sizeof buf, 0);

	//Consolas y CPUs
	listener = get_fd_listener(data_config.puerto_prog);

	FD_SET(listener, &fd_procesos);

	fdmax = listener;

	for(;;) {
		read_fds = fd_procesos;
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) {
				if (i == listener) {
					newfd = sock_accept_new_connection(listener, &fdmax, &fd_procesos);
				} else {
					memset(buf, 0, 256*sizeof(char));	//limpiamos el buffer
					if ((nbytes = recv(i, &codigo, sizeof(int), 0)) <= 0) {

						if (nbytes == 0) {
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						//Dado que no sabemos a que proceso pertenece dicho socket, hacemos que se fije en ambas listas para encontrar el elemento y liberarlo
						pthread_mutex_lock(&mutex_fd_cpus);
						remove_by_fd_socket(lista_cpus, i); //Lo sacamos de la lista de conexiones cpus y liberamos la memoria
						pthread_mutex_unlock(&mutex_fd_cpus);

						pthread_mutex_lock(&mutex_fd_consolas);
						remove_by_fd_socket(lista_consolas, i); //Lo sacamos de la lista de conexiones de consola y liberamos la memoria
						pthread_mutex_unlock(&mutex_fd_consolas);

						close(i);
						FD_CLR(i, &fd_procesos); // remove from master set

						printf("Hay %d cpus conectadas\n", list_size(lista_cpus));
						printf("Hay %d consolas conectadas\n", list_size(lista_consolas));
					} else {
						printf("Se recibio: %d\n", codigo);	//Si recibio un 1 significa que el que lo envio es una CPU, si es 2, una consola.

						if(codigo == 1){
							nueva_conexion_cpu = malloc(sizeof(cpu_conexion));
							nueva_conexion_cpu->sock_fd = newfd;
							pthread_mutex_lock(&mutex_fd_cpus);
							list_add(lista_cpus, nueva_conexion_cpu);
							pthread_mutex_unlock(&mutex_fd_cpus);

							printf("Hay %d cpus conectadas\n", list_size(lista_cpus));
						}
						if(codigo == 2){
							nueva_conexion_consola = malloc(sizeof(consola_conexion));
							nueva_conexion_consola->sock_fd = newfd;
							pthread_mutex_lock(&mutex_fd_consolas);
							list_add(lista_consolas, nueva_conexion_consola);
							pthread_mutex_unlock(&mutex_fd_consolas);

							printf("Hay %d consolas conectadas\n", list_size(lista_consolas));
						}
						//send(sockfd_memoria, buf, sizeof buf,0);	//Le mandamos a la memoria
						//send(sockfd_fs, buf, sizeof buf,0);	//Le mandamos al filesystem


						memset(buf, 0, sizeof buf);
					}
				}
			}
		}
	}

	close(sockfd_memoria);
	config_destroy(config_file);
	return EXIT_SUCCESS;
}
