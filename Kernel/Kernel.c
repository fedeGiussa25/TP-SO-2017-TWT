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
#include <parser/metadata_program.h>

//Todo lo de mutex
pthread_mutex_t mutex_fd_cpus;
pthread_mutex_t mutex_fd_consolas;
pthread_mutex_t mutex_procesos_actuales;

//Los mutex para las colas
pthread_mutex_t mutex_ready_queue;
pthread_mutex_t mutex_exit_queue;
pthread_mutex_t mutex_exec_queue;
pthread_mutex_t mutex_new_queue;
pthread_mutex_t mutex_process_list;

// Structs de conexiones
//Todo lo de structs de PCB
typedef struct{
	int sock_fd;
	int proceso;
}proceso_conexion;

// STRUCTS DE PCB
//VOLO: "TAL VEZ DEBERIAMOS PONERLOS EN UN .h"
//GIUSSA: "Tal vez deberias hacerlo :D"
//VOLO: "Seras rompehuevos eh"
//GIUSSA: "Si que te gusta comentar boludeces eh"
//VOLO: "Asi soy yo ¯\_(ツ)_/¯"
//GIUSSA: "Asi te quiero. Y justamente por estas cosas es que me encanta romperte los bolas"

/*typedef struct{
	int offset;
	int size;
}code_index_line;


typedef struct{
	char* id;
	int page;
	int offset;
	int size;
}idpagoffsize;

*/

//Si, agregue las otras cosas que va a tener el PCB como comentarios porque me da paja hacerlo
//despues. Asi que lo hago ahora ¯\_(ツ)_/¯

typedef struct{
	int page;
	int offset;
	int size;
}pagoffsize;

typedef struct{
	u_int32_t inicio;
	u_int32_t offset;
} entrada_indice_de_codigo;

typedef struct {
	t_list* args;
	t_list* vars;
	int ret_pos;
	pagoffsize ret_var;
} registroStack;

typedef struct{
	u_int32_t pid;

	int page_counter;
	int direccion_inicio_codigo;
	int program_counter;

	int cantidad_de_instrucciones;
	entrada_indice_de_codigo* indice_de_codigo;
	//aca iria una referencia a la tabla de archivos del proceso

	char* lista_de_etiquetas;
	int lista_de_etiquetas_length;
	int exit_code;
	char* estado;

	t_list* stack_index;
	int primerPaginaStack; //Seria la cant de paginas del codigo porque viene despues del codigo
	int stackPointer;
	int tamanioStack;
}PCB;

typedef struct{ //Estructura auxiliar para ejecutar el manejador de scripts
	int fd_consola; //La Consola que me mando el script
	int fd_mem; //La memoria
	int grado_multiprog; //El grado de multiprog actual
	int messageLength; //El largo del script
	void* realbuf; //El script serializado
}script_manager_setup;

typedef struct{
	PCB* pcb;
	script_manager_setup* sms;
}new_pcb;

// SI, HAY VARIOS PARECIDOS PERO VA A SER MUY MOLESTO USARLOS SI LOS ANIDO

//Todo lo de variables globales
int pid = 0;
int mem_sock;
int listener_cpu;
int fdmax_cpu;
int procesos_actuales = 0; //La uso para ver que no haya mas de lo que la multiprogramacion permite
bool plan_go = true;


fd_set fd_procesos;

kernel_config data_config;
int tamanio_del_stack;

//Lista de conexiones (Cpus y Consolas)
t_list* lista_cpus;
t_list* lista_consolas;
t_list* lista_en_ejecucion;

//Colas de planificacion
t_queue* ready_queue;
t_queue* exit_queue;
t_queue* exec_queue;
t_queue* new_queue;

//Listado con los procesos del sistema
t_list* todos_los_procesos;

//FUNCIONES
void remove_and_destroy_by_fd_socket(t_list *lista, int sockfd){
	bool _remove_socket(void* unaConex)
	    {
			proceso_conexion *conex = (proceso_conexion*) unaConex;
			return conex->sock_fd == sockfd;
	    }
	proceso_conexion* conexion_encontrada =  list_remove_by_condition(lista,*_remove_socket);
	free(conexion_encontrada);
}

proceso_conexion *remove_by_fd_socket(t_list *lista, int sockfd){
	bool _remove_socket(void* unaConex)
	    {
			proceso_conexion *conex = (proceso_conexion*) unaConex;
			return conex->sock_fd == sockfd;
	    }
	proceso_conexion* conexion_encontrada =  list_remove_by_condition(lista,*_remove_socket);
	return conexion_encontrada;
}

entrada_indice_de_codigo* create_indice_de_codigo(t_metadata_program *metadata){
	int i;
	u_int32_t cantidad_instrucciones = metadata->instrucciones_size;

	entrada_indice_de_codigo *indice_de_codigo = malloc(sizeof(entrada_indice_de_codigo)*cantidad_instrucciones);

	for(i=0; i < cantidad_instrucciones; i++){
		indice_de_codigo[i].inicio = metadata->instrucciones_serializado[i].start;
		indice_de_codigo[i].offset = metadata->instrucciones_serializado[i].offset;
	}

	return indice_de_codigo;
}


void print_metadata(t_metadata_program* metadata){
	int i;

	printf("\n\n***INFORMACION DE METADATA***\n");
	printf("instruccion_inicio = %d\n", metadata->instruccion_inicio);
	printf("instrucciones_size = %d\n", metadata->instrucciones_size);

	printf("Instrucciones serializadas: \n");

	for(i=0; i<(metadata->instrucciones_size); i++){
		printf("Instruccion %d: Inicio = %d, Offset = %d\n", i, metadata->instrucciones_serializado[i].start, metadata->instrucciones_serializado[i].offset);
	}

	printf("etiquetas_size = %d\n", metadata->etiquetas_size);
	printf("etiquetas = %s\n", metadata->etiquetas);

	printf("cantidad_de_funciones = %d\n", metadata->cantidad_de_funciones);
	printf("cantidad_de_etiquetas = %d\n", metadata->cantidad_de_etiquetas);
	printf("***FIN DE LA METADATA***\n\n");
}


//Todo lo de funciones de PCB
PCB* create_PCB(char* script, int fd_consola){
	PCB* nuevo_PCB = malloc(sizeof(PCB));
	t_metadata_program* metadata = metadata_desde_literal(script);
	nuevo_PCB->pid = ++pid;
	nuevo_PCB->program_counter = metadata->instruccion_inicio;
	nuevo_PCB->page_counter = 0;

	nuevo_PCB->cantidad_de_instrucciones = metadata->instrucciones_size;
	nuevo_PCB->indice_de_codigo = create_indice_de_codigo(metadata);

	nuevo_PCB->lista_de_etiquetas = metadata->etiquetas;
	nuevo_PCB->lista_de_etiquetas_length = metadata->etiquetas_size;
	nuevo_PCB->estado = "Nuevo";


	nuevo_PCB->tamanioStack=tamanio_del_stack;
	nuevo_PCB->stackPointer=0;
	nuevo_PCB->stack_index=list_create();

	print_metadata(metadata);

	pthread_mutex_lock(&mutex_process_list);
	list_add(todos_los_procesos,nuevo_PCB);
	pthread_mutex_unlock(&mutex_process_list);
	pthread_mutex_lock(&mutex_procesos_actuales);
	procesos_actuales++;
	pthread_mutex_unlock(&mutex_procesos_actuales);

	proceso_conexion *consola = remove_by_fd_socket(lista_consolas,fd_consola);
	consola->proceso = nuevo_PCB->pid;
	list_add(lista_consolas, consola);

	return nuevo_PCB;
}

void print_PCB(PCB* pcb){
	printf("PID: %d, Estado: %s\n",pcb->pid,pcb->estado);
}

//Esta funcion es un quilombo asi que la explico aca
//No hay manera de sacar un objeto de un queue sin sacar todos los que tiene adelante
void remove_PCB_from_specific_queue(PCB* pcb,t_queue* queue){
	int i = 0, len;
	t_list* lista_auxiliar = list_create();
	len = queue_size(queue);
	while(i<len){
		//Saco un PCB y me fijo si es el que busco
		PCB* aux = queue_pop(queue);
		if(!(aux->pid == pcb->pid))
		{
			//Si no es, lo ubico en una lista auxiliar y aumento i para seguir buscando
			list_add(lista_auxiliar,aux);
			//Si lo es, no hago nada, mi objetivo era sacarlo y ya lo hice
		}
		i++;
	}
	//Cuando finalice me fijo si coloque algun PCB en la lista auxiliar
	i = 0;
	if(list_size(lista_auxiliar)>0){
		//De ser asi...
		while(i<list_size(lista_auxiliar)){
			//Voy ubicandolos del ultimo al primero en la cola de vuelta para mantener el orden previo
			PCB* aux = list_get(lista_auxiliar, i);
			queue_push(queue,aux);
			i++;
		}
	}
	list_destroy(lista_auxiliar);
}

void delete_PCB(PCB* pcb){
	pthread_mutex_lock(&mutex_procesos_actuales);
	procesos_actuales--;
	pthread_mutex_unlock(&mutex_procesos_actuales);
	pthread_mutex_lock(&mutex_exit_queue);
	queue_push(exit_queue,pcb);
	pthread_mutex_unlock(&mutex_exit_queue);
}

void remove_from_queue(PCB* pcb){
	if(strcmp(pcb->estado,"New")==0)
	{
		pthread_mutex_lock(&mutex_new_queue);
		remove_PCB_from_specific_queue(pcb,new_queue);
		pthread_mutex_unlock(&mutex_new_queue);
	}
	else if(strcmp(pcb->estado,"Ready")==0)
	{
		pthread_mutex_lock(&mutex_ready_queue);
		remove_PCB_from_specific_queue(pcb,ready_queue);
		pthread_mutex_unlock(&mutex_ready_queue);
	}
	else if(strcmp(pcb->estado,"Exec")==0)//No va a funcionar asi por siempre pero lo dejo como placeholder
	{
		pthread_mutex_lock(&mutex_exec_queue);
		remove_PCB_from_specific_queue(pcb,exec_queue);
		pthread_mutex_unlock(&mutex_exec_queue);
	}
	else printf("PCB invalido, no se encuentra en ninguna cola");
}

void end_process(int PID, int socket_memoria, int sock_consola){
	int i = 0;
	bool encontrado = 0;
	pthread_mutex_lock(&mutex_process_list);
	while(i<list_size(todos_los_procesos) && !encontrado)
	{
		PCB* PCB = list_get(todos_los_procesos,i);
		if(PID == PCB->pid)	{
			if(strcmp(PCB->estado,"Exit")!=0){
				remove_from_queue(PCB);
				PCB->exit_code = -7;
				PCB->estado = "Exit";
				delete_PCB(PCB);
				printf("El proceso ha sido finalizado\n");
			}
			else{
				printf("El proceso elegido ya esta finalizado\n");
			}
			encontrado = 1;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_process_list);
	if(!encontrado)	{
		printf("El PID seleccionado todavia no ha sido asignado a ningun proceso\n");
	}
	if(encontrado){
		//Le aviso a memoria que le saque las paginas asignadas
		void* sendbuf_mem = malloc(sizeof(int)*2);
		int codigo_para_borrar_paginas = 5;
		memcpy(sendbuf_mem,&codigo_para_borrar_paginas,sizeof(int));
		memcpy(sendbuf_mem+sizeof(int),&pid,sizeof(int));
		send(socket_memoria,sendbuf_mem,sizeof(int)*2,0);

		//Y le aviso a la consola que se aborto el proceso
		void* sendbuf_consola = malloc(sizeof(int));
		int codigo_para_abortar_proceso = 7;
		memcpy(sendbuf_consola,&codigo_para_abortar_proceso,sizeof(int));
		send(sock_consola,sendbuf_consola,sizeof(int),0);
	}
	printf("\n");
}

void print_PCB_list(){
	if(list_size(todos_los_procesos)>0)
	{
		int i = 0;
		pthread_mutex_lock(&mutex_process_list);
		while(i < list_size(todos_los_procesos)){
			PCB* aux = list_get(todos_los_procesos,i);
			printf("PID: %d\n",aux->pid);
			i++;
		}
		pthread_mutex_unlock(&mutex_process_list);
		printf("\n");
	} else printf("No hay procesos en planificacion\n\n");
}

void print_commands()
{
	printf("\nComandos\n");
	printf("\t list   - Lista de Procesos\n");
	printf("\t end    - Finalizar Proceso\n");
	printf("\t state  - Estado de un Proceso\n");
	printf("\t plan   - Detener/Reanudar Planificacion\n\n");
}

void pcb_state()
{
	int i = 0;
	bool encontrado = 0;
	int* PID = malloc(sizeof(int));
	printf("Ingrese el PID del PCB cuyo estado desea ver: ");
	scanf("%d",PID);
	if(*PID<1)
	{
		printf("Los PIDs empiezan en 1 genio :l");
	}
	else
	{
		pthread_mutex_lock(&mutex_process_list);
		while(i<list_size(todos_los_procesos) && !encontrado)
		{
			PCB* PCB = list_get(todos_los_procesos,i);
			if(*PID == PCB->pid)
			{
				print_PCB(PCB);
				encontrado = 1;
			}
			i++;
		}
		pthread_mutex_unlock(&mutex_process_list);
		if(!encontrado)
		{
			printf("El PID seleccionado todavia no ha sido asignado a ningun proceso\n");
		}
	}
	printf("\n");
	free(PID);
}

void menu()
{
	while(1)
	{
		char* command = malloc(20);
		scanf("%s", command);
		if((strcmp(command, "list")) == 0)
		{
			print_PCB_list();
		}
		else if((strcmp(command, "end")) == 0)
		{
			//end_process(); //Faltan cositas
		}
		else if((strcmp(command, "state")) == 0)
		{
			pcb_state();
		}
		else if((strcmp(command, "plan")) == 0)
		{
			if(plan_go){
				plan_go=false;
				printf("Se ha detenido la planificacion\n\n");}
			else{
				plan_go=true;
				printf("Se ha reanudado la planificacion\n\n");}
		}
		else
		{
			printf("Comando incorrecto. Ingrese otro comando: \n");
			continue;
		}
		free(command);
	}
}



void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) 
		return &(((struct sockaddr_in*)sa)->sin_addr);
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//Todo lo de sockets

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
				if (newfd > *fdmax)
					*fdmax = newfd;
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
		if (listener < 0)
			continue;
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

	while(data_config.sem_init[y])
	{
		data_config.sem_init[y] = atoi(data_config.sem_init[y]);
		y++;
	}

	data_config.shared_vars = config_get_array_value(config_file, "SHARED_VARS");
	data_config.stack_size = config_get_int_value(config_file, "STACK_SIZE");

	tamanio_del_stack = data_config.stack_size;
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
		if(cpu->sock_fd == fd)
			en_uso =1;
	}
	return en_uso;
}

int buscar_cpu_libre(){
	int i=0;
	int encontrado =0;
	proceso_conexion *cpu;
	while(encontrado == 0 && i!=list_size(lista_cpus)){
		cpu = list_get(lista_cpus, i);
		if(esta_en_uso(cpu->sock_fd) == 0)
			encontrado = 1;
		else
			i++;
	}
	return i;
}

//Todo lo referido a manejador_de_scripts


void guardado_en_memoria(script_manager_setup* sms, PCB* pcb_to_use){
	void *sendbuf;
	int codigo_cpu = 2, numbytes, page_counter, direccion;

	//Le mando el codigo y el largo a la memoria
	//INICIO SERIALIZACION PARA MEMORIAAAAA
	sendbuf = malloc(sizeof(int)*3 + sizeof(u_int32_t) + sms->messageLength);
	memcpy(sendbuf,&codigo_cpu,sizeof(int));
	memcpy(sendbuf+sizeof(int),&(pcb_to_use->pid),sizeof(u_int32_t));
	memcpy(sendbuf+sizeof(int)+sizeof(u_int32_t),&(data_config.stack_size),sizeof(int));
	memcpy(sendbuf+sizeof(int)*2+sizeof(u_int32_t),&(sms->messageLength),sizeof(int));
	memcpy(sendbuf+sizeof(int)*3+sizeof(u_int32_t),sms->realbuf,sms->messageLength);
	printf("Mandamos a memoria!\n");
	send(sms->fd_mem, sendbuf, sms->messageLength+sizeof(int)*3+sizeof(u_int32_t),0);
	//YA SERIALIZE Y MANDE A MEMORIA MIAMEEEEEEEEEE

	//Me quedo esperando que responda memoria
	printf("Y esperamos!\n");

	numbytes = recv(sms->fd_mem, &page_counter, sizeof(int),0);
	recv(sms->fd_mem, &direccion, sizeof(int),0);

	if(numbytes > 0)
	{
		//significa que hay espacio y guardo las cosas
		if(page_counter > 0){
			printf("El proceso PID %d se ha guardado en memoria \n\n",pcb_to_use->pid);
			pcb_to_use->page_counter = page_counter;
			pcb_to_use->primerPaginaStack=page_counter-tamanio_del_stack; //pagina donde arranca el stack
			pcb_to_use->direccion_inicio_codigo = direccion;
			pcb_to_use->estado = "Ready";
			pthread_mutex_lock(&mutex_ready_queue);
			queue_push(ready_queue,pcb_to_use);
			pthread_mutex_unlock(&mutex_ready_queue);
			send(sms->fd_consola,&page_counter,sizeof(int),0);
		}
		//significa que no hay espacio
		if(page_counter < 0){
			printf("El proceso PID %d no se ha podido guardar en memoria \n\n",pcb_to_use->pid);
			pcb_to_use->estado = "Exit";
			pthread_mutex_lock(&mutex_exit_queue);
			queue_push(exit_queue,pcb_to_use);
			pthread_mutex_unlock(&mutex_exit_queue);
			send(sms->fd_consola,&page_counter,sizeof(int),0);
		}
	}
	if(numbytes != 0){perror("receive");}
}

void send_PCB(proceso_conexion *cpu, PCB *pcb){
	int tamanio_indice_codigo = (pcb->cantidad_de_instrucciones)*sizeof(entrada_indice_de_codigo);
	int tamanio_indice_stack = 1*sizeof(registroStack); //Esto es solo para probar
	//Creamos nuestro heroico buffer, quien se va a encargar de llevar el PCB a la CPU
	void *ultraBuffer = malloc(sizeof(int)*9 + sizeof(u_int32_t) + tamanio_indice_codigo+tamanio_indice_stack);

	memcpy(ultraBuffer, &(pcb->pid), sizeof(u_int32_t));
	memcpy(ultraBuffer+sizeof(u_int32_t), &(pcb->page_counter), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+sizeof(int), &(pcb->direccion_inicio_codigo), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+2*sizeof(int), &(pcb->program_counter), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+3*sizeof(int), &(pcb->cantidad_de_instrucciones), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+4*sizeof(int), &tamanio_indice_codigo, sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+5*sizeof(int), pcb->indice_de_codigo, tamanio_indice_codigo);
	memcpy(ultraBuffer+sizeof(u_int32_t)+5*sizeof(int)+tamanio_indice_codigo,&(pcb->tamanioStack),sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+6*sizeof(int)+tamanio_indice_codigo,&(pcb->primerPaginaStack),sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+7*sizeof(int)+tamanio_indice_codigo,&(pcb->stackPointer),sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+8*sizeof(int)+tamanio_indice_codigo, &tamanio_indice_stack, sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+9*sizeof(int)+tamanio_indice_codigo,pcb->stack_index,tamanio_indice_stack);


	send(cpu->sock_fd, ultraBuffer, sizeof(int)*9 + sizeof(u_int32_t) + tamanio_indice_codigo+tamanio_indice_stack,0);

	printf("Mande un PCB a una CPU :D\n\n");
	free(ultraBuffer);	//Cumpliste con tu mision. Ya eres libre.
}

void planificacion(int *grado_multiprog){
	while(1){
		//Si esta funcionando la planificacion
		if(plan_go){
			int i;
			//Me fijo si hay procesos en la cola New y si no llegue al tope de multiprog
			//Si es asi, pasa un proceso de New a Ready
			pthread_mutex_lock(&mutex_new_queue);

			if(queue_size(new_queue) > 0)
			{
				if(procesos_actuales < *grado_multiprog)
				{
					new_pcb* new = queue_pop(new_queue);
					guardado_en_memoria(new->sms,new->pcb);
					free(new);
				}
			}
			pthread_mutex_unlock(&mutex_new_queue);
			pthread_mutex_lock(&mutex_ready_queue);
			if(queue_size(ready_queue)>0){

				pthread_mutex_lock(&mutex_fd_cpus);
				i = buscar_cpu_libre();

				if(list_size(lista_cpus)>0 && i<list_size(lista_cpus)){
					proceso_conexion *cpu = list_get(lista_cpus,i);
					list_add(lista_en_ejecucion, cpu);
					PCB *pcb_to_use = queue_pop(ready_queue);

					send_PCB(cpu, pcb_to_use);

					pcb_to_use->estado = "Exec";
					pthread_mutex_lock(&mutex_exec_queue);
					queue_push(exec_queue,pcb_to_use);
					pthread_mutex_unlock(&mutex_exec_queue);
				}
				pthread_mutex_unlock(&mutex_fd_cpus);
			}
			pthread_mutex_unlock(&mutex_ready_queue);
		}
	}
}


void manejador_de_scripts(script_manager_setup* sms){
	PCB* pcb_to_use;

	//Creo el PCB
	char* script = (char*) sms->realbuf;
	pcb_to_use = create_PCB(script,sms->fd_consola);

	if(procesos_actuales <= sms->grado_multiprog)	
		guardado_en_memoria(sms, pcb_to_use);
	else{
		//Lo dejo en New sin guardar en memoria
		new_pcb* new = malloc(sizeof(new_pcb));
		new->pcb = pcb_to_use;
		new->sms = sms;
		pthread_mutex_lock(&mutex_new_queue);
		queue_push(new_queue,new);
		pthread_mutex_unlock(&mutex_new_queue);
		printf("El sistema ya llego a su tope de multiprogramacion.\nEl proceso sera guardado pero no podra ejecutarse hasta que termine otro.\n\n");
		int error = -1;
		send(sms->fd_consola, &error,sizeof(int),0);
	}
	free(sms);
}

void execute_write(int pid, int archivo, void* mensaje){
	if(archivo == 1){
		int i = 0;
		bool encontrado = false;
		while(i<list_size(lista_consolas) && !encontrado){
			proceso_conexion* aux = list_get(lista_consolas,i);
			if(pid == aux->proceso)
				//Le envio el mensaje a la consola :P
				encontrado = true;
			else
				i++;
		}
		if(!encontrado)
			printf("Error, no exista la consola en la que se quiere imprimir");
	}
	else printf("Not yet implemented");
}

//TODO el main

int main(int argc, char** argv) {
	if(argc == 1){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}
	//Variables para config
	t_config *config_file;
	int i=0;

	//Variables para conexiones con servidores
	char *buf = malloc(256);
	int sockfd_memoria;//, sockfd_fs;	//File descriptors de los sockets correspondientes a la memoria y al filesystem
	int bytes_mem;//, bytes_fs;

	//variables para conexiones con clientes
	int listener, fdmax, newfd, nbytes;
	fd_set read_fds;
	int codigo, processID;
	int messageLength;
	void* realbuf;
//	void* sendbuf;
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
	new_queue = queue_create();

	//Lista con todos los procesos
	todos_los_procesos = list_create();

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

	//Hilo menu + print command
	print_commands();
	pthread_t men;
	pthread_create(&men,NULL,(void*)menu,NULL);

	//Hilo planficacion
	pthread_t planif;
	pthread_create(&planif,NULL,(void*)planificacion,&(data_config.grado_multiprog));

	while(1) {
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
						remove_and_destroy_by_fd_socket(lista_cpus, i); //Lo sacamos de la lista de conexiones cpus y liberamos la memoria
						pthread_mutex_unlock(&mutex_fd_cpus);

						pthread_mutex_lock(&mutex_fd_consolas);
						remove_and_destroy_by_fd_socket(lista_consolas, i); //Lo sacamos de la lista de conexiones de consola y liberamos la memoria
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

							if(plan_go)
							{
								//Setteo el script manager
								script_manager_setup* sms = malloc(sizeof(script_manager_setup));
								sms->fd_consola = i;
								sms->fd_mem = sockfd_memoria;
								sms->grado_multiprog = data_config.grado_multiprog;
								sms->messageLength = messageLength;
								sms->realbuf = aux;

								manejador_de_scripts(sms);
							} else {
							printf("Lo sentimos, la planificacion no esta en funcionamiento\n\n");
							int error = -1;
							send(i, &error,sizeof(int),0); }
						}
						//Si el codigo es 3 significa que debo terminar el proceso
						if (codigo == 3)
						{
							//Cacheo el pid del proceso que tengo que borrar
							int pid;
							recv(i,&pid,sizeof(int),0);
							//Y llamo a la funcion que lo borra
							end_process(pid,sockfd_memoria,i);
						}
						//Si el codigo es 4 significa que quieren hacer un write
						if(codigo == 4)
						{
							int archivo, pid, messageLength;
							recv(i,&archivo,sizeof(int),0);
							recv(i,&pid,sizeof(int),0);
							recv(i,&messageLength,sizeof(int),0);
							void* message = malloc(messageLength+2);
							memset(message,0,messageLength+2);
							recv(i, message, messageLength, 0);
							memset(message+messageLength+1,'\0',1);

							execute_write(pid,archivo,message);
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
