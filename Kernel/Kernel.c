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
#include <parser/metadata_program.h>
#include "../shared_libs/PCB.h"

//Todo lo de mutex
pthread_mutex_t mutex_fd_cpus;
pthread_mutex_t mutex_fd_consolas;
pthread_mutex_t mutex_procesos_actuales;

//Los mutex para las colas
pthread_mutex_t mutex_ready_queue;
pthread_mutex_t mutex_exit_queue;
pthread_mutex_t mutex_exec_queue;
pthread_mutex_t mutex_new_queue;
pthread_mutex_t mutex_wait_queue;
pthread_mutex_t mutex_process_list;

// Structs de conexiones
//Todo lo de structs de PCB

// STRUCTS DE PCB
//VOLO: "TAL VEZ DEBERIAMOS PONERLOS EN UN .h"
//GIUSSA: "Tal vez deberias hacerlo :D"
//VOLO: "Seras rompehuevos eh"
//GIUSSA: "Si que te gusta comentar boludeces eh"
//VOLO: "Asi soy yo ¯\_(ツ)_/¯"
//GIUSSA: "Asi te quiero. Y justamente por estas cosas es que me encanta romperte los bolas"

//Si, agregue las otras cosas que va a tener el PCB como comentarios porque me da paja hacerlo
//despues. Asi que lo hago ahora ¯\_(ツ)_/¯

enum{
	HANDSHAKE = 1,
	ID_CPU = 1,
	ID_CONSOLA = 2,
	NUEVO_PROGRAMA = 2,
	ELIMINAR_PROCESO = 3,
	WRITE = 4,
	ASIGNAR_VALOR_COMPARTIDA = 5,
	BUSCAR_VALOR_COMPARTIDA = 6,
	WAIT = 7,
	SIGNAL = 8,
	DESCONEXION = 9,
	PROCESO_FINALIZADO_CORRECTAMENTE = 10,
	FILE_SIZE = 11,
	FILE_EXISTS = 12
};

typedef struct{
	PCB* pcb;
	script_manager_setup* sms;
}new_pcb;

//Si bien la estructura se llama variable_compartida,
//este tipo de dato es el mismo que vamos a usar para los semaforos ya que poseen los mismo atributos
//La idea de esto es no repetir ni logica ni codigo.

typedef struct{
	char* ID;
	u_int32_t valor;
}variable_compartida;

typedef variable_compartida semaforo;

typedef struct{
	semaforo* sem;
	t_queue* cola_de_bloqueados;
} semaforo_cola;

//Todo lo de variables globales
int pid = 0;
int mem_sock;
int listener_cpu;
int fdmax_cpu;
int procesos_actuales = 0; //La uso para ver que no haya mas de lo que la multiprogramacion permite
bool plan_go = true;

fd_set fd_procesos;

kernel_config data_config;

uint32_t tamanio_pagina;

//Lista de conexiones (Cpus y Consolas)
t_list* lista_cpus;
t_list* lista_consolas;
t_list* lista_en_ejecucion;

//Lista de Variables y Semaforos Ansisop
t_list* variables_compartidas;
t_list* semaforos;

//Colas de planificacion
t_queue* ready_queue;
t_queue* exit_queue;
t_queue* exec_queue;
t_queue* new_queue;

//Listado con los procesos del sistema
t_list* todos_los_procesos;

//FUNCIONES

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

void print_PCB(PCB* pcb){
	int i;
	printf("\nPID: %d\n", pcb->pid);
	printf("page_counter: %d\n", pcb->page_counter);
	printf("direccion_inicio_codigo: %d\n", pcb->direccion_inicio_codigo);
	printf("program_counter: %d\n", pcb->program_counter);
	printf("cantidad_de_instrucciones: %d\n", pcb->cantidad_de_instrucciones);
	printf("primer pagina del stack: %d\n", pcb->primerPaginaStack);
	printf("stack pointer: %d\n", pcb->stackPointer);
	printf("Stack size: %d\n\n", pcb->tamanioStack);
	for(i=0; i<pcb->cantidad_de_instrucciones; i++){
		printf("Instruccion %d: Inicio = %d, Offset = %d\n", i, pcb->indice_de_codigo[i].inicio, pcb->indice_de_codigo[i].offset);
	}
	printf("\n");
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


	nuevo_PCB->tamanioStack=data_config.stack_size;
	nuevo_PCB->stackPointer=0;
	nuevo_PCB->stack_index=list_create();

	print_metadata(metadata);

	pthread_mutex_lock(&mutex_process_list);
	list_add(todos_los_procesos,nuevo_PCB);
	pthread_mutex_unlock(&mutex_process_list);
	pthread_mutex_lock(&mutex_procesos_actuales);
	procesos_actuales++;
	pthread_mutex_unlock(&mutex_procesos_actuales);

	pthread_mutex_lock(&mutex_fd_consolas);
	proceso_conexion *consola = remove_by_fd_socket(lista_consolas,fd_consola);
	consola->proceso = nuevo_PCB->pid;
	list_add(lista_consolas, consola);
	pthread_mutex_unlock(&mutex_fd_consolas);

	return nuevo_PCB;
}

PCB *remove_and_get_PCB(int processid,t_queue* queue){
	int i = 0, len;
	t_list* lista_auxiliar = list_create();
	len = queue_size(queue);
	PCB *toReturn;
	while(i<len){
		//Saco un PCB y me fijo si es el que busco
		PCB* aux = queue_pop(queue);
		if(!(aux->pid == processid))
		{
			//Si no es, lo ubico en una lista auxiliar y aumento i para seguir buscando
			list_add(lista_auxiliar,aux);
			//Si lo es, no hago nada, mi objetivo era sacarlo y ya lo hice
		}
		else{
			toReturn = aux;
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
	return toReturn;
}

//Esta funcion es un quilombo asi que la explico aca
//No hay manera de sacar un objeto de un queue sin sacar todos los que tiene adelante
void remove_PCB_from_specific_queue(int processid,t_queue* queue){
	int i = 0, len;
	t_list* lista_auxiliar = list_create();
	len = queue_size(queue);
	while(i<len){
		//Saco un PCB y me fijo si es el que busco
		PCB* aux = queue_pop(queue);
		if(!(aux->pid == processid))
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
		remove_PCB_from_specific_queue(pcb->pid,new_queue);
		pthread_mutex_unlock(&mutex_new_queue);
	}
	else if(strcmp(pcb->estado,"Ready")==0)
	{
		pthread_mutex_lock(&mutex_ready_queue);
		remove_PCB_from_specific_queue(pcb->pid,ready_queue);
		pthread_mutex_unlock(&mutex_ready_queue);
	}
	else if(strcmp(pcb->estado,"Exec")==0)//No va a funcionar asi por siempre pero lo dejo como placeholder
	{
		pthread_mutex_lock(&mutex_exec_queue);
		remove_PCB_from_specific_queue(pcb->pid,exec_queue);
		pthread_mutex_unlock(&mutex_exec_queue);
	}
	else printf("PCB invalido, no se encuentra en ninguna cola");
}

int buscar_consola_de_proceso(int processid){
	pthread_mutex_lock(&mutex_fd_consolas);
	int i = 0, dimension = list_size(lista_consolas), res = -1;
	while(i < dimension){
		proceso_conexion* aux = list_get(lista_consolas,i);
		if(aux->proceso == processid){
			res = aux->sock_fd;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_fd_consolas);
	return res;
}

void end_process(int PID, int socket_memoria, int exit_code, int sock_consola){
	int i = 0;
	bool encontrado = 0;
	pthread_mutex_lock(&mutex_process_list);
	while(i<list_size(todos_los_procesos) && !encontrado)
	{
		PCB* PCB = list_get(todos_los_procesos,i);
		if(PID == PCB->pid)	{
			if(strcmp(PCB->estado,"Exit")!=0){
				remove_from_queue(PCB);
				PCB->exit_code = exit_code;
				PCB->estado = "Exit";
				delete_PCB(PCB);
				printf("El proceso ha sido finalizado\n");
				encontrado = 1;
			}
			else{
				printf("El proceso elegido ya esta finalizado\n");
			}
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_process_list);
	if(!encontrado)	{
		printf("El PID seleccionado todavia no ha sido asignado a ningun proceso\n");
		void* sendbuf_consola_mensajera = malloc(sizeof(uint32_t));
		//Lo mando 0 para que sepa que no se pudo borrar el proceso
		uint32_t codigo_de_cancelado_no_ok = 0;
		memcpy(sendbuf_consola_mensajera,&codigo_de_cancelado_no_ok,sizeof(uint32_t));
		send(sock_consola,sendbuf_consola_mensajera,sizeof(uint32_t)*2,0);
	}
	if(encontrado){
		//Le aviso a memoria que le saque las paginas asignadas
		void* sendbuf_mem = malloc(sizeof(uint32_t)*2);
		uint32_t codigo_para_borrar_paginas = 5;
		memcpy(sendbuf_mem,&codigo_para_borrar_paginas,sizeof(uint32_t));
		memcpy(sendbuf_mem+sizeof(uint32_t),&PID,sizeof(uint32_t));
		send(socket_memoria,sendbuf_mem,sizeof(uint32_t)*2,0);

		//Y le aviso a la consola que se aborto el proceso
		void* sendbuf_consola = malloc(sizeof(uint32_t));
		uint32_t codigo_para_abortar_proceso = 7;
		int consola = buscar_consola_de_proceso(PID);
		memcpy(sendbuf_consola,&codigo_para_abortar_proceso,sizeof(uint32_t));
		send(consola,sendbuf_consola,sizeof(uint32_t)*2,0);

		//Y al hilo que me mando el mensaje que fue lo que paso
		void* sendbuf_consola_mensajera = malloc(sizeof(uint32_t));
		//Le mando 1 para que sepa que se pudo borrar
		uint32_t codigo_de_cancelado_ok = 1;
		memcpy(sendbuf_consola_mensajera,&codigo_de_cancelado_ok,sizeof(uint32_t));
		send(sock_consola,sendbuf_consola_mensajera,sizeof(uint32_t)*2,0);
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
	printf("\t list    - Lista de Procesos\n");
	printf("\t state   - Estado de un Proceso\n");
	printf("\t plan    - Detener/Reanudar Planificacion\n");
	printf("\t f_exist - Existencia de un archivo\n");
	printf("\t f_size  - Dimension de un archivo\n\n");
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
				printf("Estado: %s\n", PCB->estado);
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

void file_handler(int sockfs, int tipo){
	//Pongo los tipos de codigo que puedo mandar
	uint32_t codigo_size = FILE_SIZE;
	uint32_t codigo_exists = FILE_EXISTS;
	uint32_t codigo_fail = -20;

	//Por ahora cacheo el nombre del archivo por pantalla
	char* archivo = malloc(100);
	printf("Ingrese el nombre del archivo: ");
	scanf("%s",archivo);
	uint32_t dimension = strlen(archivo);

	void* buffer = malloc(sizeof(uint32_t)*2+dimension);

	//Meto el codigo segun corresponda
	switch(tipo){
		case FILE_SIZE:
			memcpy(buffer,&codigo_size,sizeof(uint32_t));
			printf("Codigo: %d\n", codigo_size);
			break;
		case FILE_EXISTS:
			memcpy(buffer,&codigo_exists,sizeof(uint32_t));
			printf("Codigo: %d\n", codigo_exists);
			break;
		default:
			memcpy(buffer,&codigo_fail,sizeof(uint32_t));
			break;
	}

	//Meto el resto del mensaje
	memcpy(buffer+sizeof(uint32_t),&dimension,sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2,archivo,sizeof(uint32_t));

	printf("Dimension: %d\n", dimension);
	printf("Archivo: %s\n", archivo);

	//Mando el mensaje y espero la respuesta
	send(sockfs,buffer,sizeof(uint32_t)*2+dimension,0);
	uint32_t respuesta = 0;
	recv(sockfs,&respuesta,sizeof(uint32_t),0);

	//Dependiendo del tipo de operacion respondo de manera distinta
	switch(tipo){
			case FILE_SIZE:
				if(respuesta > 0)
					printf("El archivo %s tiene un tamaño de %d\n",archivo,respuesta);
				if(respuesta == 0)
					printf("Algo raro paso, probablemente se haya desconetado FS\n");
				break;
			case FILE_EXISTS:
				if(respuesta == true)
					printf("El archivo %s existe\n",archivo);
				if(respuesta == false)
					printf("El archivo %s no existe\n",archivo);
				break;
			default:
				break;
		}
	if(respuesta == -10) printf("Algo salio muy mal\n");
	printf("\n");
}


void menu(int* sockfs)
{
	while(1)
	{
		char* command = malloc(20);
		scanf("%s", command);
		if((strcmp(command, "list")) == 0){
			print_PCB_list();
		}
		else if((strcmp(command, "state")) == 0){
			pcb_state();
		}
		else if((strcmp(command, "plan")) == 0){
			if(plan_go){
				plan_go=false;
				printf("Se ha detenido la planificacion\n\n");
			}
			else{
				plan_go=true;
				printf("Se ha reanudado la planificacion\n\n");
			}
		}
		else if((strcmp(command, "f_exist") == 0)){
			file_handler(*sockfs,FILE_EXISTS);
		}
		else if((strcmp(command, "f_size") == 0)){
				file_handler(*sockfs,FILE_SIZE);
		}
		else{
			printf("Comando incorrecto. Ingrese otro comando: \n");
			continue;
		}
		free(command);
	}
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

//Funciones de Capa Memoria

void settear_variables_Ansisop(){
	int i = 0;
	int j = 0;

	while(data_config.shared_vars[i]!=NULL){
		variable_compartida *unaVar = malloc(sizeof(variable_compartida));
		unaVar->ID = data_config.shared_vars[i]+1;
		unaVar->valor = 0;
		list_add(variables_compartidas, unaVar);
		i++;
	}

	while(data_config.sem_ids[j] != NULL){
		semaforo *unSem = malloc(sizeof(semaforo));
		unSem->ID = data_config.sem_ids[j];
		unSem->valor = data_config.sem_init[j];
		semaforo_cola* el_semaforo_posta = malloc(sizeof(semaforo_cola));
		t_queue* la_cola = queue_create();
		el_semaforo_posta->sem = unSem;
		el_semaforo_posta->cola_de_bloqueados = la_cola;
		list_add(semaforos, el_semaforo_posta);
		j++;
	}
}

void print_vars(){
	int max_var = list_size(variables_compartidas);
	int max_sem = list_size(semaforos);
	int i = 0, j=0;
	printf("Variables Compartidas:\n");
	while(i < max_var){
		variable_compartida *unaVar = list_get(variables_compartidas, i);
		printf("Variable %s, valor = %d\n", unaVar->ID, unaVar->valor);
		i++;
	}
	while(j < max_sem){
		semaforo_cola *unSem = list_get(semaforos, j);
		printf("Semaforo %s, valor = %d\n", unSem->sem->ID, unSem->sem->valor);
		j++;
	}
}
//Es homogenea tanto para semaforos como para variables_compartidas
variable_compartida *remove_by_ID(t_list *lista, char* ID){
	bool _remove_element(void* list_element)
	    {
		variable_compartida *unaVar = (variable_compartida*) list_element;
		return strcmp(ID, unaVar->ID)==0;
	    }
	variable_compartida* variable_buscada =  list_remove_by_condition(lista,*_remove_element);
	return variable_buscada;
}

semaforo_cola *remove_semaforo_by_ID(t_list *lista, char* ID){
	bool _remove_element(void* list_element)
	    {
		semaforo_cola *unSem = (semaforo_cola *) list_element;
		return strcmp(ID, unSem->sem->ID)==0;
	    }
	semaforo_cola *semaforo_buscada = list_remove_by_condition(lista, *_remove_element);
	return semaforo_buscada;
}

void asignar_valor_variable_compartida(char* ID, u_int32_t value){
	variable_compartida *variable_a_modificar = remove_by_ID(variables_compartidas, ID);
	variable_a_modificar->valor = value;
	list_add(variables_compartidas, variable_a_modificar);
}

u_int32_t obtener_valor_variable_compartida(char* ID){
	u_int32_t value;
	variable_compartida *variable_a_modificar = remove_by_ID(variables_compartidas, ID);
	value = variable_a_modificar->valor;
	list_add(variables_compartidas, variable_a_modificar);
	return value;
}

void wait(char *id_semaforo, uint32_t PID){
	semaforo_cola *unSem = remove_semaforo_by_ID(semaforos, id_semaforo);

	pthread_mutex_lock(&mutex_exec_queue);
	PCB *unPCB = remove_and_get_PCB(PID, exec_queue);
	pthread_mutex_unlock(&mutex_exec_queue);

	unSem->sem->valor = unSem->sem->valor - 1;

	if(unSem->sem->valor < 0){
		unPCB->estado = "Blocked";
		queue_push(unSem->cola_de_bloqueados, unPCB);
	}

	list_add(semaforos, unSem);
}

void signal(char *id_semaforo){
	semaforo_cola *unSem = remove_semaforo_by_ID(semaforos, id_semaforo);

	unSem->sem->valor = unSem->sem->valor + 1;

	if(unSem->sem->valor <= 0){
		PCB *unPCB = queue_pop(unSem->cola_de_bloqueados);
		unPCB->estado = "Ready";

		pthread_mutex_lock(&mutex_ready_queue);
		queue_push(ready_queue, unPCB);
		pthread_mutex_unlock(&mutex_ready_queue);
	}

	list_add(semaforos, unSem);
}

//Fin de funciones de capa memoria

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
	int numbytes, page_counter, direccion;

	//Le mando el codigo y el largo a la memoria
	//INICIO SERIALIZACION PARA MEMORIAAAAA
	sendbuf = (void*) PCB_cereal(sms,pcb_to_use,&(data_config.stack_size),MEMPCB);
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
			pcb_to_use->primerPaginaStack=page_counter-data_config.stack_size; //pagina donde arranca el stack
			pcb_to_use->direccion_inicio_codigo = direccion;
			pcb_to_use->estado = "Ready";
			pthread_mutex_lock(&mutex_ready_queue);
			queue_push(ready_queue,pcb_to_use);
			pthread_mutex_unlock(&mutex_ready_queue);
			send(sms->fd_consola,&pcb_to_use->pid,sizeof(uint32_t),0);
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

PCB* recibirPCB(uint32_t fd_socket)
{

	u_int32_t pid;

	uint32_t page_counter, direccion_inicio_codigo, program_counter, cantidad_de_instrucciones,
	stack_size, primerPagStack, stack_pointer, codigo;

	PCB* pcb = malloc(sizeof(PCB));

	uint32_t tamanio_indice_codigo, tamanio_indice_etiquetas;
	uint32_t cantRegistros;

	recv(fd_socket, &pid, sizeof(u_int32_t),0);

	recv(fd_socket, &page_counter, sizeof(uint32_t),0);

	recv(fd_socket, &direccion_inicio_codigo, sizeof(uint32_t),0);

	recv(fd_socket, &program_counter, sizeof(uint32_t),0);

	recv(fd_socket, &cantidad_de_instrucciones, sizeof(uint32_t),0);

	recv(fd_socket, &tamanio_indice_codigo, sizeof(uint32_t),0);


	entrada_indice_de_codigo *indice_de_codigo = malloc(tamanio_indice_codigo);


	recv(fd_socket, indice_de_codigo, tamanio_indice_codigo,0);


	recv(fd_socket, &stack_size,sizeof(uint32_t),0);

	recv(fd_socket, &primerPagStack,sizeof(uint32_t),0);

	recv(fd_socket, &stack_pointer,sizeof(uint32_t),0);


	recv(fd_socket, &cantRegistros, sizeof(uint32_t),0);


	/*t_list* indice_de_stack = malloc(tamanio_indice_stack);

	bytes_recv = recv(fd_kernel, indice_de_stack, tamanio_indice_stack,0);
	verificar_conexion_socket(fd_kernel,bytes_recv);*/

	//recibo indice etiquetas:

	printf("\nRECIBI TODO MENOS ETIQUETAS\n");

	recv(fd_socket, &tamanio_indice_etiquetas, sizeof(uint32_t),0);


	printf("RECIBI TAMANIO ETIQUETAS Y ES: %d\n", tamanio_indice_etiquetas);

	char* indice_de_etiquetas = malloc(tamanio_indice_etiquetas);

	if(tamanio_indice_etiquetas>0)
	{
	recv(fd_socket, indice_de_etiquetas, tamanio_indice_etiquetas,0);


	printf("RECIBI indice ETIQUETAS\n\n");

	}


	//-----Recibo indice de Stack-----

	pcb->stack_index = list_create();
	/*if(cantRegistros==0)
	{
		printf("Cantidad de registros: %d", cantRegistros);
		cantRegistros=1; //Esto es para que entre al while (aunque no reciba nada) entonces hace
						 //list_create para vars y args (no se me ocurrio otra manera todavia)
	}*/

	int registrosAgregados = 0;

	int cantArgumentos, cantVariables;

	if(cantRegistros>0){


	while(registrosAgregados < cantRegistros)
	{
		recv(fd_socket, &cantArgumentos, sizeof(int),0);

		printf("cant argums: %d\n", cantArgumentos);
		registroStack* nuevoReg = malloc(sizeof(registroStack));

		nuevoReg->args = list_create();

		if(cantArgumentos>0) //Si tiene argumentos
		{
		//Recibo argumentos:

		int argumentosAgregados = 0;

		while(argumentosAgregados < cantArgumentos)
		{
			variable *nuevoArg = malloc(sizeof(variable));

			recv(fd_socket, &(nuevoArg->id), sizeof(char),0);

			recv(fd_socket, &(nuevoArg->offset), sizeof(int),0);

			recv(fd_socket, &(nuevoArg->page), sizeof(int),0);

			recv(fd_socket, &(nuevoArg->size), sizeof(int),0);


			list_add(nuevoReg->args, nuevoArg);
			argumentosAgregados++;
		}
		} //Fin recepcion argumentos

		recv(fd_socket, &cantVariables, sizeof(int),0);


		nuevoReg->vars= list_create();
		printf("cant vars: %d\n", cantVariables);
		if(cantVariables>0) //Si tiene variables
		{
		//Recibo variables:

		int variablesAgregadas = 0;

		while(variablesAgregadas < cantVariables)
		{
			variable *nuevaVar = malloc(sizeof(variable));

			recv(fd_socket, &(nuevaVar->id), sizeof(char),0);

			recv(fd_socket, &(nuevaVar->offset), sizeof(int),0);

			recv(fd_socket, &(nuevaVar->page), sizeof(int),0);

			recv(fd_socket, &(nuevaVar->size), sizeof(int),0);


			list_add(nuevoReg->vars, nuevaVar);
			variable* primeraVar = list_get(nuevoReg->vars,variablesAgregadas);
			printf("(id,offset,page,size): (%c,%d,%d,%d,)\n",primeraVar->id,primeraVar->offset,primeraVar->page,primeraVar->size);

			variablesAgregadas++;

		}
		} //Fin recepcion variables

		//Recibo retPos

		recv(fd_socket, &(nuevoReg->ret_pos), sizeof(int),0);


		//Recibo retVar

		recv(fd_socket, &(nuevoReg->ret_var.offset), sizeof(int),0);

		recv(fd_socket, &(nuevoReg->ret_var.page), sizeof(int),0);

		recv(fd_socket, &(nuevoReg->ret_var.size), sizeof(int),0);

		list_add(pcb->stack_index, nuevoReg);

		registrosAgregados++;


	}//Fin recepcion Stack
	}

	pcb->pid = pid;
	pcb->page_counter = page_counter;
	pcb->lista_de_etiquetas_length=tamanio_indice_etiquetas;
	pcb->lista_de_etiquetas=indice_de_etiquetas;
	pcb->direccion_inicio_codigo = direccion_inicio_codigo;
	pcb->program_counter = program_counter;
	pcb->cantidad_de_instrucciones = cantidad_de_instrucciones;
	pcb->indice_de_codigo = indice_de_codigo;
	pcb->tamanioStack = stack_size;
	pcb->primerPaginaStack=primerPagStack;
	pcb->stackPointer=stack_pointer;
	//pcb->stack_index=indice_de_stack;

	return pcb;
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

					uint32_t codigo = 10;
					send_PCB(cpu->sock_fd, pcb_to_use, codigo);

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
	int sockfd_memoria, sockfd_fs;	//File descriptors de los sockets correspondientes a la memoria y al filesystem
	int bytes_mem, bytes_fs;

	//variables para conexiones con clientes
	int listener, fdmax, newfd, nbytes;
	fd_set read_fds;
	int codigo, processID;
	int messageLength;

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

	//Variables_compartidas y semaforos
	variables_compartidas = list_create();
	semaforos = list_create();

	FD_ZERO(&fd_procesos);
	FD_ZERO(&read_fds);

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../Kernel/") + strlen(argv[1])+1);
	*cfgPath = '\0';
	strcpy(cfgPath, "../../Kernel/");

	config_file = config_create_from_relative_with_check(argv, cfgPath);
	cargar_config(config_file);	//Carga la estructura data_config de Kernel
	print_config();	//Adivina... la imprime por pantalla
	settear_variables_Ansisop();//todo
	print_vars();

	free(cfgPath);

	//********************************Conexiones***************************************//

	//Servidores
	sockfd_memoria = get_fd_server(data_config.ip_memoria,data_config.puerto_memoria);		//Nos conectamos a la memoria
	sockfd_fs= get_fd_server(data_config.ip_fs,data_config.puerto_fs);		//Nos conectamos al fs

	int handshake = HANDSHAKE;
	int resp;
	send(sockfd_memoria, &handshake, sizeof(u_int32_t), 0);
	bytes_mem = recv(sockfd_memoria, &resp, sizeof(u_int32_t), 0);
	if(bytes_mem > 0 && resp > 0){
				printf("Conectado con Memoria\n");
				tamanio_pagina = resp;
				printf("Tamaño de pagina = %d\n", resp);
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

	resp = 0;
	send(sockfd_fs, &handshake, sizeof(u_int32_t), 0);
	bytes_fs = recv(sockfd_fs, &resp, sizeof(u_int32_t), 0);
	if(bytes_fs > 0 && resp == 1){
		printf("Conectado con FS\n");
	}
	else{
		if(bytes_mem == -1){
			perror("recieve");
			exit(3);
		}
		if(bytes_mem == 0){
			printf("Se desconecto el socket: %d\n", sockfd_fs);
			close(sockfd_fs);
		}
	}

	//Consolas y CPUs
	listener = get_fd_listener(data_config.puerto_prog);

	FD_SET(listener, &fd_procesos);

	fdmax = listener;

	//Hilo menu + print command
	print_commands();
	pthread_t men;
	pthread_create(&men,NULL,(void*)menu,&sockfd_fs);

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
						if(codigo == HANDSHAKE){
							recv(i, &processID,sizeof(int),0);
							if(processID == ID_CPU){	//Si el processID es 1, sabemos que es una CPU
								printf("Es una CPU\n");
								nueva_conexion_cpu = malloc(sizeof(proceso_conexion));
								nueva_conexion_cpu->sock_fd = newfd;
								pthread_mutex_lock(&mutex_fd_cpus);
								list_add(lista_cpus, nueva_conexion_cpu);
								pthread_mutex_unlock(&mutex_fd_cpus);

								printf("Hay %d cpus conectadas\n\n", list_size(lista_cpus));

							}
							if(processID == ID_CONSOLA){	//Si en cambio el processID es 2, es una Consola
								printf("Es una Consola\n");
								nueva_conexion_consola = malloc(sizeof(proceso_conexion));
								nueva_conexion_consola->sock_fd = newfd;
								pthread_mutex_lock(&mutex_fd_consolas);
								list_add(lista_consolas, nueva_conexion_consola);
								pthread_mutex_unlock(&mutex_fd_consolas);

								printf("Hay %d consolas conectadas\n\n", list_size(lista_consolas));
							}
						}//Si el codigo es 2, significa que del otro lado estan queriendo mandar un programa ansisop
						if(codigo == NUEVO_PROGRAMA){
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
						if (codigo == ELIMINAR_PROCESO)
						{
							//Cacheo el pid del proceso que tengo que borrar
							int pid;
							recv(i,&pid,sizeof(int),0);
							//Y llamo a la funcion que lo borra
							end_process(pid,sockfd_memoria,-7,i);
						}
						//Si el codigo es 4 significa que quieren hacer un write
						if(codigo == WRITE)
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
						if(codigo == ASIGNAR_VALOR_COMPARTIDA){
							u_int32_t value, tamanio;
							recv(i, &value, sizeof(u_int32_t),0);
							recv(i, &tamanio, sizeof(u_int32_t),0);

							char *id_var = malloc(tamanio);
							recv(i, id_var, tamanio,0);

							printf("Hay que modificar la variable %s con el valor %d\n", id_var, value);

							asignar_valor_variable_compartida(id_var, value);
							print_vars();
						}
						if(codigo == BUSCAR_VALOR_COMPARTIDA){
							u_int32_t value, tamanio;
							recv(i, &tamanio, sizeof(u_int32_t),0);

							char *id_var = malloc(tamanio);
							recv(i, id_var, tamanio,0);

							printf("Hay que obtener el valor de la variable %s\n", id_var);

							value = obtener_valor_variable_compartida(id_var);

							printf("La variable %s tiene valor %d\n", id_var, value);

							send(i, &value, sizeof(u_int32_t), 0);
						}
						//Si el codigo es 50, significa que CPU me mando que necesita hacer WAIT
						//Y WAIT  es una operacion privilegiada, solo yo, kernel, la puedo hacer ;)
						if (codigo==WAIT)
						{
							uint32_t PID;
							recv(i,&PID, sizeof(uint32_t),0);
							recv(i, &messageLength , sizeof(uint32_t), 0);
							char *id_sem = malloc(messageLength);

							recv(i, id_sem, messageLength, 0);

							printf("CPU pide: Wait en semaforo: %s\n\n", id_sem);

							wait(id_sem, PID);

							print_vars();

							free(id_sem);
						}
						if(codigo==SIGNAL){
							recv(i, &messageLength , sizeof(uint32_t), 0);

							char *id_sem = malloc(messageLength);

							recv(i, id_sem, messageLength, 0);

							printf("CPU pide: Signal en semaforo: %s \n\n", id_sem);

							signal(id_sem);

							free(id_sem);
						}
						//Si recibo 9 significa que se desconecto la consola
						if(codigo==DESCONEXION){
							//Cacheo el pid del proceso que tengo que borrar
							int pid;
							recv(i,&pid,sizeof(int),0);
							//Y llamo a la funcion que lo borra
							end_process(pid,sockfd_memoria,-6,i);
						}
						if(codigo == PROCESO_FINALIZADO_CORRECTAMENTE){
							PCB *unPCB = recibirPCB(i);
							print_PCB(unPCB);
							unPCB->estado = "exit";
							proceso_conexion *unaCPU = remove_by_fd_socket(lista_en_ejecucion, i);

							uint32_t PID = unPCB->pid;

							pthread_mutex_lock(&mutex_exec_queue);
							remove_PCB_from_specific_queue(PID, exec_queue);
							pthread_mutex_unlock(&mutex_exec_queue);

							pthread_mutex_lock(&mutex_exit_queue);
							queue_push(exit_queue, unPCB);
							pthread_mutex_unlock(&mutex_exit_queue);

							int fd_consola = buscar_consola_de_proceso(unPCB->pid);
							uint32_t finalizado = 6;
							send(fd_consola, &finalizado, sizeof(uint32_t),0);

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
