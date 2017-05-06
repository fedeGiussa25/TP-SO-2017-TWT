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
#include <commons/collections/queue.h>
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

//Todo lo de mutex
pthread_mutex_t mutex_fd_cpus;
pthread_mutex_t mutex_fd_consolas;
pthread_mutex_t mutex_procesos_actuales;

//Los mutex para las colas
pthread_mutex_t mutex_ready_queue;
pthread_mutex_t mutex_exit_queue;
pthread_mutex_t mutex_exec_queue;


// Structs de conexiones
//Todo lo de structs de PCB
typedef struct{
	int sock_fd;
}proceso_conexion;

// STRUCTS DE PCB
//VOLO: "TAL VEZ DEBERIAMOS PONERLOS EN UN .h"
//GIUSSA: "Tal vez deberias hacerlo :D"
//VOLO: "Seras rompehuevos eh"
//GIUSSA: "Si que te gusta comentar boludeces eh"
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

//Todo lo de variables globales
int pid = 0;
int mem_sock;
int listener_cpu;
int fdmax_cpu;
int procesos_actuales = 0; //La uso para ver que no haya mas de lo que la multiprogramacion permite

fd_set fd_procesos;

kernel_config data_config;

//Lista de conexiones (Cpus y Consolas)
t_list* lista_cpus;
t_list* lista_consolas;
t_list* lista_en_ejecucion;

//Colas de planificacion
t_queue* ready_queue;
t_queue* exit_queue;
t_queue* exec_queue;

//FUNCIONES
//Todo lo de funciones de PCB
PCB* create_PCB(){
	PCB* nuevo_PCB = malloc(sizeof(PCB));
	/* M A G I A */
	/* A         */  //Aca sacaria toda la posta del PCB pero todavia no lo necesitamos.
	/* G         */  //Asi que no lo hice.
	/* I         */
	/* A / / / / */
	nuevo_PCB->pid = ++pid;
	nuevo_PCB->page_counter = 0;
	pthread_mutex_lock(&mutex_procesos_actuales);
	procesos_actuales++;//Se updatearia cuando pedimos espacio a memoria
	pthread_mutex_unlock(&mutex_procesos_actuales);
	return nuevo_PCB;
}

void delete_PCB(PCB* pcb){
	pthread_mutex_lock(&mutex_procesos_actuales);
	procesos_actuales--;
	pthread_mutex_unlock(&mutex_procesos_actuales);
	queue_push(exit_queue,pcb);
}

void *get_in_addr(struct sockaddr *sa)
{
if (sa->sa_family == AF_INET) {
return &(((struct sockaddr_in*)sa)->sin_addr);
}
return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//Todo lo de sockets

void remove_by_fd_socket(t_list *lista, int sockfd){
	bool _remove_socket(void* unaConex)
	    {
			proceso_conexion *conex = (proceso_conexion*) unaConex;
			return conex->sock_fd == sockfd;
	    }
	proceso_conexion* conexion_encontrada =  list_remove_by_condition(lista,*_remove_socket);
	free(conexion_encontrada);
}

int sock_accept_new_connection(int listener, int *fdmax, fd_set *master){
	int newfd;
	uint32_t addrlen;
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

//Todo lo de config

void cargar_config(t_config *config_file){
	int y = 0;
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
	//Sino hago el atoi me los toma como strings por alguna razon
	data_config.sem_init = (int*) config_get_array_value(config_file, "SEM_INIT");

	while(data_config.sem_init[y]!=NULL)
	{
		data_config.sem_init[y] = atoi(data_config.sem_init[y]);
		y++;
	}

	data_config.shared_vars = config_get_array_value(config_file, "SHARED_VARS");
	data_config.stack_size = config_get_int_value(config_file, "STACK_SIZE");
}

void print_config(){
	int k = 0;
	int i =0;

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
}

int esta_en_uso(int fd){
	int i;
	int en_uso = 0;
	proceso_conexion *cpu;

	for(i= 0; i< list_size(lista_en_ejecucion); i++){
		cpu = list_get(lista_en_ejecucion,i);
		if(cpu->sock_fd == fd){
			en_uso =1;
		}
	}
	return en_uso;
}

int buscar_cpu_libre(){
	int i=0;
	int encontrado =0;
	proceso_conexion *cpu;
	while(encontrado == 0 && list_size(lista_cpus)){
		cpu = list_get(lista_cpus, i);
		if(esta_en_uso(cpu->sock_fd) == 0){
			encontrado = 1;
		}else{
			i++;
		}
	}
	return i;
}

void ready_a_cpu(){
	while(1)
	{
		int i;

		pthread_mutex_lock(&mutex_ready_queue);

		if(queue_size(ready_queue)>0){

			pthread_mutex_lock(&mutex_fd_cpus);

			i = buscar_cpu_libre();

			if(list_size(lista_cpus)>0){
				proceso_conexion *cpu = list_get(lista_cpus,i);
				list_add(lista_en_ejecucion, cpu);
				PCB *pcb_to_use = queue_pop(ready_queue);
				void *sendbuf = malloc(sizeof(u_int32_t)+sizeof(int));
				memcpy(sendbuf,&(pcb_to_use->pid),sizeof(u_int32_t));
				memcpy(sendbuf+sizeof(u_int32_t),&(pcb_to_use->page_counter),sizeof(int));
				send(cpu->sock_fd,sendbuf,sizeof(u_int32_t)+sizeof(int),0);
				printf("Mande un PCB a una CPU :D\n\n");
				free(sendbuf);
			}
			pthread_mutex_unlock(&mutex_fd_cpus);
		}
		pthread_mutex_unlock(&mutex_ready_queue);
	}
}


//Todo lo referido a manejador_de_scripts

typedef struct{ //Estructura auxiliar para ejecutar el manejador de scripts
	int fd_consola; //La Consola que me mando el script
	int fd_mem; //La memoria
	int grado_multiprog; //El grado de multiprog actual
	int messageLength; //El largo del script
	void* realbuf; //El script serializado
}script_manager_setup;

void manejador_de_scripts(script_manager_setup* sms){
	void *sendbuf;
	int codigo_cpu = 2, numbytes, recvmem;
	PCB* pcb_to_use;

	if(procesos_actuales < sms->grado_multiprog){
		//Creo el PCB
		printf("A crear el PCB!\n");
		pcb_to_use = create_PCB();

		printf("Ahora hay %d procesos en planificacion!\n", procesos_actuales);

		//Le mando el codigo y el largo a la memoria
		sendbuf = malloc(sizeof(int)*2 + sizeof(u_int32_t) + sms->messageLength);
		memcpy(sendbuf,&codigo_cpu,sizeof(int));
		memcpy(sendbuf+sizeof(u_int32_t),&(pcb_to_use->pid),sizeof(u_int32_t));
		memcpy(sendbuf+sizeof(int)+sizeof(u_int32_t),&(sms->messageLength),sizeof(int));
		memcpy(sendbuf+sizeof(int)*2 + sizeof(u_int32_t),sms->realbuf,sms->messageLength);
		printf("Mandamos a memoria!\n");
		send(sms->fd_mem, sendbuf, sms->messageLength+sizeof(int)*2,0);

		//Me quedo esperando que responda memoria
		printf("Y esperamos!\n");
		numbytes = recv(sms->fd_mem, &recvmem, sizeof(int),0);
		if(numbytes > 0)
		{
			//significa que hay espacio y guardo las cosas
			if(recvmem > 0){
				char *happy = "Hay espacio en memoria :D\n\n";
				printf("%s",happy);
				pcb_to_use->page_counter = recvmem;
				pthread_mutex_lock(&mutex_ready_queue);
				queue_push(ready_queue,pcb_to_use);
				pthread_mutex_unlock(&mutex_ready_queue);
				send(sms->fd_consola,&recvmem,sizeof(int),0);
			}
			//significa que no hay espacio
			if(recvmem < 0){
				char *sad = "No hay espacio en memoria D:\n\n";
				printf("%s", sad);
				pthread_mutex_lock(&mutex_exit_queue);
				queue_push(exit_queue,pcb_to_use);
				pthread_mutex_unlock(&mutex_exit_queue);
				send(sms->fd_consola,&recvmem,sizeof(int),0);
			}
		}
		if(numbytes == 0){printf("Se desconecto memoria\n\n");}
		if(numbytes != 0){perror("receive");}
	}
	else{
		printf("El sistema ya llego a su tope de multiprogramacion, intente luego\n\n");
		int error = -1;
		send(sms->fd_consola, &error,sizeof(int),0);
	}
	//free(sms->realbuf);
	free(sms);
}

//TODO el main

int main(int argc, char** argv) {
	if(argc == 0){
	printf("Debe ingresar ruta de .config y archivo\n");
	exit(1);
	}
	//Variables para config
	t_config *config_file;
	int i=0;

	//Variables para conexiones con servidores
	char *buf = malloc(256);
	int sockfd_memoria, sockfd_fs;	//File descriptors de los sockets correspondientes a la memoria y al filesystem
	int bytes_mem, bytes_fs;

	//variables para conexiones con clientes
	int listener, fdmax, newfd, nbytes;
	fd_set read_fds;
	int codigo, processID;
	int messageLength;
	void* realbuf;
	void* sendbuf;
	char* message;

	//Consolas y cpus
	lista_cpus = list_create();
	lista_consolas = list_create();
	lista_en_ejecucion = list_create();
	proceso_conexion *nueva_conexion_cpu;
	proceso_conexion *nueva_conexion_consola;

	//Colas de planificacion
	exit_queue = queue_create();
	ready_queue = queue_create();
	exec_queue = queue_create();

	FD_ZERO(&fd_procesos);
	FD_ZERO(&read_fds);

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../Kernel/") + strlen(argv[1])+1);
	*cfgPath = '\0';
	strcpy(cfgPath, "../../Kernel/");

	config_file = config_create_from_relative_with_check(argv, cfgPath);
	cargar_config(config_file);	//Carga la estructura data_config de Kernel
	print_config();	//Adivina... la imprime por pantalla
	//config_destroy(config_file);

	free(cfgPath);

	//********************************Conexiones***************************************//

	//Servidores
	sockfd_memoria = get_fd_server(data_config.ip_memoria,data_config.puerto_memoria);		//Nos conectamos a la memoria
	//sockfd_fs= get_fd_server(data_config.ip_fs,data_config.puerto_fs);		//Nos conectamos al fs

	int handshake = 1;
	int resp;
	send(sockfd_memoria, &handshake, sizeof(int), 0);
	bytes_mem = recv(sockfd_memoria, &resp, sizeof(int), 0);
	if(bytes_mem > 0 && resp == 1){
				printf("%d\n",resp);
	}else{
		if(bytes_mem == -1){
			perror("recieve");
			exit(3);
			}
		if(bytes_mem == 0){
			printf("Se desconecto el socket: %d\n", sockfd_memoria);
			close(sockfd_memoria);
		}
	}

	//Consolas y CPUs
	listener = get_fd_listener(data_config.puerto_prog);

	FD_SET(listener, &fd_procesos);

	fdmax = listener;

	//Hilo que manda de cola ready a cpu
	pthread_t rtocpu;
	pthread_create(&rtocpu,NULL,(void*)ready_a_cpu,NULL);

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
						//Dado que no sabemos a que proceso pertenece dicho socket,
						//hacemos que se fije en ambas listas para encontrar el elemento y liberarlo
						pthread_mutex_lock(&mutex_fd_cpus);
						remove_by_fd_socket(lista_cpus, i); //Lo sacamos de la lista de conexiones cpus y liberamos la memoria
						pthread_mutex_unlock(&mutex_fd_cpus);

						pthread_mutex_lock(&mutex_fd_consolas);
						remove_by_fd_socket(lista_consolas, i); //Lo sacamos de la lista de conexiones de consola y liberamos la memoria
						pthread_mutex_unlock(&mutex_fd_consolas);

						close(i);
						FD_CLR(i, &fd_procesos); // remove from master set

						printf("Hay %d cpus conectadas\n", list_size(lista_cpus));
						printf("Hay %d consolas conectadas\n\n", list_size(lista_consolas));
					} else {
						printf("Se recibio: %d\n", codigo);
						//Si el codigo es 1 significa que el proceso del otro lado esta haciendo el handshake
						if(codigo == 1){
							recv(i, &processID,sizeof(int),0);
							if(processID == 1){	//Si el processID es 1, sabemos que es una CPU
								printf("Es una CPU\n");
								nueva_conexion_cpu = malloc(sizeof(proceso_conexion));
								nueva_conexion_cpu->sock_fd = newfd;
								pthread_mutex_lock(&mutex_fd_cpus);
								list_add(lista_cpus, nueva_conexion_cpu);
								pthread_mutex_unlock(&mutex_fd_cpus);

								printf("Hay %d cpus conectadas\n\n", list_size(lista_cpus));

							}
							if(processID == 2){	//Si en cambio el processID es 2, es una Consola
								printf("Es una Consola\n");
								nueva_conexion_consola = malloc(sizeof(proceso_conexion));
								nueva_conexion_consola->sock_fd = newfd;
								pthread_mutex_lock(&mutex_fd_consolas);
								list_add(lista_consolas, nueva_conexion_consola);
								pthread_mutex_unlock(&mutex_fd_consolas);

								printf("Hay %d consolas conectadas\n\n", list_size(lista_consolas));
							}
						}//Si el codigo es 2, significa que del otro lado estan queriendo mandar un programa ansisop
						if(codigo == 2){
							//Agarro el resto del mensaje
							printf("Ding, dong, bing, bong! Me llego un script!\n");

							recv(i, &messageLength, sizeof(int), 0);
							printf("El script mide: %d \n", messageLength);
							void* aux = malloc(messageLength+2);
							memset(aux,0,messageLength+2);
							recv(i, aux, messageLength, 0);
							memset(aux+messageLength+1,'\0',1);
							char* charaux = (char*) aux;
							//printf("Y este es el script:\n%s\n", charaux);


							//Setteo el script manager
							script_manager_setup* sms = malloc(sizeof(script_manager_setup));
							sms->fd_consola = i;
							sms->fd_mem = sockfd_memoria;
							sms->grado_multiprog = data_config.grado_multiprog;
							sms->messageLength = messageLength;
							sms->realbuf = aux;

							manejador_de_scripts(sms);
						}
						//Si el codigo es 50, significa que CPU me mando que necesita hacer WAIT
						//Y WAIT  es una operacion privilegiada, solo yo, kernel, la puedo hacer ;)
						if (codigo==50)
						{
							recv(i, &messageLength , sizeof(int), 0);
							realbuf = malloc(messageLength+2);
							memset(realbuf,0,messageLength+2);
							recv(i, realbuf, messageLength, 0);
							message = (char*) realbuf;
							message[messageLength+1]='\0';
							printf("CPU pide: Wait en semaforo: %s\n\n", message);
							free(realbuf);
						}

						//send(sockfd_memoria, buf, sizeof buf,0);	//Le mandamos a la memoria
						//send(sockfd_fs, buf, sizeof buf,0);	//Le mandamos al filesystem

						memset(buf,0,256);
					}
				}
			}
		}
	}

	close(sockfd_memoria);
	config_destroy(config_file);
	return EXIT_SUCCESS;
}
