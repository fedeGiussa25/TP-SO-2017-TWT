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
#include <stdbool.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/log.h>
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
#include <errno.h>

//Mutex de listas de conexion
pthread_mutex_t mutex_fd_cpus;
pthread_mutex_t mutex_fd_consolas;

//Mutex de listas de procesos
pthread_mutex_t mutex_procesos_actuales;
pthread_mutex_t mutex_in_exec;
pthread_mutex_t mutex_process_list;
pthread_mutex_t mutex_to_delete;

//Mutex de capa de memoria
pthread_mutex_t mutex_semaforos_ansisop;
pthread_mutex_t mutex_vCompartidas_ansisop;

//Mutex de capa de FS
pthread_mutex_t mutex_archivos_x_proceso;
pthread_mutex_t mutex_archivos_globales;

//Los mutex para las colas
pthread_mutex_t mutex_ready_queue;
pthread_mutex_t mutex_exit_queue;
pthread_mutex_t mutex_exec_queue;
pthread_mutex_t mutex_new_queue;
pthread_mutex_t mutex_wait_queue;

//Mutex para lista de datos
pthread_mutex_t mutex_lista_datos;

//Mutex para evitar espera activa
pthread_mutex_t mutex_planificacion;


// Structs de conexiones
//Todo lo de structs de PCB

enum{
	HANDSHAKE = 1,
	ID_CPU = 1,
	ID_CONSOLA = 2,
	NUEVO_PROGRAMA = 2,
	FD_INICIAL_ARCHIVOS = 3,
	ELIMINAR_PROCESO = 3,
	ASIGNAR_VALOR_COMPARTIDA = 5,
	BUSCAR_VALOR_COMPARTIDA = 6,
	WAIT = 7,
	SIGNAL = 8,
	DESCONEXION = 9,
	PROCESO_FINALIZADO_CORRECTAMENTE = 10,
	FILE_SIZE = 11,
	FILE_EXISTS = 12,
	FIN_DE_RAFAGA = 13,
	PROCESO_FINALIZO_ERRONEAMENTE = 22,
//Acciones sobre archivos
	ABRIR_ARCHIVO = 14,
	LEER_ARCHIVO = 15,
	ESCRIBIR_ARCHIVO = 16,
	CERRAR_ARCHIVO = 17,
	BORRAR_ARCHIVO = 20,
	MOVER_CURSOR = 21,
//Permisos de archivos
	READ = 1,
	WRITE = 2,
	CREATE = 3,
	READ_WRITE = 4,
	READ_CREATE = 5,
	WRITE_CREATE = 6,
	READ_WRITE_CREATE = 7,
//Acciones sobre el Heap
	RESERVAR_MEMORIA = 18,
	LIBERAR_MEMORIA = 19,
	SOLICITAR_HEAP = 7,
	ALOCAR = 8,
	LIBERAR = 9,
//Codigos comunicacion FS
	BUSCAR_ARCHIVO_FS = 11,
	CREAR_ARCHIVO_FS = 12,
	LEER_ARCHIVO_FS = 13,
	ESCRIBIR_ARCHIVO_FS = 14
};

typedef struct{
	PCB* pcb;
	script_manager_setup* sms;
}new_pcb;

typedef struct{
	uint32_t proceso;
	uint32_t consola_askeadora;
	int exit_code;
	bool connection;
}just_a_pid;

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

typedef struct{
	uint32_t size;
	uint32_t direccion;
	uint32_t pagina;
} puntero;

typedef struct{
	uint32_t pid;
	t_list* heap;
} heap_de_proceso;

typedef struct{
	uint32_t fd;
	t_banderas* flags;
	uint32_t referencia_a_tabla_global;
	uint32_t offset;
}archivo_de_proceso;

//tabla_de_archivos_de_proceso es la tabla que tiene UN proceso de sus archivos
typedef struct{
	uint32_t pid;
	t_list* lista_de_archivos;//Cada elemento es un "archivo_de_proceso" (Uno por cada archivo abierto por el proceso)
	uint32_t current_fd;
} tabla_de_archivos_de_proceso;

typedef struct{
	char* ruta_del_archivo;
	int32_t instancias_abiertas;
} archivo_global;

typedef struct{
	int id;
	int syscalls;
	int rafagas;
	int op_alocar;
	int bytes_alocar;
	int op_liberar;
	int bytes_liberar;
} datos_proceso;

//Todo lo de variables globales
int pid = 0;
int mem_sock;
int listener_cpu;
int fdmax_cpu;
int procesos_actuales = 0; //La uso para ver que no haya mas de lo que la multiprogramacion permite
bool plan_go = true;
t_log *kernelLog;

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

//Lista de Heaps de Procesos
t_list* heap;

//Colas de planificacion
t_queue* ready_queue;
t_queue* exit_queue;
t_queue* exec_queue;
t_queue* new_queue;

//Listado con los procesos del sistema
t_list* todos_los_procesos;

//Listado de procesos a borrar
t_list* procesos_a_borrar;

//Listado de tablas de archivos
t_list* tabla_de_archivos_por_proceso; //Cada elemento es una "tabla_de_archivos_de_proceso" (Uno por cada proceso)
t_list* tabla_global_de_archivos;

//Listado de datos
t_list* lista_datos_procesos;

//Lista de PCBs previos
t_list* PCBs_usados;

//Sockets Memoria y FS
int sockfd_memoria, sockfd_fs;

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

	printf("Se imprimio la informacion de metadata en el log\n");
	log_info(kernelLog, "\n\n***INFORMACION DE METADATA***\n");
	log_info(kernelLog, "instruccion_inicio = %d\n", metadata->instruccion_inicio);
	log_info(kernelLog, "instrucciones_size = %d\n", metadata->instrucciones_size);

	log_info(kernelLog, "Instrucciones serializadas: \n");

	for(i=0; i<(metadata->instrucciones_size); i++){
		log_info(kernelLog, "Instruccion %d: Inicio = %d, Offset = %d\n", i, metadata->instrucciones_serializado[i].start, metadata->instrucciones_serializado[i].offset);
	}

	log_info(kernelLog, "etiquetas_size = %d\n", metadata->etiquetas_size);
	log_info(kernelLog, "etiquetas = %s\n", metadata->etiquetas);

	log_info(kernelLog, "cantidad_de_funciones = %d\n", metadata->cantidad_de_funciones);
	log_info(kernelLog, "cantidad_de_etiquetas = %d\n", metadata->cantidad_de_etiquetas);
	log_info(kernelLog, "***FIN DE LA METADATA***\n\n");
}


void print_PCB(PCB* pcb){
	int i;

	printf("Se imprimieron los datos de un pcb en el log\n");

	log_info(kernelLog, "\nPID: %d\n", pcb->pid);
	log_info(kernelLog, "page_counter: %d\n", pcb->page_counter);
	log_info(kernelLog, "direccion_inicio_codigo: %d\n", pcb->direccion_inicio_codigo);
	log_info(kernelLog, "program_counter: %d\n", pcb->program_counter);
	log_info(kernelLog, "cantidad_de_instrucciones: %d\n", pcb->cantidad_de_instrucciones);
	log_info(kernelLog, "primer pagina del stack: %d\n", pcb->primerPaginaStack);
	log_info(kernelLog, "stack pointer: %d\n", pcb->stackPointer);
	log_info(kernelLog, "Stack size: %d\n\n", pcb->tamanioStack);
	for(i=0; i<pcb->cantidad_de_instrucciones; i++){
		log_info(kernelLog, "Instruccion %d: Inicio = %d, Offset = %d\n", i, pcb->indice_de_codigo[i].inicio, pcb->indice_de_codigo[i].offset);
	}
	log_info(kernelLog, "\n");
}

void print_PCB2(PCB* pcb){
	int i, j=0, k, l;
	printf("pid: %d\n", pcb->pid);
	printf("page_counter: %d\n", pcb->page_counter);
	printf("direccion_inicio_de_codigo: %d\n", pcb->direccion_inicio_codigo);
	printf("program_counter: %d\n", pcb->program_counter);
	printf("cantidad_de_instrucciones: %d\n", pcb->cantidad_de_instrucciones);
	printf("primeraPaginaStack: %d\n", pcb->primerPaginaStack);
	printf("stackPointer: %d\n", pcb->stackPointer);
	printf("tamanioStack: %d\n", pcb->tamanioStack);
	printf("estado: %s\n", pcb->estado);
	printf("exit_code: %d\n", pcb->exit_code);
	for(i=0; i<pcb->cantidad_de_instrucciones; i++){
		printf("Instruccion %d: Inicio = %d, Offset = %d\n", i, pcb->indice_de_codigo[i].inicio, pcb->indice_de_codigo[i].offset);
	}
	printf("lista_de_etiquetas: %s\n", pcb->lista_de_etiquetas);
	while(j<list_size(pcb->stack_index)){
		registroStack *unReg = list_get(pcb->stack_index,j);
		printf("ret_pos: %d\n",unReg->ret_pos);
		printf("Pagina:%d, Offset: %d, Size: %d\n",unReg->ret_var.page,unReg->ret_var.offset, unReg->ret_var.size);
		printf("Variables:\n");
		for(k=0; k<list_size(unReg->vars);k++){
			variable *unaVar = list_get(unReg->vars, k);
			printf("ID: %c, offset: %d, page: %d, size: %d\n", unaVar->id,unaVar->offset, unaVar->page, unaVar->size);
		}
		printf("Argumentos:\n");
		for(l=0; l<list_size(unReg->args);l++){
			variable *unArg = list_get(unReg->args, l);
			printf("ID: %c, offset: %d, page: %d, size: %d\n", unArg->id,unArg->offset, unArg->page, unArg->size);
		}
		j++;
	}

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

	pthread_mutex_lock(&mutex_fd_consolas);
	proceso_conexion *consola = remove_by_fd_socket(lista_consolas,fd_consola);
	consola->proceso = nuevo_PCB->pid;
	list_add(lista_consolas, consola);
	pthread_mutex_unlock(&mutex_fd_consolas);

	tabla_de_archivos_de_proceso* pft = malloc(sizeof(tabla_de_archivos_de_proceso));
	pft->pid = nuevo_PCB->pid;
	pft->lista_de_archivos = list_create();
	pft->current_fd = 3;

	pthread_mutex_lock(&mutex_archivos_x_proceso);
	list_add(tabla_de_archivos_por_proceso,pft);
	pthread_mutex_unlock(&mutex_archivos_x_proceso);

	datos_proceso* dp = malloc(sizeof(datos_proceso));
	dp->id = nuevo_PCB->pid;
	dp->op_alocar = 0;
	dp->op_liberar = 0;
	dp->rafagas = 0;
	dp->syscalls = 0;
	dp->bytes_alocar = 0;
	dp->bytes_liberar = 0;

	pthread_mutex_lock(&mutex_lista_datos);
	list_add(lista_datos_procesos,dp);
	pthread_mutex_unlock(&mutex_lista_datos);

	return nuevo_PCB;
}


void remove_PCB_from_list(t_list* lista, int pid){
	int i = 0, dimension = list_size(lista), encontrado = 0;
	while(!encontrado && i < dimension){
		PCB* aux = list_get(lista,i);
		if(aux->pid == pid){
			list_remove(lista,i);
			encontrado = 1;
		}
		i++;
	}
}


int queue_exists(uint32_t processid, t_queue *queue){
	int i = 0, len, encontrado = 0;
	t_list* lista_auxiliar = list_create();
	len = queue_size(queue);
	while(i<len){
		//Saco un PCB y me fijo si es el que busco
		PCB* aux = queue_pop(queue);
		if(aux->pid == processid)
		{
			encontrado = 1;
		}
		list_add(lista_auxiliar,aux);
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
	return encontrado;
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
	if(list_size(lista_auxiliar)>0)
	{
		//De ser asi...
		while(i<list_size(lista_auxiliar))
		{
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
	log_info(kernelLog, "La cantidad de procesos actuales es: %d\n", procesos_actuales);
	pthread_mutex_unlock(&mutex_procesos_actuales);
	pthread_mutex_lock(&mutex_exit_queue);
	queue_push(exit_queue,pcb);
	pthread_mutex_unlock(&mutex_exit_queue);
}


PCB *get_PCB_by_ID(t_list *lista, uint32_t PID){
	bool _remove_element(void* list_element)
	    {
		PCB *unPCB = (PCB*) list_element;
		return unPCB->pid == PID;
	    }
	PCB* PCB_buscado =  list_remove_by_condition(lista,*_remove_element);
	return PCB_buscado;
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

PCB* get_PCB(int PId){
	int i = 0, dimension = list_size(todos_los_procesos);
	PCB *to_ret;
	while(i < dimension){
		PCB *aux = list_get(todos_los_procesos,i);
		if(aux->pid == PId)
			to_ret = aux;
		i++;
	}
	return to_ret;
}

int buscar_consola_de_proceso(int processid){
	pthread_mutex_lock(&mutex_fd_consolas);
	int i = 0, dimension = list_size(lista_consolas), res = -1;
	while(i < dimension)
	{
		proceso_conexion* aux = list_get(lista_consolas,i);
		if(aux->proceso == processid)
		{
			pthread_mutex_unlock(&mutex_fd_consolas);
			return aux->sock_fd;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_fd_consolas);
	return res;
}


int proceso_para_borrar(int processID){
	pthread_mutex_lock(&mutex_to_delete);
	int i = 0, dimension = list_size(procesos_a_borrar);
	just_a_pid *aux;
	while(i < dimension){
		aux = list_get(procesos_a_borrar,i);
		if(aux->proceso == processID)
		{
			pthread_mutex_unlock(&mutex_to_delete);
			return i;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_to_delete);
	return -1;
}

void signal(char *id_semaforo){
	//pthread_mutex_lock(&mutex_semaforos_ansisop);
	semaforo_cola *unSem = remove_semaforo_by_ID(semaforos, id_semaforo);
	unSem->sem->valor = unSem->sem->valor + 1;

	if(queue_size(unSem->cola_de_bloqueados)>0)
	{
		PCB *unPCB = queue_pop(unSem->cola_de_bloqueados);

		int posicion = proceso_para_borrar(unPCB->pid);

		if(posicion < 0){
		//	pthread_mutex_lock(&mutex_process_list);
			PCB *PCB_a_modif = get_PCB(unPCB->pid);
			PCB_a_modif->estado = "Ready";
		//	pthread_mutex_unlock(&mutex_process_list);

			pthread_mutex_lock(&mutex_ready_queue);
			queue_push(ready_queue, unPCB);
			pthread_mutex_unlock(&mutex_ready_queue);
			log_info(kernelLog, "El proceso %d paso de Wait a Ready\n",unPCB->pid);
		}
	}
	list_add(semaforos, unSem);
	//pthread_mutex_unlock(&mutex_semaforos_ansisop);
}

void remove_from_semaphore(uint32_t PID){
	int i, encontrado = 0;
	int cant_semaforos = list_size(semaforos);
	semaforo_cola *unSem;
	for(i=0; i<cant_semaforos; i++){
		unSem = list_get(semaforos, i);
		encontrado = queue_exists(PID, unSem->cola_de_bloqueados);
		if(encontrado == 1){
			remove_PCB_from_specific_queue(PID, unSem->cola_de_bloqueados);
			signal(unSem->sem->ID);
		}
	}
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
	else if(strcmp(pcb->estado, "Block")==0)
	{
		pthread_mutex_lock(&mutex_semaforos_ansisop);
		remove_from_semaphore(pcb->pid);
		pthread_mutex_unlock(&mutex_semaforos_ansisop);
	}
	else log_error(kernelLog, "Se ha intentado eliminar un PCB que no se encuentra en ninguna cola");
}

proceso_conexion* buscar_conexion_de_cpu(int cpuSock){
	int i = 0, dimension = list_size(lista_cpus);
	while(i < dimension)
		{
			proceso_conexion* aux = list_get(lista_cpus,i);
			if(aux->sock_fd == cpuSock)
				return aux;
			i++;
		}
	//Yes I know, this shit is not kosher but whatevs I only use it once and I take it into account
	return -1;
}


proceso_conexion* buscar_conexion_de_consola(int consolaSock){
	int i = 0, dimension = list_size(lista_consolas);
	while(i < dimension)
		{
			proceso_conexion* aux = list_get(lista_consolas,i);
			if(aux->sock_fd == consolaSock)
				return aux;
			i++;
		}
	//Yes I know, this shit is not kosher but whatevs I only use it once and I take it into account
	return -1;
}

bool exist_PCB(int PId){
	int i = 0, dimension = list_size(todos_los_procesos);
	bool encontrado = false;
	while(i < dimension){
		PCB *aux = list_get(todos_los_procesos,i);
		if(aux->pid == PId)
			encontrado = true;
		i++;
	}
	return encontrado;
}


bool proceso_en_ejecucion(int processID){
	pthread_mutex_lock(&mutex_in_exec);
	int i = 0, dimension = list_size(lista_en_ejecucion);
	proceso_conexion *aux;
	while(i < dimension){
		aux = list_get(lista_en_ejecucion,i);
		if(aux->proceso == processID)
		{
			pthread_mutex_unlock(&mutex_in_exec);
			return true;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_in_exec);
	return false;
}

proceso_conexion* buscar_conexion_de_proceso(int processID){
	pthread_mutex_lock(&mutex_fd_cpus);
	int i = 0, dimension = list_size(lista_cpus);
	while(i < dimension)
		{
			proceso_conexion* aux = list_get(lista_cpus,i);
			if(aux->proceso == processID)
			{
				pthread_mutex_unlock(&mutex_fd_cpus);
				return aux;
			}
			i++;
		}
	pthread_mutex_unlock(&mutex_fd_cpus);
	return -1;
}

int execute_close(int pid, int fd)
{
	int i = 0;
	int referencia_tabla_global;
	tabla_de_archivos_de_proceso* tabla_archivos;
	bool encontrado = false;

	//Saco la tabla de archivos asociada a mi proceso
	while(i < list_size(tabla_de_archivos_por_proceso) && !encontrado)
	{
		tabla_de_archivos_de_proceso* aux = list_get(tabla_de_archivos_por_proceso, i);
		if(pid == aux->pid)
		{
			tabla_archivos = aux;
			encontrado = true;
		}
		i++;
	}
	//Si no encuentra la tabla de archivos del proceso, hay tabla :P
	if(!encontrado)
	{
		printf("Ocurrio un error al cerrar un archivo, revisar el log\n");
		log_error(kernelLog, "Error al cerrar el archivo %d para el proceso %d: el proceso no existe\n", fd, pid);
		return -2;
	}

	log_info(kernelLog, "El proceso %d existe\n", pid);
	i = 0;
	encontrado = false;

	//Busco el fd dentro de la tabla del proceso y elimino las entradas correspondientes a dicho fd
	while(i < list_size(tabla_archivos->lista_de_archivos) && !encontrado)
	{
		archivo_de_proceso* arch_aux = list_get(tabla_archivos->lista_de_archivos, i);
		if(arch_aux->fd == fd)
		{
			log_info(kernelLog, "Encontre el archivo %d para el proceso %d", fd, pid);
			encontrado = true;
			referencia_tabla_global = arch_aux->referencia_a_tabla_global;
			list_remove(tabla_archivos->lista_de_archivos,i);
			free(arch_aux);
			log_info(kernelLog, "Borre el archivo %d de la tabla de archivos y lo libere\n", fd);
		}
		i++;
	}
	//Si no encuentro el fd, hay tabla :P
	if(!encontrado)
	{
		printf("Ocurrio un error al cerrar un archivo, revisar el log\n");
		log_error(kernelLog, "Error al cerrar el archivo %d para el proceso %d - El archivo nunca fue abierto\n", fd, pid);
		return -12;
	}

	i = 0;
	encontrado = false;

	//Disminuyo la cantidad de instancias abiertas del archivo perteneciente al fd dado
	pthread_mutex_lock(&mutex_archivos_globales);
	archivo_global* arch = list_get(tabla_global_de_archivos, referencia_tabla_global);
	pthread_mutex_unlock(&mutex_archivos_globales);

	if(arch->instancias_abiertas == 1)
	{
		//Si la cantidad de instancias abiertas del archivo dado es 1, al disminuir la cantidad de instancias, queda
		//en 0, por lo que tengo que eliminar esa fila de la tabla global
		log_info(kernelLog, "Ningun otro proceso tiene abierto el archivo borrado asi que fue borrado de la global table\n");
		pthread_mutex_lock(&mutex_archivos_globales);
		archivo_global* archivito = list_remove(tabla_global_de_archivos, referencia_tabla_global);
		pthread_mutex_unlock(&mutex_archivos_globales);
		free(archivito->ruta_del_archivo);
	}
	else
	{
		//Disminuyo en 1 la cantidad de instancias abiertas del archivo
		arch->instancias_abiertas--;
		log_info(kernelLog, "Disminui en 1 las instancias abiertas del archivo\n");
	}

	return 1;
}

void borrarTablaDeArchivos(int PID){
	int i = 0, j = 0, mem_ref = 0;
	tabla_de_archivos_de_proceso* tabla_archivos = 0;
	bool encontrado = false;

	while(i < list_size(tabla_de_archivos_por_proceso) && !encontrado)
	{
		tabla_de_archivos_de_proceso* aux = list_get(tabla_de_archivos_por_proceso, i);
		if(pid == aux->pid)
		{
			mem_ref = i;
			tabla_archivos = aux;
			encontrado = true;
		}
		i++;
	}

	if(tabla_archivos > 0){
		while(j < list_size(tabla_archivos->lista_de_archivos))
		{
			archivo_de_proceso* aux = list_get(tabla_archivos->lista_de_archivos,j);
			execute_close(PID,aux->fd);
			j++;
		}
		list_remove(tabla_de_archivos_por_proceso,mem_ref);
	}
	free(tabla_archivos);
}


//Devuelve bytes liberados
int liberarHeap(int PID){
	int i = 0, dimension = list_size(heap), bytes_liberados = 0;
	while(i < dimension)
	{
		heap_de_proceso* hearocess = list_get(heap,i);
		if(hearocess->pid == PID)
		{
			int j = 0, dimension_heap_proceso = list_size(hearocess->heap);
			while(j < dimension_heap_proceso)
			{
				puntero* ptro = list_get(hearocess->heap,j);
				bytes_liberados += ptro->size;
				free(ptro);
				j++;
			}
		}
		i++;
	}
	return bytes_liberados;
}


int existe_PCB_usado(int peide){
	int i = 0, dimension = list_size(PCBs_usados);
	while(i < dimension){
		PCB* aux = list_get(PCBs_usados,i);
		if(aux->pid == peide){
			return i;
		}
		i++;
	}
	return -1;
}

void borrar_PBCs_usados(int peide)
{
	int pos_use = existe_PCB_usado(peide);
	if(pos_use >= 0){
		PCB* usado = list_remove(PCBs_usados,pos_use);
		usado->estado = malloc(sizeof(char)*5);
		liberar_PCB(usado);
	}
}


void end_process(int PID, int exit_code, int sock_consola, bool consola_conectada){
	int i = 0;
	bool encontrado = 0;
	pthread_mutex_lock(&mutex_process_list);
	while(i<list_size(todos_los_procesos) && !encontrado)
	{
		PCB* PCB = list_get(todos_los_procesos,i);
		if(PID == PCB->pid)
		{
			if(strcmp(PCB->estado,"Exit")!=0)
			{
				borrar_PBCs_usados(PID);
				borrarTablaDeArchivos(PID);
				remove_from_queue(PCB);
				PCB->exit_code = exit_code;
				PCB->estado = "Exit";
				delete_PCB(PCB);
				printf("El proceso ha sido finalizado con Exit Code: %d\n", PCB->exit_code);
				log_info(kernelLog, "El proceso ha sido finalizado con Exit Code: %d\n", PCB->exit_code);
				encontrado = 1;
			}
			else
			{
				printf("El proceso elegido ya esta finalizado\n");
				log_info(kernelLog, "El proceso elegido ya esta finalizado\n");
			}
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_process_list);
	if(encontrado == 0 && consola_conectada && exit_code == -7)
	{
			void* sendbuf_consola_mensajera = malloc(sizeof(uint32_t));
			printf("El PID seleccionado todavia no ha sido asignado a ningun proceso\n");
	 		log_info(kernelLog, "El PID seleccionado todavia no ha sido asignado a ningun proceso\n");
			//Lo mando 0 para que sepa que no se pudo borrar el proceso
			uint32_t codigo_de_cancelado_no_ok = 0;
			memcpy(sendbuf_consola_mensajera,&codigo_de_cancelado_no_ok,sizeof(uint32_t));
			send(sock_consola,sendbuf_consola_mensajera,sizeof(uint32_t)*2,0);
			free(sendbuf_consola_mensajera);
	}
	if(encontrado == 1)
	{
		//Aca indico cuantos bytes quedaron leakeando
		int bytes_lost = liberarHeap(PID);
		if(bytes_lost != 0)
				log_info(kernelLog, "Memory leak: se perdieron %d bytes\n",bytes_lost);
			else
				log_info(kernelLog, "No hubo Memory leaks\n");

		//Le aviso a memoria que le saque las paginas asignadas
		void* sendbuf_mem = malloc(sizeof(uint32_t)*2);
		uint32_t codigo_para_borrar_paginas = 5;
		memcpy(sendbuf_mem,&codigo_para_borrar_paginas,sizeof(uint32_t));
		memcpy(sendbuf_mem+sizeof(uint32_t),&PID,sizeof(uint32_t));
		send(sockfd_memoria,sendbuf_mem,sizeof(uint32_t)*2,0);
		free(sendbuf_mem);

		log_info(kernelLog, "Se ha mandado a memoria que borre el proceso\n");

		if(consola_conectada){
			void* sendbuf_consola_mensajera = malloc(sizeof(uint32_t));
			uint32_t codigo_para_abortar_proceso;
			void* sendbuf_consola = malloc(sizeof(uint32_t));

			if(exit_code==0)
				//Si el codigo es 0 termino bien, sino hubo error
				codigo_para_abortar_proceso = 6;
			else
				codigo_para_abortar_proceso = 7;

			int consola = 0;
			consola = buscar_consola_de_proceso(PID);
			memcpy(sendbuf_consola,&codigo_para_abortar_proceso,sizeof(uint32_t));
			send(consola,sendbuf_consola,sizeof(uint32_t),0);

			//Y al hilo que me mando el mensaje que fue lo que paso
			//Le mando 1 para que sepa que se pudo borrar
			uint32_t codigo_de_cancelado_ok = 1;

			memcpy(sendbuf_consola_mensajera,&codigo_de_cancelado_ok,sizeof(uint32_t));
			send(sock_consola,sendbuf_consola_mensajera,sizeof(uint32_t),0);

			free(sendbuf_consola);
			free(sendbuf_consola_mensajera);
		}
	}
	printf("\n");
	log_info(kernelLog, "Se ha finalizado todo el proceso de borrado\n");
}

void print_PCB_list(char* state){
	bool todos;
	if((strcasecmp(state, "all")) == 0)
		todos = true;
	else todos = false;
	int i = 0, number_of_prints = 0;
	if(list_size(todos_los_procesos)>0)
	{
		pthread_mutex_lock(&mutex_process_list);

		printf("Se imprime lista de PCBs en el log\n");

		log_info(kernelLog, "***LISTA DE PCBs***\n");

		while(i < list_size(todos_los_procesos))
		{
			PCB* aux = list_get(todos_los_procesos,i);
			if(todos || strcasecmp(aux->estado,state) == 0)
			{
				printf("PID: %d\n",aux->pid);
				log_info(kernelLog,"PID: %d\n",aux->pid);
				number_of_prints++;
			}
			i++;
		}
		pthread_mutex_unlock(&mutex_process_list);
	}
	if(number_of_prints == 0)
	{
		printf("No hay procesos en planificacion\n");
		log_info(kernelLog,"No hay procesos en planificacion\n");
	}
	printf("\n");
}

void interface_print_PCB(){
	char* state = malloc(sizeof(char)*256);
	printf("Elija el estado de los procesos que desea imprimir\n");
	printf("All - New - Ready - Exec - Block - Exit\n");
	scanf("%s", state);
	if((strcasecmp(state, "All")) == 0 || (strcasecmp(state, "New")) == 0 || (strcasecmp(state, "Ready")) == 0 || (strcasecmp(state, "Exec")) == 0 || (strcasecmp(state, "Block")) == 0 || (strcasecmp(state, "Exit")) == 0)
		print_PCB_list(state);
	else printf("Error: cola invalida. Sera devuelto al menu principal\n\n");
	free(state);
}

void print_all_files(){
	pthread_mutex_lock(&mutex_archivos_globales);

	printf("Se imprimen la informacion de todos los archivos en el log\n");

	if(list_size(tabla_global_de_archivos) > 0)
	{
		int i = 0;
		while(i < list_size(tabla_global_de_archivos))
		{
			archivo_global* aux = list_get(tabla_global_de_archivos,i);
			printf("Ruta del archivo: %s ; Instancias abiertas: %d\n", aux->ruta_del_archivo, aux->instancias_abiertas);
			log_info(kernelLog, "Ruta del archivo: %s ; Instancias abiertas: %d\n", aux->ruta_del_archivo, aux->instancias_abiertas);
			i++;
		}
	}
	else
	{
		printf("No se ha abierto ningun archivo\n");
		log_info(kernelLog, "No se ha abierto ningun archivo\n");
	}
	pthread_mutex_unlock(&mutex_archivos_globales);
	printf("\n");
}


void print_files_from_process(){
	int i = 0, j = 0, encontrado = 0;
	int PID;
	printf("Ingrese el PID del proceso cuyos archivos desea ver\n");
	scanf("%d", &PID);

	pthread_mutex_lock(&mutex_archivos_x_proceso);
	while(i < list_size(tabla_de_archivos_por_proceso) && encontrado == 0)
	{
		tabla_de_archivos_de_proceso* aux_table = list_get(tabla_de_archivos_por_proceso,i);
		if(aux_table->pid == PID)
		{
			printf("Se imprime la informacion de los archivos del proceso en el log\n");

			if(list_size(aux_table->lista_de_archivos) > 0)
			{
				while(j < list_size(aux_table->lista_de_archivos))
				{
					archivo_de_proceso* aux_file = list_get(aux_table->lista_de_archivos,j);
					pthread_mutex_lock(&mutex_archivos_globales);
					archivo_global* aux_global = list_get(tabla_global_de_archivos,aux_file->referencia_a_tabla_global);
					pthread_mutex_unlock(&mutex_archivos_globales);
					printf("Ruta del archivo: %s ; File descriptor: %d\n",aux_global->ruta_del_archivo,aux_file->fd);
					log_info(kernelLog, "Ruta del archivo: %s ; File descriptor: %d\n",aux_global->ruta_del_archivo,aux_file->fd);
					j++;
				}
			}
			else
				log_info(kernelLog, "Este proceso no ha abierto ningun archivo\n");
				encontrado = 1;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_archivos_x_proceso);
	printf("\n");
}

void print_commands()
{
	printf("\nComandos\n");
	printf("\t list		- Lista de Procesos\n");
	printf("\t state		- Estado de un Proceso\n");
	printf("\t plan		- Detener/Reanudar Planificacion\n");
	printf("\t files_all	- Lista de todos los archivos\n");
	printf("\t files		- Lista de los archivos de un proceso\n");
	printf("\t data		- Informacion estadistica de un proceso\n");
	printf("\t grado		- Modifica el grado de multiprogramacion\n");
	printf("\t qsleep		- Modifica el quantum sleep\n");
	printf("\t menu		- Mostrar menu\n\n");
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
		printf("Los PIDs empiezan en 1");
		log_error(kernelLog, "Se intento localizar el PCB de un proceso con pid menor a 1, lo cual es invalido\n");
	}
	else
	{
		pthread_mutex_lock(&mutex_process_list);
		while(i<list_size(todos_los_procesos) && !encontrado)
		{
			PCB* PCB = list_get(todos_los_procesos,i);
			if(*PID == PCB->pid)
			{
				printf("Proceso: %d\n",PCB->pid);
				printf("Estado: %s\n", PCB->estado);
				if(!strcmp(PCB->estado,"Exit"))
					printf("Exit code: %d\n", PCB->exit_code);
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

datos_proceso* get_datos_proceso(int PID){
	pthread_mutex_lock(&mutex_lista_datos);
	int i = 0, dimension = list_size(lista_datos_procesos);
	datos_proceso* aux;
	while(i < dimension){
		aux = list_get(lista_datos_procesos,i);
		if(aux->id == PID){
			pthread_mutex_unlock(&mutex_lista_datos);
			return aux;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_lista_datos);
	return 0;
}

void data_menu(int PID, datos_proceso* datapro){
	char* data = malloc(sizeof(char)*20);
	printf("\nOpciones\n");
	printf(">rafagas	- Numero de rafagas ejecutadas por el proceso\n");
	printf(">syscalls	- Numero de syscalls realizadas\n");
	printf(">alocar		- Numero de operaciones 'Alocar' y bytes alocados\n");
	printf(">liberar	- Numero de operaciones 'Liberar' y bytes liberados\n\n");
	printf("Ingrese los datos que desea ver del proceso %d\n",PID);
	scanf("%s",data);
	if((strcmp(data, "rafagas")) == 0)
		printf("Numero de rafagas del proceso: %d\n\n", datapro->rafagas);
	else if((strcmp(data, "syscalls")) == 0)
		printf("Numero de syscalls realizados por el proceso: %d\n\n", datapro->syscalls);
	else if((strcmp(data, "alocar")) == 0)
		printf("Numero de operaciones 'Alocar': %d\nNumero de bytes alocados: %d\n\n", datapro->op_alocar,datapro->bytes_alocar);
	else if((strcmp(data, "liberar")) == 0)
		printf("Numero de operaciones 'Liberados': %d\nNumero de bytes liberados: %d\n\n", datapro->op_liberar,datapro->bytes_liberar);
	else
	{
		printf("Comando incorrecto. Vuelva a intentar\n\n");
		data_menu(PID, datapro);
	}
}

void get_data_from_process(){
	int* PID = malloc(sizeof(int));
	printf("Ingrese el PID del proceso cuyos datos desea ver\n");
	scanf("%d", PID);
	datos_proceso* datapro = get_datos_proceso(*PID);
	if(datapro > 0){
		data_menu(*PID,datapro);
	} else printf("Error: PID invalido. Sera de vuelto al menu principal/n");
}

void finish_process(){
	int PID;
	printf("Ingrese el PID del PCB cuyo estado desea ver: ");
	scanf("%d",&PID);
	if(PID < 1)
		printf("Error: el grado de multiprogramacion debe ser mayor a 0\n");
	else
	{
		int consola = buscar_consola_de_proceso(PID);
		if(consola != -1)
			end_process(PID,-7,consola,true);
		else
			end_process(PID,-7,consola,false);
		pthread_mutex_unlock(&mutex_planificacion);
	}
}

void abort_process(uint32_t pid, int32_t codigo_error, uint32_t fd_cpu){
	int32_t respuesta = -1;
	enviar(fd_cpu, &respuesta, sizeof(int32_t));

	pthread_mutex_lock(&mutex_in_exec);
	remove_by_fd_socket(lista_en_ejecucion, fd_cpu);
	pthread_mutex_unlock(&mutex_in_exec);

	int fd_consola = buscar_consola_de_proceso(pid);

	int proceso_borrado = proceso_para_borrar(pid);
	bool hay_consola;
	if(proceso_borrado > 0)
		hay_consola = false;
	else hay_consola = true;

	end_process(pid, codigo_error, fd_consola, hay_consola);
	pthread_mutex_unlock(&mutex_planificacion);
}

void set_multiprog(){
	int grado;
	printf("Ingrese el nuevo grado de multiprogramacion (grado actual: %d)\n", data_config.grado_multiprog);
	scanf("%d", &grado);
	if(grado < 1)
		printf("Error: el grado de multiprogramacion debe ser mayor a 0\n");
	else
	{
		//Por las dudas detengo la planificacion durante esta operacion
		plan_go = false;
		data_config.grado_multiprog = grado;
		plan_go = true;
		printf("El grado de multiprogramacion fue cambiado a %d\n", data_config.grado_multiprog);
		int diferencia = procesos_actuales - data_config.grado_multiprog;
		while(diferencia > 0){
			pthread_mutex_unlock(&mutex_planificacion);
			diferencia--;
		}
	}
	printf("\n");
}

void set_qsleep(){
	int qsleep;
	printf("Ingrese el nuevo quantum sleep (sleep actual: %d)\n", data_config.quantum_sleep);
	scanf("%d", &qsleep);
	if(qsleep < 1)
		printf("Error: el quantum sleep debe ser mayor a 0\n");
	else
	{
		//Por las dudas detengo la planificacion durante esta operacion
		plan_go = false;
		data_config.quantum_sleep = qsleep;
		plan_go = true;
		printf("El quantum sleep fue cambiado a %d\n", data_config.quantum_sleep);
	}
	printf("\n");
}

void menu()
{
	while(1)
	{
		char* command = malloc(20);
		scanf("%s", command);
		if((strcmp(command, "list")) == 0)
			interface_print_PCB();
		else if((strcmp(command, "state")) == 0)
			pcb_state();
		else if((strcmp(command, "plan")) == 0)
		{
			if(plan_go)
			{
				plan_go = false;
				printf("Se ha detenido la planificacion\n\n");
				log_info(kernelLog, "Se ha detenido la planificacion\n\n");
			}
			else
			{
				plan_go=true;
				printf("Se ha reanudado la planificacion\n\n");
				log_info(kernelLog, "Se ha reanudado la planificacion\n\n");
			}
		}
		else if((strcmp(command, "files_all") == 0))
			print_all_files();
		else if((strcmp(command, "files") == 0))
			print_files_from_process();
		else if((strcmp(command, "data") == 0))
			get_data_from_process();
		else if((strcmp(command, "grado") == 0))
			set_multiprog();
		else if((strcmp(command, "menu") == 0))
			print_commands();
		else if((strcmp(command, "end") == 0))
			finish_process();
		else if((strcmp(command, "volo") == 0))
			printf("%d\n",list_size(lista_en_ejecucion));
		else if((strcmp(command, "giussa") == 0))
			printf("%d\n",list_size(procesos_a_borrar));
		else if((strcmp(command, "qsleep") == 0))
			set_qsleep();
		else
		{
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

	if ((result = getaddrinfo(ip, puerto, &hints, &servinfo)) != 0)
	{
		printf("Ocurrio un error de conexion, revisar el log\n");
		log_error(kernelLog, "Getaddrinfo: %s\n", gai_strerror(result));

		return 1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next)
	{
		int errornum;
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			errornum = errno;
			printf("Ocurrio un error de conexion, revisar el log\n");
			log_error(kernelLog, "Error - Client: socket %s\n", strerror(errornum));
			continue;
		}
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);errornum = errno;
			printf("Ocurrio un error de conexion, revisar el log\n");
			log_error(kernelLog, "Error - Client: connect %s\n", strerror(errornum));
			continue;
		}
		break;
	}
	if (p == NULL)
	{
		log_error(kernelLog, "Client: failed to connect\n");
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

	printf("Se imprime la informacion del archivo de configuracion en el log\n");

	log_info(kernelLog, "***INFORMACION DEL ARCHIVO DE CONFIG***");
	log_info(kernelLog, "Puerto Programas: %s\n", data_config.puerto_prog);
	log_info(kernelLog, "Puerto CPUs: %s\n", data_config.puerto_cpu);
	log_info(kernelLog, "IP Memoria: %s\n", data_config.ip_memoria);
	log_info(kernelLog, "Puerto Memoria: %s\n", data_config.puerto_memoria);
	log_info(kernelLog, "IP FS: %s\n", data_config.ip_fs);
	log_info(kernelLog, "Puerto FS: %s\n", data_config.puerto_fs);
	log_info(kernelLog, "Quantum: %i\n", data_config.quantum);
	log_info(kernelLog, "Quantum Sleep: %i\n", data_config.quantum_sleep);
	log_info(kernelLog, "Algoritmo: %s\n", data_config.algoritmo);
	log_info(kernelLog, "Grado Multiprog: %i\n", data_config.grado_multiprog);

	while(data_config.sem_ids[i]!=NULL)
	{
		log_info(kernelLog, "Semaforo %s = %i\n",data_config.sem_ids[i],data_config.sem_init[i]);
		i++;
	}
	while(data_config.shared_vars[k]!=NULL)
	{
		log_info(kernelLog, "Variable Global %i: %s\n",k,data_config.shared_vars[k]);
		k++;
	}
	log_info(kernelLog, "Tamaño del Stack: %i\n", data_config.stack_size);
}

//Funciones de Capa Memoria

void settear_variables_Ansisop(){
	int i = 0;
	int j = 0;

	while(data_config.shared_vars[i]!=NULL)
	{
		variable_compartida *unaVar = malloc(sizeof(variable_compartida));
		unaVar->ID = data_config.shared_vars[i]+1;
		unaVar->valor = 0;
		list_add(variables_compartidas, unaVar);
		i++;
	}

	while(data_config.sem_ids[j] != NULL)
	{
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

	printf("Se imprime informacion de variables compartidas y semaforos en el log\n");
	log_info(kernelLog, "Variables Compartidas:\n");

	while(i < max_var)
	{
		variable_compartida *unaVar = list_get(variables_compartidas, i);
		log_info(kernelLog, "Variable %s, valor = %d\n", unaVar->ID, unaVar->valor);
		i++;
	}
	while(j < max_sem)
	{
		semaforo_cola *unSem = list_get(semaforos, j);
		log_info(kernelLog, "Semaforo %s, valor = %d\n", unSem->sem->ID, unSem->sem->valor);
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

bool existeSemaforo(char* id_semaforo){
	bool _remove_element(void* list_element)
	{
		semaforo_cola *unSem = (semaforo_cola *) list_element;
		return strcmp(id_semaforo, unSem->sem->ID)==0;
    }
	bool existe = list_any_satisfy(semaforos, *_remove_element);
	return existe;
}

bool existeCompartida(char* id_compartida){
	bool _remove_element(void* list_element)
	{
		variable_compartida *unaVar = (variable_compartida *) list_element;
		return strcmp(id_compartida, unaVar->ID)==0;
    }
	bool existe = list_any_satisfy(variables_compartidas, *_remove_element);
	return existe;
}

int32_t wait(char *id_semaforo, uint32_t PID){
	uint32_t valorSemaforo;
	pthread_mutex_lock(&mutex_semaforos_ansisop);
	semaforo_cola *unSem = remove_semaforo_by_ID(semaforos, id_semaforo);
	unSem->sem->valor = unSem->sem->valor - 1;
	list_add(semaforos, unSem);
	pthread_mutex_unlock(&mutex_semaforos_ansisop);
	return unSem->sem->valor;
}

bool tiene_heap(uint32_t pid){
	int tamanio_lista = list_size(heap),i;
	bool encontrado = false;
	for(i=0; i<tamanio_lista; i++){
		heap_de_proceso *unHeap = list_get(heap, i);
		if(unHeap->pid == pid){
			encontrado = true;
		}
	}
	return encontrado;
}

_Bool puntero_existe(t_list* lista_de_punteros, uint32_t direccion){
	int tamanio_lista = list_size(lista_de_punteros),i;
	bool encontrado = false;
	for(i=0; i<tamanio_lista; i++){
		puntero *unPuntero = list_get(lista_de_punteros, i);
		if(unPuntero->direccion == direccion){
			encontrado = true;
		}
	}
	return encontrado;
}

puntero *remove_puntero(t_list* lista_de_punteros ,uint32_t direccion){
	bool _remove_element(void* list_element)
	    {
		puntero *unPuntero = (puntero *) list_element;
		return unPuntero->direccion == direccion;
	    }
	puntero *puntero_buscado = list_remove_by_condition(lista_de_punteros, *_remove_element);
	return puntero_buscado;
}

heap_de_proceso *buscar_heap(uint32_t pid){
	bool _remove_element(void* list_element)
	    {
		heap_de_proceso *unHeap = (heap_de_proceso *) list_element;
		return unHeap->pid == pid;
	    }
	heap_de_proceso *heap_buscado = list_remove_by_condition(heap,*_remove_element);
	return heap_buscado;
}

void print_heap(t_list* heap_proceso){
	int tamanio = list_size(heap_proceso), i;
	for(i=0; i< tamanio; i++){
		puntero *unPuntero = list_get(heap_proceso, i);
		log_info(kernelLog, "Puntero %d, pagina: %d, direccion: %d, espacio: %d\n", i, unPuntero->pagina, unPuntero->direccion, unPuntero->size);
	}
}


int esta_en_uso(int fd){
	int i;
	int en_uso = 0;
	proceso_conexion *cpu;

	pthread_mutex_lock(&mutex_in_exec);
	for(i= 0; i< list_size(lista_en_ejecucion); i++)
	{
		cpu = list_get(lista_en_ejecucion,i);
		if(cpu->sock_fd == fd)
			en_uso = 1;
	}
	pthread_mutex_unlock(&mutex_in_exec);
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

	sendbuf = (void*) PCB_cerealV2(sms,pcb_to_use,&(data_config.stack_size),MEMPCB);
	log_info(kernelLog, "Se ha mandado el proceso a memoria para que sea guardado\n");
	send(sms->fd_mem, sendbuf, sms->messageLength+sizeof(int)*3+sizeof(u_int32_t),0);


	numbytes = recv(sms->fd_mem, &page_counter, sizeof(int),0);
	recv(sms->fd_mem, &direccion, sizeof(int),0);

	if(numbytes > 0)
	{
		//significa que hay espacio y guardo las cosas
		if(page_counter > 0)
		{
			log_info(kernelLog, "El proceso %d se ha guardado en memoria \n",pcb_to_use->pid);
			pcb_to_use->page_counter = page_counter;
			pcb_to_use->primerPaginaStack=page_counter-data_config.stack_size; //pagina donde arranca el stack
			pcb_to_use->direccion_inicio_codigo = direccion;
			pcb_to_use->estado = "Ready";
			pthread_mutex_lock(&mutex_ready_queue);
			queue_push(ready_queue,pcb_to_use);
			pthread_mutex_unlock(&mutex_ready_queue);
			send(sms->fd_consola,&pcb_to_use->pid,sizeof(uint32_t),0);
			int i = 0;
			pthread_mutex_lock(&mutex_fd_consolas);
			while(i > list_size(lista_consolas)){
				proceso_conexion* aux = list_get(lista_consolas,i);
				if(sms->fd_consola == aux->sock_fd)
					aux->proceso = pcb_to_use->pid;
				i++;
			}
			pthread_mutex_unlock(&mutex_fd_consolas);
			log_info(kernelLog, "El proceso %d paso de New a Ready\n",pcb_to_use->pid);

			pthread_mutex_lock(&mutex_procesos_actuales);
			procesos_actuales++;
			log_info(kernelLog, "La cantidad de procesos actuales es: %d\n",procesos_actuales);
			pthread_mutex_unlock(&mutex_procesos_actuales);

		}
		//significa que no hay espacio
		if(page_counter < 0)
		{
			printf("Ocurrio un error al guardar un proceso en memoria, revisar el log\n");
			log_error(kernelLog, "El proceso PID %d no se ha podido guardar en memoria \n",pcb_to_use->pid);
			pcb_to_use->estado = "Exit";
			pcb_to_use->exit_code = -1;
			pthread_mutex_lock(&mutex_exit_queue);
			queue_push(exit_queue,pcb_to_use);
			pthread_mutex_unlock(&mutex_exit_queue);
			log_info(kernelLog, "El proceso PID %d ha pasado de New a Exit \n",pcb_to_use->pid);

			send(sms->fd_consola,&page_counter,sizeof(int),0);
		}
	}
	if(numbytes == 0){perror("receive");}
}

void planificacion(){
	while(1)
	{
		//Si esta funcionando la planificacion
		if(plan_go)
		{
			pthread_mutex_lock(&mutex_planificacion);
			int i;
			//Me fijo si hay procesos en la cola New y si no llegue al tope de multiprog
			//Si es asi, pasa un proceso de New a Ready
			pthread_mutex_lock(&mutex_new_queue);

			if(queue_size(new_queue) > 0)
			{
				if(procesos_actuales < data_config.grado_multiprog)
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

				if(list_size(lista_cpus)>0 && i<list_size(lista_cpus))
				{
					proceso_conexion *cpu = list_get(lista_cpus,i);

					pthread_mutex_lock(&mutex_in_exec);
					list_add(lista_en_ejecucion, cpu);
					pthread_mutex_unlock(&mutex_in_exec);

					PCB *pcb_to_use = queue_pop(ready_queue);

					uint32_t codigo = 10;
					int32_t el_quantum;
					if(strcmp(data_config.algoritmo, "FIFO") == 0){
						el_quantum = -1;
					}
					if(strcmp(data_config.algoritmo, "RR") == 0){
						el_quantum = data_config.quantum;
					}
					enviar(cpu->sock_fd, &el_quantum, sizeof(int32_t));
					enviar(cpu->sock_fd, &(data_config.quantum_sleep), sizeof(uint32_t));
					log_info(kernelLog, "Mande Codigo: %d\n", codigo);
					send_PCBV2(cpu->sock_fd, pcb_to_use, codigo);
					cpu->proceso = pcb_to_use->pid;

					pcb_to_use->estado = "Exec";

					log_info(kernelLog, "El proceso %d ha pasado de Ready a Exec\n",pcb_to_use->pid);

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

	if(procesos_actuales < data_config.grado_multiprog)
		guardado_en_memoria(sms, pcb_to_use);
	else
	{
		//Lo dejo en New sin guardar en memoria
		new_pcb* new = malloc(sizeof(new_pcb));
		new->pcb = pcb_to_use;
		new->sms = sms;
		pthread_mutex_lock(&mutex_new_queue);
		queue_push(new_queue,new);
		pthread_mutex_unlock(&mutex_new_queue);
		log_info(kernelLog, "El sistema ya llego a su tope de multiprogramacion. El proceso sera guardado pero no podra ejecutarse hasta que termine otro.\n");
		send(sms->fd_consola, &pcb_to_use->pid,sizeof(int),0);
	}

	free(sms);
}

int obtener_consola_asignada_al_proceso(int pid)
{
	int i = 0, sock_consola = 0;
	bool encontrado = false;

	pthread_mutex_lock(&mutex_fd_consolas);
	while(i < list_size(lista_consolas) && !encontrado)
	{
		proceso_conexion* aux = list_get(lista_consolas, i);
		if(pid == aux->proceso)
		{
			sock_consola = aux->sock_fd;
			encontrado = true;
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_fd_consolas);

	return sock_consola;
}

tabla_de_archivos_de_proceso* obtener_tabla_archivos_proceso(int pid)
{
	int j = 0;
	bool proceso_encontrado = false;
	tabla_de_archivos_de_proceso* tabla;

	//Saco la tabla de archivos asociada a mi proceso
	while(j < list_size(tabla_de_archivos_por_proceso) && !proceso_encontrado)
	{
		tabla_de_archivos_de_proceso* aux = list_get(tabla_de_archivos_por_proceso, j);
		if(pid == aux->pid)
		{
			tabla = aux;
			proceso_encontrado = true;
		}
		j++;
	}
	//Siempre va a encontrar la tabla de archivos de un proceso, ya que esta se crea con el PCB de dicho proceso
	return tabla;
}

bool puede_escribir_archivos(int flag)
{
	if((flag == WRITE) || (flag == READ_WRITE) || (flag == WRITE_CREATE) || (flag == READ_WRITE_CREATE))
	{
		return true;
	}
	return false;
}

bool puede_crear_archivos(int flag)
{
	if((flag == CREATE) || (flag == READ_CREATE) || (flag == WRITE_CREATE) || (flag == READ_WRITE_CREATE))
	{
		return true;
	}
	return false;
}

bool puede_leer_archivos(int flag)
{
	if((flag == READ) || (flag == READ_CREATE) || (flag == READ_WRITE) || (flag == READ_WRITE_CREATE))
	{
		return true;
	}
	return false;
}

int execute_open(uint32_t pid, t_banderas* permisos, char* path, uint32_t path_length)
{
	uint32_t i = 0, codigo, referencia_tabla_global;
	uint32_t encontrado = false, pudo_crear_archivo = false;
	tabla_de_archivos_de_proceso* tabla_archivos;

	//Le pregunto a fs si ese archivo existe
	//Primero serializo data
	codigo = BUSCAR_ARCHIVO_FS;

	enviar(sockfd_fs, &codigo, sizeof(uint32_t));
	enviar(sockfd_fs, &path_length, sizeof(uint32_t));
	enviar(sockfd_fs, path, path_length);

	recibir(sockfd_fs, &encontrado, sizeof(encontrado));

	if((encontrado != 1) && (permisos->creacion))
	{
		log_info(kernelLog, "El archivo no existe pero se ha abierto con permisos de creacion\n");
		//Le dice al fs que cree el archivo
		codigo = CREAR_ARCHIVO_FS;

		enviar(sockfd_fs, &codigo, sizeof(uint32_t));
		enviar(sockfd_fs, &path_length, sizeof(path_length));
		enviar(sockfd_fs, path, path_length);

		recibir(sockfd_fs, &pudo_crear_archivo, sizeof(pudo_crear_archivo));

		if(pudo_crear_archivo > 0)
		{
			log_info(kernelLog, "El archivo %s fue creado correctamente\n", path);
			//Si pudo crear el archivo, seteo el offset (desplazamiento dentro del archivo) en 0 (porque es nuevo)
		}
		else
		{
			printf("Ocurrio un error al abrir un archivo, revisar el log\n");
			log_error(kernelLog, "Error al abrir un archivo para el proceso %d: El FS no pudo crear el archivo\n", pid);
			return -2; //Afuera de la funcion mata el proceso y le avisa a CPU
		}
	}
	else if((encontrado != 1) && (!permisos->creacion))
	{
		printf("Ocurrio un error al abrir un archivo, revisar el log\n");
		log_info(kernelLog, "Error al abrir un archivo para el proceso %d: El archivo no existe y no se tienen permisos para crearlo\n", pid);
		return -1;		// Afuera de esta funcion se termina el proceso y le avisa a CPU
	}

	//Si se encontro el archivo o se pudo crear...
	//Busco el archivo por su ruta en la tabla global y aumento la cantidad de instancias
	bool existe = false;
	pthread_mutex_lock(&mutex_archivos_globales);
	while(i < list_size(tabla_global_de_archivos) && !existe)
	{
		archivo_global* aux = list_get(tabla_global_de_archivos, i);

		if(strcmp(aux->ruta_del_archivo, path) == 0)
		{
			existe = true;
			aux->instancias_abiertas++;
			log_info(kernelLog, "Se ha encontrado el archivo en la tabla global\n");
		}
		i++;
	}
	pthread_mutex_unlock(&mutex_archivos_globales);
	//Si no esta en la tabla global, creo otra entrada en dicha tabla
	if(!existe)
	{
		log_info(kernelLog, "No se ha encontrado el archivo en la tabla global por lo que sera creado\n");
		archivo_global* nuevo = malloc(sizeof(archivo_global));
		nuevo->instancias_abiertas = 1;
		nuevo->ruta_del_archivo = path;
		pthread_mutex_lock(&mutex_archivos_globales);
		list_add(tabla_global_de_archivos,nuevo);
		pthread_mutex_unlock(&mutex_archivos_globales);
	}

	//Si no encuentra el path en la tabla global -> i = list_size (tamanio de la tabla global de archivos)
	//eso referencia a la posicion de la nueva entrada agregada a la tabla
	referencia_tabla_global = i;

	tabla_archivos = obtener_tabla_archivos_proceso(pid);
	archivo_de_proceso* archivoAbierto = malloc(sizeof(archivo_de_proceso));

	log_info(kernelLog, "Setteando la entrada del archivo %s en la tabla de archivos locales del proceso %d\n", path, pid);
	//Agrego la data de este archivo a la tabla del proceso que abre el archivo
	tabla_archivos->current_fd++;
	archivoAbierto->fd = tabla_archivos->current_fd;

	t_banderas* banderita = malloc(sizeof(t_banderas));
	if(permisos->creacion)
		banderita->creacion = 1;
	else banderita->creacion = 0;

		if(permisos->creacion)
			banderita->escritura = 1;
		else banderita->escritura = 0;

		if(permisos->creacion)
			banderita->lectura = 1;
		else banderita->lectura = 0;

	archivoAbierto->flags = banderita;
	archivoAbierto->offset = 0;
	archivoAbierto->referencia_a_tabla_global = referencia_tabla_global;
	list_add(tabla_archivos->lista_de_archivos, archivoAbierto);
	return archivoAbierto->fd;
}

int move_cursor(int pid, int fd, int position)
{
	if(fd < 3)
	{
		printf("Ocurrio un error al leer un archivo, revisar el log\n");
		log_error(kernelLog, "Error de lectura en el proceso %d\n", pid);
		return -2;
	}

	if(position > tamanio_pagina)
	{
		printf("Ocurrio un error al leer un archivo, revisar el log\n");
		log_error(kernelLog, "Error de lectura en el proceso %d\n", pid);
		return -15;
	}

	tabla_de_archivos_de_proceso* tabla_archivos = obtener_tabla_archivos_proceso(pid);

	//Busco el fd dentro de la tabla
	int k = 0;
	bool encontrado = false;

	while(k < list_size(tabla_archivos->lista_de_archivos) && !encontrado)
	{
		archivo_de_proceso* arch_aux = list_get(tabla_archivos->lista_de_archivos, k);
		//Si lo encuentro
		if(arch_aux->fd == fd)
		{
			encontrado = true;
			arch_aux->offset = position;
		}
		k++;
	}
	//Si no lo encontre signfica que el proceso nunca abrio ese archivo
	if(!encontrado)
	{
		printf("Ocurrio un error al leer un archivo, revisar el log\n");
		log_error(kernelLog, "Error al leer el archivo %d para el proceso %d: El archivo nunca fue abierto por el proceso\n", fd, pid);
		return -2;
	}

	return 1;
}

void* execute_read(int pid, int fd, int messageLength, int32_t *error)
{
	void* readText = malloc(messageLength);

	if(fd < 3)
	{
		//Quiere leer de consola. Eso no debe pasar --> Error
		printf("Ocurrio un error al leer un archivo, revisar el log\n");
		log_error(kernelLog, "Error de lectura en el proceso %d: File descriptor no valido\n", pid);
		*error = -2;
	}

	tabla_de_archivos_de_proceso* tabla_archivos = obtener_tabla_archivos_proceso(pid);

	//Busco el fd dentro de la tabla
	int k = 0;
	int32_t referencia_tabla_global, offset;
	bool encontrado = false;
	archivo_de_proceso* arch_posta;

	while(k < list_size(tabla_archivos->lista_de_archivos) && !encontrado)
	{
		archivo_de_proceso* arch_aux = list_get(tabla_archivos->lista_de_archivos, k);
		//Si lo encuentro
		if(arch_aux->fd == fd)
		{
			encontrado = true;
			arch_posta = arch_aux;
			referencia_tabla_global = arch_aux->referencia_a_tabla_global;
			log_info(kernelLog, "Se ha encontrado la entrada del archivo %d en la tabla de archivos locales del proceso %d\n", fd, pid);
		}
		k++;
	}
	//Si no lo encontre signfica que el proceso nunca abrio ese archivo
	if(!encontrado)
	{
		printf("Ocurrio un error al leer un archivo, revisar el log\n");
		log_error(kernelLog, "Error al leer el archivo %d para el proceso %d: El archivo nunca fue abierto por el proceso\n", fd, pid);
		*error = -12;
	}

	if(!arch_posta->flags->lectura)
	{
		printf("Ocurrio un error al leer un archivo, revisar el log\n");
		log_error(kernelLog, "Error al leer el archivo %d para el proceso %d: El proceso no tiene permisos para leer el archivo\n", fd, pid);
		*error = -3;
	}

	//Obtengo el path para el fd dado
	pthread_mutex_lock(&mutex_archivos_globales);
	archivo_global* arch = list_get(tabla_global_de_archivos, referencia_tabla_global);
	pthread_mutex_unlock(&mutex_archivos_globales);

	//Serializo datos (codigo + mesageLength + offset + path) y se los mando a FS, para que me de el texto leido
	int32_t codigo = LEER_ARCHIVO_FS, size = strlen(arch->ruta_del_archivo)+1;
	void* buffer = malloc((sizeof(uint32_t)*4) + size);

	offset = arch_posta->offset;

	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(int32_t), &size, sizeof(int32_t));
	memcpy(buffer + sizeof(int32_t)*2, arch->ruta_del_archivo, size);
	memcpy(buffer + sizeof(int32_t)*2 + size, &offset, sizeof(int32_t));
	memcpy(buffer + sizeof(int32_t)*3 + size, &messageLength, sizeof(int32_t));

	enviar(sockfd_fs, buffer, (sizeof(int32_t)*4) + size);
	log_info(kernelLog, "Se ha enviado la peticion de lectura al FS\n");
	int32_t codigo_recv;

	recv(sockfd_fs,(void*)&codigo_recv, sizeof(int32_t),0);
	if(codigo_recv == 1){
		log_info(kernelLog, "El FS realizo la lecutra exitosamente\n");
		recv(sockfd_fs,(void*)readText, messageLength,0);
		*error = 1;
	}
	else{
		log_error(kernelLog, "Error: el FS fallo al hacer la lectura\n");
		*error = -14;
	}

	free(buffer);

	return readText;
}

int execute_write(int pid, int archivo, char* message, int messageLength, int sock_mem)
{
	int sock_consola, codigo;
	bool escritura_correcta = false;

	sock_consola = obtener_consola_asignada_al_proceso(pid);

	if(archivo == 1)
	{
		//Impresion por consola
		int codigo_de_impresion = 5;

		//Serializo

		void* buffer = malloc((sizeof(int)*2) + messageLength);

		memcpy(buffer, &codigo_de_impresion, sizeof(int));
		memcpy(buffer + sizeof(int), &messageLength, sizeof(int));
		memcpy(buffer + (sizeof(int)*2), message, messageLength);

		enviar(sock_consola, buffer, (sizeof(int)*2) + messageLength);

		free(buffer);
		log_info(kernelLog, "El texto %s ha sido impreso en la consola correspondiente\n", message);
	}

	if(archivo == 0 || archivo == 2)
	{
		//Estos fd NUNCA se van a usar, si me los piden hubo un error
		printf("Ocurrio un error al escribir un archivo, revisar el log\n");
		log_error(kernelLog, "Error al escribir el archivo %d para el proceso %d: El archivo que se intenta escribir no existe\n", archivo, pid);
		return -2;
	}

	int* encontrado = malloc(sizeof(int));
	memset(encontrado,0,sizeof(int));

	if(archivo > FD_INICIAL_ARCHIVOS)
	{
		tabla_de_archivos_de_proceso* tabla_archivos = obtener_tabla_archivos_proceso(pid);

		//Busco el fd dentro de la tabla
		int k = 0;
		while(k < list_size(tabla_archivos->lista_de_archivos) && !(*encontrado))
		{
			archivo_de_proceso* arch_aux = list_get(tabla_archivos->lista_de_archivos, k);
			//Si lo encuentro
			log_info(kernelLog, "El archivo fue encontrado\n");
			if(arch_aux->fd == archivo)
			{
				//Agarro el flag para ese archivo
				*encontrado = true;
				if(arch_aux->flags->escritura == 0)
				{
					//Termino el proceso con exit code -4
					printf("El proceso no tiene permiso para escribir el archivo seleccionado\n");
					log_error(kernelLog, "Error al escribir el archivo %d para el proceso %d: El proceso no tiene permiso para escribir el archivo seleccionado\n", archivo, pid);
					return -4;
				}

				if(arch_aux->flags->escritura == 1)
				{
					log_info(kernelLog, "El proceso tiene permisos validos\n");
					//Saco la posicion en la tabla global
					int pos_en_tabla_global = arch_aux->referencia_a_tabla_global;

					//El list_get devuelve un puntero, y un string es ya un puntero, por eso tengo char**
					pthread_mutex_lock(&mutex_archivos_globales);
					archivo_global* arch = list_get(tabla_global_de_archivos, pos_en_tabla_global);
					pthread_mutex_unlock(&mutex_archivos_globales);

					//Envio a fs el codigo para que reconozca que quiero escribir algo, el path del archivo, el offset,
					//el tamanio del mensaje a escribir y lo que quiero escribir en el;
					//luego recibo de fs el resultado de la operacion

					//Primero serializo
					codigo = ESCRIBIR_ARCHIVO_FS;
					int size_arch = strlen(arch->ruta_del_archivo) +1;
					void* buffer = malloc((sizeof(uint32_t)*4) + size_arch + messageLength);

					int offset = 0;

					memcpy(buffer, &codigo, sizeof(uint32_t));
					memcpy(buffer + sizeof(uint32_t), &size_arch, sizeof(uint32_t));
					memcpy(buffer + (sizeof(uint32_t)*2), arch->ruta_del_archivo, size_arch);
					memcpy(buffer + (sizeof(uint32_t)*2) + size_arch, &offset, sizeof(uint32_t));
					memcpy(buffer + (sizeof(uint32_t)*3) + size_arch, &messageLength, sizeof(uint32_t));
					memcpy(buffer + (sizeof(uint32_t)*4) + size_arch, message, messageLength);

					log_info(kernelLog, "Se mando el texto a escribir al FS\n");
					enviar(sockfd_fs, buffer, (sizeof(uint32_t)*4) + size_arch + messageLength);
					recibir(sockfd_fs, &escritura_correcta, sizeof(int32_t));

					free(buffer);

					if(!escritura_correcta)
					{
						//Termino el proceso con exit code -11 -> El fs no pudo escribir el archivo
						printf("El fs no pudo modificar el archivo seleccionado\n");
						log_error(kernelLog, "Error al escribir el archivo %d para el proceso %d: El fs no pudo modificar el archivo seleccionado\n", archivo, pid);
						return -14;
					}
				}
			}
			k++;
		}

		//Si no lo encontre signfica que el proceso nunca abrio ese archivo
		if(!(*encontrado))
		{
			//end_process(pid,-4,sock_consola, true);
			printf("El archivo pedido nunca fue abierto por el proceso\n");
			log_error(kernelLog, "Error al escribir el archivo %d para el proceso %d: El archivo pedido nunca fue abierto por el proceso\n", archivo, pid);
			return -12;
		}
	}
	log_info(kernelLog, "La escritura se realizo correctamente\n");
	free(encontrado);
	return 1;
}

int execute_delete(int pid, int fd){
	uint32_t i = 0, ubicacion_tabla_archivos, referencia_tabla_global, codigo_borrar = 15;
	tabla_de_archivos_de_proceso* tabla_archivos;
	bool encontrado = false;

	//Saco la tabla de archivos asociada a mi proceso
	while(i < list_size(tabla_de_archivos_por_proceso) && !encontrado)
	{
		tabla_de_archivos_de_proceso* aux = list_get(tabla_de_archivos_por_proceso, i);
		if(pid == aux->pid)
		{
			tabla_archivos = aux;
			encontrado = true;
		}
		i++;
	}
	//Si no encuentra la tabla de archivos del proceso, hay tabla :P
	if(!encontrado)
	{
		printf("Ocurrio un error al borrar un archivo, revisar el log\n");
		log_error(kernelLog, "Error al borrar el archivo %d para el proceso %d: no se ha encontrado la tabla de archivos del proceso\n", fd, pid);
		return -2;
	}
	log_info(kernelLog, "Se ha encontrado la tabla de archivos del proceso %d\n", pid);
	i = 0;
	encontrado = false;
	archivo_de_proceso* arch_aux;

	//Busco el fd dentro de la tabla del proceso
	while(i < list_size(tabla_archivos->lista_de_archivos) && !encontrado)
	{
		arch_aux = list_get(tabla_archivos->lista_de_archivos, i);
		if(arch_aux->fd == fd)
		{
			log_info(kernelLog, "Se ha encontrado el archivo %d para el proceso %d\n", fd, pid);
			encontrado = true;
			ubicacion_tabla_archivos = i;
			referencia_tabla_global = arch_aux->referencia_a_tabla_global;
		}
		i++;
	}

	if(!encontrado)
	{
		printf("Ocurrio un error al cerrar un archivo, revisar el log\n");
		log_error(kernelLog, "Error al cerrar el archivo %d para el proceso %d: El archivo nunca fue abierto\n", fd, pid);
		return -12;
	}

	pthread_mutex_lock(&mutex_archivos_globales);
	archivo_global* arch = list_get(tabla_global_de_archivos, referencia_tabla_global);
	pthread_mutex_unlock(&mutex_archivos_globales);

	//Significa que solo esta abierto por este proceso
	if(arch->instancias_abiertas == 1)
	{
		log_info(kernelLog, "El archivo %s tiene una sola instancia en la tabla global, se puede borrar\n" ,arch->ruta_del_archivo);
		uint32_t size = strlen(arch->ruta_del_archivo)+4;
		uint32_t reciever;
		void* buffer = malloc(sizeof(uint32_t)*2 + size);

		memcpy(buffer, &codigo_borrar, sizeof(uint32_t));
		memcpy(buffer + sizeof(uint32_t), &size, sizeof(uint32_t));
		memcpy(buffer + sizeof(uint32_t)*2, arch->ruta_del_archivo, size);

		log_info(kernelLog, "Se ha enviado el archivo a borrar al FS\n");
		enviar(sockfd_fs, buffer, sizeof(uint32_t) + size);
		recibir(sockfd_fs, &reciever, sizeof(uint32_t));
		log_info(kernelLog, "Se ha recibido una respuesta del FS\n");

		free(buffer);
		if(reciever)
		{
			list_remove(tabla_global_de_archivos, referencia_tabla_global);
			free(arch);
			list_remove(tabla_archivos->lista_de_archivos,ubicacion_tabla_archivos);
			free(arch_aux);
			log_info(kernelLog, "Se han eliminado las entradas del archivo en las tablas local y global %d\n");
			return reciever;
		}
		else
		{
			log_info(kernelLog, "Error: el Filesystem no pudo borrar el archivo %d\n");
			return -15;
		}
	}
	else
	{
		log_info(kernelLog, "Error: varios procesos tienen abierto el archivo %d\n");
		//Hay varios procesos que lo abrieron => No se puede borrar
		return -16;
	}
}


//TODO el main

int main(int argc, char** argv) {

	//Variables para config
	t_config *config_file;
	int i=0;

	//Variables para conexiones con servidores
	char *buf = malloc(256);
	//File descriptors de los sockets correspondientes a la memoria y al filesystem
	int bytes_mem, bytes_fs;

	//variables para conexiones con clientes
	int listener, fdmax, newfd, nbytes;
	fd_set read_fds;
	int codigo, processID;
	int messageLength;

	//Creo el log
	kernelLog = log_create("../../Files/Logs/Kernel.log", "Kernel", false, LOG_LEVEL_INFO);
	log_info(kernelLog, "\n\n//////////////////////\n\n");	//lo pongo para separar entre ejecuciones

	//Consolas y cpus
	lista_cpus = list_create();
	lista_consolas = list_create();
	lista_en_ejecucion = list_create();
	tabla_de_archivos_por_proceso = list_create();
	procesos_a_borrar = list_create();
	PCBs_usados = list_create();
	proceso_conexion *nueva_conexion_cpu;
	proceso_conexion *nueva_conexion_consola;

	//Colas de planificacion
	exit_queue = queue_create();
	ready_queue = queue_create();
	exec_queue = queue_create();
	new_queue = queue_create();

	//Lista con todos los procesos
	todos_los_procesos = list_create();

	//Tabla global de archivos
	tabla_global_de_archivos = list_create();

	//Variables_compartidas y semaforos
	variables_compartidas = list_create();
	semaforos = list_create();

	//Heap :P
	heap = list_create();

	//Lista con los datos de los procesos
	lista_datos_procesos = list_create();

	FD_ZERO(&fd_procesos);
	FD_ZERO(&read_fds);

	checkArguments(argc);

	config_file = config_create(argv[1]);
	cargar_config(config_file);	//Carga la estructura data_config de Kernel
	print_config();	//Adivina... la imprime por pantalla
	settear_variables_Ansisop();//todo
	print_vars();

	//********************************Conexiones***************************************//

	//Servidores
	sockfd_memoria = get_fd_server(data_config.ip_memoria,data_config.puerto_memoria);		//Nos conectamos a la memoria
	sockfd_fs = get_fd_server(data_config.ip_fs,data_config.puerto_fs);		//Nos conectamos al fs

	int handshake = HANDSHAKE;
	int resp;
	send(sockfd_memoria, &handshake, sizeof(u_int32_t), 0);
	bytes_mem = recv(sockfd_memoria, &resp, sizeof(u_int32_t), 0);
	if(bytes_mem > 0 && resp > 0)
	{
		log_info(kernelLog, "Conectado con Memoria\n");
		tamanio_pagina = resp;
		log_info(kernelLog, "Tamaño de pagina = %d\n", resp);
	}
	else
	{
		if(bytes_mem == -1)
		{
			int errornum = errno;		//esto mas el strerror() permite reemplazar el perror();
										//tengo que guardar el nro de error porque una llamada a sistema me lo puede cambiar
			printf("Ocurrio un error, revisar el log\n");
			log_error(kernelLog, "Se ha producido un error: %s\n", strerror(errornum));
			exit(3);
		}
		if(bytes_mem == 0)
		{
			log_info(kernelLog, "Se desconecto el socket: %d\n", sockfd_memoria);
			close(sockfd_memoria);
		}
	}

	resp = 0;
	send(sockfd_fs, &handshake, sizeof(u_int32_t), 0);
	bytes_fs = recv(sockfd_fs, &resp, sizeof(u_int32_t), 0);
	if(bytes_fs > 0 && resp == 1)
	{
		log_info(kernelLog, "Conectado con FS\n");
	}
	else
	{
		if(bytes_mem == -1)
		{
			int errornum = errno;

			printf("Ocurrio un error, revisar el log\n");
			log_error(kernelLog, "Se ha producido un error con un receive: %s\n", strerror(errornum));

			exit(3);
		}
		if(bytes_mem == 0)
		{
			log_info(kernelLog, "Se desconecto el socket: %d\n", sockfd_fs);
			close(sockfd_fs);
		}
	}
	//Consolas y CPUs
	listener = get_fd_listener(data_config.puerto_prog);

	FD_SET(listener, &fd_procesos);

	fdmax = listener;

//	pthread_mutex_init(&mutex_semaforos_ansisop, NULL);

	//Hilo menu + print command
	print_commands();
	pthread_t men;
	pthread_create(&men,NULL,(void*)menu, NULL);

	//Hilo planficacion
	pthread_t planif;
	pthread_create(&planif,NULL,(void*)planificacion,NULL);

	while(1)
	{
		read_fds = fd_procesos;
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
		{
			int errornum = errno;

			printf("Ocurrio un error, revisar el log\n");
			log_error(kernelLog, "Se ha producido un error con un select: %s\n", strerror(errornum));
			exit(4);
		}

		for(i = 0; i <= fdmax; i++)
		{
			if (FD_ISSET(i, &read_fds))
			{
				if (i == listener)
				{
					newfd = sock_accept_new_connection(listener, &fdmax, &fd_procesos);
				}
				else
				{
					memset(buf, 0, 256*sizeof(char));	//limpiamos el buffer
					if ((nbytes = recv(i, &codigo, sizeof(int), 0)) <= 0)
					{
						bool fue_CPU = false;
						//Dado que no sabemos a que proceso pertenece dicho socket,
						//hacemos que se fije en ambas listas para encontrar el elemento y liberarlo

						pthread_mutex_lock(&mutex_fd_cpus);
						//Aca es donde me encargo de que el proceso muere si se desconecta la CPU
						proceso_conexion* cpu_a_quitar = buscar_conexion_de_cpu(i);
						if(cpu_a_quitar != -1){
							fue_CPU = true;
							if(cpu_a_quitar->proceso <= pid && cpu_a_quitar->proceso > 0)
							{
								log_info(kernelLog, "Se desconecto la CPU %d, que estaba ejecutando el proceso %d\n",cpu_a_quitar->sock_fd,cpu_a_quitar->proceso);
								int consola = buscar_consola_de_proceso(cpu_a_quitar->proceso);
								end_process(cpu_a_quitar->proceso, -17, consola, true);

								pthread_mutex_unlock(&mutex_planificacion);
								pthread_mutex_lock(&mutex_in_exec);
								remove_by_fd_socket(lista_en_ejecucion, i);
								pthread_mutex_unlock(&mutex_in_exec);
							}
							else
							{
								log_info(kernelLog, "Se desconecto la CPU %d, que no estaba ejecutando ningun proceso\n",cpu_a_quitar->sock_fd);
							}
						}
						remove_by_fd_socket(lista_cpus, i); //Lo sacamos de la lista de conexiones cpus y liberamos la memoria
						pthread_mutex_unlock(&mutex_fd_cpus);

						if(!fue_CPU){
							pthread_mutex_lock(&mutex_fd_consolas);
							//Idem con consola
							proceso_conexion* consola_a_quitar = buscar_conexion_de_consola(i);
							PCB* pcb;
							bool exists;
							pthread_mutex_lock(&mutex_process_list);
							if((exists = exist_PCB(consola_a_quitar->proceso)) != -1)
								pcb = get_PCB(consola_a_quitar->proceso);
							pthread_mutex_unlock(&mutex_process_list);

							if(consola_a_quitar != -1 && exists){
								if(consola_a_quitar->proceso <= pid && consola_a_quitar->proceso > 0)
								{
									if((strcmp(pcb->estado,"Exit")) != 0)
									{
										log_info(kernelLog, "Se desconecto la Consola %d, que estaba esperando el proceso %d\n",consola_a_quitar->sock_fd,consola_a_quitar->proceso);
										if(proceso_en_ejecucion(pcb->pid))
										{
											log_info(kernelLog, "El proceso esta en ejecucion, se debe esperar hasta que termine su rafaga\n");
											just_a_pid* aux = malloc(sizeof(just_a_pid));
											aux->proceso = pcb->pid;
											aux->exit_code = -6;
											aux->connection = false;

											pthread_mutex_lock(&mutex_to_delete);
											list_add(procesos_a_borrar,aux);
											pthread_mutex_unlock(&mutex_to_delete);
										}
										else
										{
											log_info(kernelLog, "El proceso no esta en ejecucion, se puede finalizar ahora\n");
											end_process(pcb->pid, -6, i, false);
											pthread_mutex_unlock(&mutex_planificacion);

											log_info(kernelLog, "Proceso borrado ahora\n");
										}
									} else log_info(kernelLog, "El proceso ya ha sido finalizado\n");
								}
							}
							remove_by_fd_socket(lista_consolas, i); //Lo sacamos de la lista de conexiones de consola y liberamos la memoria
							pthread_mutex_unlock(&mutex_fd_consolas);
						}

						close(i);
						FD_CLR(i, &fd_procesos); // remove from master set

						log_info(kernelLog, "Hay %d cpus conectadas\n", list_size(lista_cpus));
						log_info(kernelLog, "Hay %d consolas conectadas\n\n", list_size(lista_consolas));
					}
					else
					{
						log_info(kernelLog, "Se recibio: %d\n", codigo);

						//Si el codigo es 1 significa que el proceso del otro lado esta haciendo el handshake
						if(codigo == HANDSHAKE){
							recv(i, &processID,sizeof(int),0);

							if(processID == ID_CPU)		//Si el processID es 1, sabemos que es una CPU
							{
								log_info(kernelLog, "Es una CPU\n");
								nueva_conexion_cpu = malloc(sizeof(proceso_conexion));
								nueva_conexion_cpu->sock_fd = i;
								nueva_conexion_cpu->proceso = 0;
								pthread_mutex_lock(&mutex_fd_cpus);
								list_add(lista_cpus, nueva_conexion_cpu);
								pthread_mutex_unlock(&mutex_fd_cpus);

								log_info(kernelLog, "Hay %d cpus conectadas\n\n", list_size(lista_cpus));
								pthread_mutex_unlock(&mutex_planificacion);
							}

							if(processID == ID_CONSOLA)		//Si en cambio el processID es 2, es una Consola
							{
								log_info(kernelLog, "Es una Consola\n");
								nueva_conexion_consola = malloc(sizeof(proceso_conexion));
								nueva_conexion_consola->sock_fd = newfd;
								pthread_mutex_lock(&mutex_fd_consolas);
								list_add(lista_consolas, nueva_conexion_consola);
								pthread_mutex_unlock(&mutex_fd_consolas);

								log_info(kernelLog, "Hay %d consolas conectadas\n\n", list_size(lista_consolas));
							}
						}

						//Si el codigo es 2, significa que del otro lado estan queriendo mandar un programa ansisop
						if(codigo == NUEVO_PROGRAMA)
						{
							//Agarro el resto del mensaje
							printf("Ha llegado un script nuevo desde una consola\n");
							log_info(kernelLog, "Ding, dong, bing, bong! Me llego un script!\n");

							recv(i, &messageLength, sizeof(int), 0);
							log_info(kernelLog, "El script mide: %d \n", messageLength);
							void* aux = malloc(messageLength + 2);
							memset(aux,0,messageLength + 2);
							recv(i, aux, messageLength, 0);
							memset(aux + messageLength+1, '\0', 1);

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
							}
							else
							{
								log_info(kernelLog, "Lo sentimos, la planificacion no esta en funcionamiento\n\n");
								int error = -1;
								send(i, &error, sizeof(int), 0);
							}
							pthread_mutex_unlock(&mutex_planificacion);
						}

						//Si el codigo es 3 significa que debo terminar el proceso
						if (codigo == ELIMINAR_PROCESO)
						{
							//Cacheo el pid del proceso que tengo que borrar
							int pid;
							recv(i,&pid,sizeof(int),0);
							PCB* actual_PCB = get_PCB(pid);

							printf("Recibi una operacion de eliminacion de proceso desde una consola\n");

							if(strcmp(actual_PCB->estado,"Exec") == 0){
								//Si esta en ejecucion lo dejo esperando a que lo cancelen
								just_a_pid* aux = malloc(sizeof(just_a_pid));
								aux->proceso = actual_PCB->pid;
								aux->exit_code = -7;
								aux->connection = true;
								aux->consola_askeadora = i;

								log_info(kernelLog, "El proceso esta en ejecucion, se borrara al terminar su rafaga\n");

								pthread_mutex_lock(&mutex_to_delete);
								list_add(procesos_a_borrar,aux);
								pthread_mutex_unlock(&mutex_to_delete);
							}
							else{
								//Saco a la cpu de la lista de ejecucion
								log_info(kernelLog, "El proceso no esta en ejecucion, se borrara ahora\n");
								pthread_mutex_lock(&mutex_in_exec);
								remove_by_fd_socket(lista_en_ejecucion, i);
								pthread_mutex_unlock(&mutex_in_exec);

								//Y llamo a la funcion que lo borra
								end_process(pid, -7, i, true);

								log_info(kernelLog, "Proceso borrado ahora\n");

								proceso_conexion* cpu = buscar_conexion_de_proceso(pid);

								if(cpu != -1){
									cpu->proceso = 0;
								}
								log_info(kernelLog, "CPU actualizada\n");
								pthread_mutex_unlock(&mutex_planificacion);
							}

						}
						if(codigo == ASIGNAR_VALOR_COMPARTIDA)
						{
							u_int32_t value, tamanio;
							int32_t head;
							bool existe;
							recv(i, &value, sizeof(u_int32_t),0);
							recv(i, &tamanio, sizeof(u_int32_t),0);

							char *id_var = malloc(tamanio);
							recv(i, id_var, tamanio,0);

							pthread_mutex_lock(&mutex_vCompartidas_ansisop);
							existe = existeCompartida(id_var);
							pthread_mutex_unlock(&mutex_vCompartidas_ansisop);

							if(existe == true)
							{
								log_info(kernelLog, "Hay que modificar la variable %s con el valor %d\n", id_var, value);

								pthread_mutex_lock(&mutex_vCompartidas_ansisop);
								asignar_valor_variable_compartida(id_var, value);
								pthread_mutex_unlock(&mutex_vCompartidas_ansisop);

								head=1;
								send(i, &head, sizeof(u_int32_t), 0);
								print_vars();
							}
							else //Si no existe la compartida
							{
								log_info(kernelLog, "Abortar proceso, no existe la variable compartida");
								int32_t codigo_error = -15;
								abort_process(pid, codigo_error, i);
							}
						}

						if(codigo == BUSCAR_VALOR_COMPARTIDA)
						{
							u_int32_t value, tamanio;
							int32_t head;
							bool existe;
							recv(i, &tamanio, sizeof(u_int32_t),0);

							char *id_var = malloc(tamanio);
							recv(i, id_var, tamanio,0);

							pthread_mutex_lock(&mutex_vCompartidas_ansisop);
							existe = existeCompartida(id_var);
							pthread_mutex_unlock(&mutex_vCompartidas_ansisop);

							if(existe == true)
							{
								log_info(kernelLog, "Hay que obtener el valor de la variable %s\n", id_var);

								head=1;
								send(i, &head, sizeof(u_int32_t), 0);

								pthread_mutex_lock(&mutex_vCompartidas_ansisop);
								value = obtener_valor_variable_compartida(id_var);
								pthread_mutex_unlock(&mutex_vCompartidas_ansisop);
								log_info(kernelLog, "La variable %s tiene valor %d\n", id_var, value);

								send(i, &value, sizeof(u_int32_t), 0);
							}
							else //Si no existe la compartida
							{
								log_info(kernelLog, "Abortar proceso, no existe la variable compartida");
								int32_t codigo_error = -15;
								abort_process(pid, codigo_error, i);
							}
						}

						if (codigo==WAIT)
						{
							uint32_t PID;
							int32_t valor, head;
							bool existe;
							recv(i, &PID, sizeof(uint32_t), 0);
							recv(i, &messageLength , sizeof(uint32_t), 0);
							char *id_sem = malloc(messageLength);
							recv(i, id_sem, messageLength, 0);

							datos_proceso* dp = get_datos_proceso(PID);
							dp->syscalls ++;

							pthread_mutex_lock(&mutex_in_exec);
							proceso_conexion* cpu = remove_by_fd_socket(lista_en_ejecucion, i);
							pthread_mutex_unlock(&mutex_in_exec);

							int posicion = proceso_para_borrar(PID);

							if(posicion < 0){

								log_info(kernelLog, "CPU pide: Wait en semaforo: %s\n\n", id_sem);

								pthread_mutex_lock(&mutex_semaforos_ansisop);
								existe = existeSemaforo(id_sem);
								pthread_mutex_unlock(&mutex_semaforos_ansisop);

								pthread_mutex_lock(&mutex_in_exec);
								list_add(lista_en_ejecucion, cpu);
								pthread_mutex_unlock(&mutex_in_exec);

								if(existe == true)
								{

									head = 1; //Existe el semaforo

									valor = wait(id_sem, PID);
									print_vars();

									send(i, &head, sizeof(int32_t),0);
									send(i, &valor, sizeof(int32_t), 0);

									if(valor < 0)
									{
										uint32_t primerMensaje;
										recv(i, &primerMensaje, sizeof(uint32_t), 0); //Este es el 10 que me mandan por ser PCB

										PCB *unPCB = recibirPCBV2(i);
										//print_PCB2(unPCB);

										pthread_mutex_lock(&mutex_in_exec);
										remove_by_fd_socket(lista_en_ejecucion, i);
										pthread_mutex_unlock(&mutex_in_exec);

										log_info(kernelLog, "check 1\n");
										pthread_mutex_lock(&mutex_semaforos_ansisop);
										semaforo_cola *unSem = remove_semaforo_by_ID(semaforos, id_sem);

										log_info(kernelLog, "check 2\n");
										pthread_mutex_lock(&mutex_exec_queue);
										remove_PCB_from_specific_queue(PID, exec_queue);
										pthread_mutex_unlock(&mutex_exec_queue);

										pthread_mutex_lock(&mutex_process_list);
										PCB* PCB_a_modif = get_PCB(unPCB->pid);
										PCB_a_modif->estado = "Block";
										pthread_mutex_unlock(&mutex_process_list);

										log_info(kernelLog, "check 3\n");
										queue_push(unSem->cola_de_bloqueados, unPCB);
										list_add(semaforos, unSem);
										pthread_mutex_unlock(&mutex_semaforos_ansisop);
										log_info(kernelLog, "El proceso %d paso de Exec a Blockn",unPCB->pid);
										log_info(kernelLog, "Termino todo el envio\n");

										pthread_mutex_lock(&mutex_fd_cpus);
										proceso_conexion* cpu = buscar_conexion_de_cpu(i);
										pthread_mutex_unlock(&mutex_fd_cpus);

										cpu->proceso = 0;
									}
									pthread_mutex_unlock(&mutex_planificacion);
								}
								else //Si no existe el semaforo
								{
									log_info(kernelLog, "Abortar proceso, semaforo inexistente");
									int32_t codigo_error = -15;
									abort_process(pid, codigo_error, i);
								}
							}

							else
							{
								int consola_con = -1, resultado = -1;
								consola_con = buscar_consola_de_proceso(PID);

								log_info(kernelLog, "El proceso %d estaba para borrar\n", PID);

								pthread_mutex_lock(&mutex_to_delete);
								just_a_pid* peide = list_remove(procesos_a_borrar,posicion);
								pthread_mutex_unlock(&mutex_to_delete);

								log_info(kernelLog, "El proceso estaba para borrar\n");

								if(peide->exit_code == -7)
									end_process(PID, peide->exit_code, peide->consola_askeadora, peide->connection);
								else
									end_process(PID, peide->exit_code, consola_con, peide->connection);
								free(peide);

								pthread_mutex_lock(&mutex_fd_cpus);
								proceso_conexion* cpu = buscar_conexion_de_cpu(i);
								pthread_mutex_unlock(&mutex_fd_cpus);

								cpu->proceso = 0;

								enviar(i,&resultado,sizeof(int));

								log_info(kernelLog, "Settee el proceso actual de la CPU en 0\n");
								pthread_mutex_unlock(&mutex_planificacion);
							}
							free(id_sem);
						}

						if(codigo==SIGNAL)
						{
							int32_t respuesta;
							recv(i, &messageLength , sizeof(uint32_t), 0);
							char *id_sem = malloc(messageLength);
							recv(i, id_sem, messageLength, 0);
							log_info(kernelLog, "CPU pide: Signal en semaforo: %s \n\n", id_sem);

							pthread_mutex_lock(&mutex_semaforos_ansisop);
							bool existe = existeSemaforo(id_sem);
							pthread_mutex_unlock(&mutex_semaforos_ansisop);

							if(existe == true){
								signal(id_sem);
								print_vars();
								respuesta = 1;
								enviar(i, &respuesta, sizeof(respuesta));
							}else{
								log_info(kernelLog, "Abortar proceso, semaforo inexistente");
								int32_t codigo_error = -15;
								abort_process(pid, codigo_error, i);
							}

							free(id_sem);
							pthread_mutex_unlock(&mutex_planificacion);
						}

						if(codigo == PROCESO_FINALIZADO_CORRECTAMENTE)
						{
							PCB *unPCB = recibirPCBV2(i);
							//print_PCB2(unPCB);
							uint32_t PID = unPCB->pid;

							datos_proceso* dp = get_datos_proceso(PID);
							dp->rafagas ++;

							int fd_consola = buscar_consola_de_proceso(PID);

							pthread_mutex_lock(&mutex_in_exec);
							remove_by_fd_socket(lista_en_ejecucion, i);
							pthread_mutex_unlock(&mutex_in_exec);

							log_info(kernelLog, "Se elimino el proceso de la lista de exec\n");
							int posicion = proceso_para_borrar(PID);
							if(posicion < 0)
							{
								log_info(kernelLog, "El proceso no estaba para borrar\n");
								end_process(PID, 0, fd_consola, true);
							}
							else
							{
								pthread_mutex_lock(&mutex_to_delete);
								just_a_pid* peide = list_remove(procesos_a_borrar,posicion);
								pthread_mutex_unlock(&mutex_to_delete);
								log_info(kernelLog, "El proceso estaba para borrar\n");

								if(peide->exit_code == -7)
									end_process(PID, peide->exit_code, peide->consola_askeadora, peide->connection);
								else
									end_process(PID, peide->exit_code, fd_consola, peide->connection);

								free(peide);
							}

							log_info(kernelLog, "Ya termine toda la operacion de borrado\n");

							pthread_mutex_lock(&mutex_fd_cpus);
							proceso_conexion* cpu = buscar_conexion_de_cpu(i);
							pthread_mutex_unlock(&mutex_fd_cpus);

							cpu->proceso = 0;

							log_info(kernelLog, "Se settea el proceso actual de la CPU en 0\n");
							pthread_mutex_unlock(&mutex_planificacion);
						}

						//Si un proceso termino su rafaga...
						if(codigo == FIN_DE_RAFAGA)
						{
							//Lo recibo...
							PCB *unPCB = recibirPCBV2(i);

							datos_proceso* dp = get_datos_proceso(unPCB->pid);
							dp->rafagas ++;

							//Saco la CPU de la lista de ejecucion
							pthread_mutex_lock(&mutex_in_exec);
							remove_by_fd_socket(lista_en_ejecucion, i);
							pthread_mutex_unlock(&mutex_in_exec);

							//Busco la version anterior del PCB y la saco de Exec
							pthread_mutex_lock(&mutex_exec_queue);
							remove_PCB_from_specific_queue(unPCB->pid,exec_queue);
							pthread_mutex_unlock(&mutex_exec_queue);

							log_info(kernelLog, "El proceso %d ha terminado una rafaga\n", unPCB->pid);

							int posicion = proceso_para_borrar(unPCB->pid);

							if(posicion < 0)
							{
								pthread_mutex_lock(&mutex_process_list);
								PCB* PCB_vieja = get_PCB(unPCB->pid);
								PCB_vieja->estado = "Ready";
								pthread_mutex_unlock(&mutex_process_list);

								//Meto el modificado en Ready
								log_info(kernelLog, "El proceso %d ha sido colocado en la Ready queue\n", unPCB->pid);
								pthread_mutex_lock(&mutex_ready_queue);
								queue_push(ready_queue,unPCB);
								pthread_mutex_unlock(&mutex_ready_queue);
							}
							else
							{
								int consola_con = -1;
								consola_con = buscar_consola_de_proceso(unPCB->pid);
								log_info(kernelLog, "El proceso %d estaba para borrar\n", unPCB->pid);

								pthread_mutex_lock(&mutex_to_delete);
								just_a_pid* peide = list_remove(procesos_a_borrar,posicion);
								pthread_mutex_unlock(&mutex_to_delete);

								if(peide->exit_code == -7)
									end_process(unPCB->pid, peide->exit_code, peide->consola_askeadora, peide->connection);
								else
									end_process(unPCB->pid, peide->exit_code, consola_con, peide->connection);
								free(peide);
							}

							borrar_PBCs_usados(unPCB->pid);

							list_add(PCBs_usados,unPCB);

							pthread_mutex_lock(&mutex_fd_cpus);
							proceso_conexion* cpu = buscar_conexion_de_cpu(i);
							pthread_mutex_unlock(&mutex_fd_cpus);

							cpu->proceso = 0;

							log_info(kernelLog, "Settee el proceso actual de la CPU en 0\n");
							pthread_mutex_unlock(&mutex_planificacion);
						}

						//Los if relacionados a archivos se ejecutan cuando CPU pide hacer algo con un archivo de FS

						if(codigo == ABRIR_ARCHIVO)
						{
							uint32_t pid, path_length;
							//uint32_t permisos;
							t_banderas* permisos = malloc(sizeof(t_banderas));

							//Recibo el pid y el largo de la ruta del archivo
							recibir(i, &pid, sizeof(uint32_t));
							recibir(i, &path_length, sizeof(uint32_t));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;

							char* file_path = malloc(path_length);

							//Recibo el path del archivo a abrir
							recibir(i, file_path, path_length);

							//Recibo los permisos con los que se abre el archivo

							recibir(i, &(permisos->creacion), sizeof(bool));
							recibir(i, &(permisos->lectura), sizeof(bool));
							recibir(i, &(permisos->escritura), sizeof(bool));

							log_info(kernelLog, "Se ha realizado una peticion de apertura de archivo con las siguientes caracteristicas:\n");
							log_info(kernelLog, "PID: %d\n",pid);
							log_info(kernelLog, "Path length: %d\n", path_length);
							log_info(kernelLog, "Path: %s\n", file_path);
							log_info(kernelLog, "Permisos: C(%d), R(%d), W(%d)\n", permisos->creacion, permisos->lectura, permisos->escritura);

							int resultado = execute_open(pid, permisos, file_path, path_length);

							if(resultado == -1)
							{
								//Codigo de error -10:
								//No se pudo abrir el archivo por que no existe y no se puede crear por falta de permisos
								int32_t codigo_error = -10;
								abort_process(pid, codigo_error, i);
							}
							else if(resultado == -2)
							{
								//Codigo de error -11:
								//El fs no pudo crear el archivo, se produjo error
								int32_t codigo_error = -11;
								abort_process(pid, codigo_error, i);
							}
							else
							{
								//Pudo abrir el archivo, le mando el fd a cpu
								log_info(kernelLog, "Un archivo ha sido abierto exitosamente para el proceso %d\n", pid);
								enviar(i, &resultado, sizeof(int));
							}
						}

						if(codigo == LEER_ARCHIVO)
						{
							int fd, pid, messageLength, codigo;

							//Recibo pid, fd, offset y largo del texto a leer
							recibir(i, &pid, sizeof(int));
							recibir(i, &fd, sizeof(int));
							recibir(i, &messageLength, sizeof(int));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;

							int32_t error = 0;
							void* resultado = execute_read(pid, fd, messageLength,&error);

							if(error < 0)
							{
								//Posibles errores:
								// > -2 -> El archivo no existe
								// > -3 -> Permisos no validos
								// > -12 -> El archivo nunca fue abierto por el proceso
								// > -13 -> Fallo el FS en la lectura
								abort_process(pid,error,i);
							}
							else
							{
								codigo = 1;
								log_info(kernelLog, "Se realizo exitosamente una lectura del archivo %d para el proceso %d\n", fd, pid);
								enviar(i, &codigo, sizeof(int));
								enviar(i, resultado, messageLength);
							}

						}

						if(codigo == ESCRIBIR_ARCHIVO)
						{
							int archivo, pid, messageLength;

							//Recibo el fd del archivo, el pid y el largo del mensaje a escribir
							recibir(i, &archivo, sizeof(int));
							recibir(i, &pid, sizeof(int));
							recibir(i, &messageLength, sizeof(int));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;

							void* message = malloc(messageLength);

							//Recibo el mensaje a escribir
							recibir(i, message, messageLength);

							log_info(kernelLog,"Me pidieron escribir %s en el archivo %d del proceso %d",message,archivo,pid);

							int resultado = execute_write(pid, archivo, message, messageLength, sockfd_memoria);

							if(resultado < 0)
								//Posibles errores:
								// > -2 -> El archivo no existe
								// > -3 -> Permisos no validos
								// > -12 -> El archivo nunca fue abierto
								// > -14 -> Fallo el FS en la escritura
								abort_process(pid, resultado, i);
							else
								enviar(i, &resultado, sizeof(int));

							free(message);
						}

						if(codigo == CERRAR_ARCHIVO)
						{
							uint32_t fd, pid, resultado = 1;

							//Recibo pid y fd
							recibir(i, &pid, sizeof(uint32_t));
							recibir(i, &fd, sizeof(uint32_t));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;

							log_info(kernelLog, "Me pidieron cerrar un archivo con las siguientes caracteristicas:\n");
							log_info(kernelLog, "  PID: %d\n",pid);
							log_info(kernelLog, "  FD: %d\n",fd);

							if(fd != 1)
								resultado = execute_close(pid, fd);

							if(resultado < 0)
							{
								//Posibles errores:
								// > -2 -> El archivo no existe
								// > -12 -> El archivo nunca fue abierto
								abort_process(pid, &resultado, i);

							}
							else
								log_info(kernelLog, "Se ha cerrado el archivo %d para el proceso %d\n", fd, pid);

							enviar(i, &resultado, sizeof(uint32_t));
						}

						if(codigo == MOVER_CURSOR){
							int pid, fd, posicion;
							recibir(i, &pid, sizeof(int));
							recibir(i, &fd, sizeof(int));
							recibir(i, &posicion, sizeof(int));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;

							int resultado = move_cursor(pid, fd, posicion);

							if(resultado < 0)
								abort_process(pid, resultado, i);
							else
								enviar(i, &resultado, sizeof(int));
						}
						if(codigo == RESERVAR_MEMORIA)
						{
							uint32_t pid;
							int espacio;
							recibir(i, &pid, sizeof(uint32_t));
							recibir(i, &espacio, sizeof(int));
							int32_t paginas_totales, error = -1;

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;
							dp->op_alocar ++;

							//tamanio maximo alocable es igual al tamanio de pagina menos el tamanio de un heapMetadata
							uint32_t tam_max_alocable = tamanio_pagina - sizeof(uint32_t)*2;

							log_info(kernelLog, "El proceso %d necesita alocar %d bytes\n", pid, espacio);

							if(tiene_heap(pid) == false && espacio <= tam_max_alocable){
								uint32_t cod = SOLICITAR_HEAP;
								void *buffer = malloc(2*sizeof(uint32_t));
								memcpy(buffer, &cod, sizeof(uint32_t));
								memcpy(buffer + sizeof(uint32_t), &pid, sizeof(uint32_t));
								enviar(sockfd_memoria, buffer, sizeof(uint32_t)*2);

								recibir(sockfd_memoria, &paginas_totales, sizeof(int32_t));
								if(paginas_totales > 0){
									heap_de_proceso *nuevoHeap = malloc(sizeof(heap_de_proceso));
									nuevoHeap->heap = list_create();
									nuevoHeap->pid = pid;
									list_add(heap, nuevoHeap);
								} else if(paginas_totales == -1)
									{
										error = -9;
										abort_process(pid, error, i);
										log_error(kernelLog, "Abortar proceso, no hay espacio suficiente para el pedido\n");
									}
							}
							if(tiene_heap(pid) == true && espacio <= tam_max_alocable){
								heap_de_proceso *heap_buscado = buscar_heap(pid);

								print_heap(heap_buscado->heap);

								pthread_mutex_lock(&mutex_process_list);
								PCB *unPCB = get_PCB_by_ID(todos_los_procesos, pid);
								uint32_t pagina_de_heap = unPCB->page_counter;
								list_add(todos_los_procesos, unPCB);
								pthread_mutex_unlock(&mutex_process_list);

								log_info(kernelLog, "Debemos alocar en la pagina %d\n", pagina_de_heap);

								void *buffer = malloc(sizeof(uint32_t)*4);
								uint32_t cod = ALOCAR;
								memcpy(buffer, &cod, sizeof(uint32_t));
								memcpy(buffer + sizeof(uint32_t), &pid, sizeof(uint32_t));
								memcpy(buffer + sizeof(uint32_t)*2, &espacio, sizeof(uint32_t));
								memcpy(buffer + sizeof(uint32_t)*3, &pagina_de_heap, sizeof(uint32_t));

								//Alocamos
								int32_t corrimiento;
								enviar(sockfd_memoria, buffer, sizeof(uint32_t)*4);
								recibir(sockfd_memoria, &corrimiento, sizeof(int32_t));
								recibir(sockfd_memoria, &pagina_de_heap, sizeof(uint32_t));
								uint32_t direccion = (pagina_de_heap * tamanio_pagina) + corrimiento;

								if(corrimiento > 0)
								{
									log_info(kernelLog, "El puntero apunta a la direccion %d\n", direccion);
									puntero *unPuntero = malloc(sizeof(puntero));
									unPuntero->direccion = direccion;
									unPuntero->size = espacio;
									unPuntero->pagina = pagina_de_heap;
									list_add(heap_buscado->heap, unPuntero);
									list_add(heap, heap_buscado);

									print_heap(heap_buscado->heap);
									enviar(i, &direccion, sizeof(int32_t));
									free(buffer);
									dp->bytes_alocar += espacio;
								}
								else
								{
									log_error(kernelLog, "Abortar proceso, no hay espacio suficiente para el pedido\n");
									error = -9;
									abort_process(pid, error, i);
								}
							}

							else
							{
								//Si llegamos aca, significa que el flaco se quiso hacer el lunga y reservar mas que una pagina
								printf("Ocurrio un error al reservar memoria, revisar el log\n");
								log_error(kernelLog, "Error: No es posible reservar un espacio mayor que una pagina\n");
								error = -8;
								abort_process(pid, error, i);
							}
						}
						if(codigo == LIBERAR_MEMORIA)
						{
							uint32_t pid, direccion;
							int32_t error = -1, ok = 1;
							recibir(i, &pid, sizeof(uint32_t));
							recibir(i, &direccion, sizeof(uint32_t));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;
							dp->op_liberar ++;

							if(tiene_heap(pid) == true){
								heap_de_proceso *heap_buscado = buscar_heap(pid);
								print_heap(heap_buscado->heap);
								if(puntero_existe(heap_buscado->heap, direccion)){
									puntero *unPuntero = remove_puntero(heap_buscado->heap, direccion);

									uint32_t corrimiento = unPuntero->direccion - (unPuntero->pagina * tamanio_pagina);
									uint32_t cod = LIBERAR;
									void *buffer = malloc(sizeof(uint32_t)*4);
									memcpy(buffer, &cod, sizeof(uint32_t));
									memcpy(buffer+sizeof(uint32_t), &pid, sizeof(uint32_t));
									memcpy(buffer+sizeof(uint32_t)*2, &(unPuntero->pagina), sizeof(uint32_t));
									memcpy(buffer+sizeof(uint32_t)*3, &corrimiento, sizeof(uint32_t));

									enviar(sockfd_memoria, buffer, sizeof(uint32_t)*4);
									dp->bytes_liberar += unPuntero->size;
									free(unPuntero);
									list_add(heap, heap_buscado);
									log_info(kernelLog, "El espacio ha sido liberado!\n");
									enviar(i, &ok, sizeof(int32_t));
									free(buffer);
								}
								else{
									log_info(kernelLog, "Abortar el proceso, no tiene dicha direccion reservada\n");
									error = -5;
									abort_process(pid, error, i);
								}
							}
							else{
								log_error(kernelLog, "Abortar el proceso, no tiene paginas alocadas\n");
								error = -5;
								abort_process(pid, error, i);
							}
						}
						if(codigo == PROCESO_FINALIZO_ERRONEAMENTE)
						{
							void* buffer_codigo = malloc(4);
							recibir(i, buffer_codigo, 4);
							PCB *unPCB = recibirPCBV2(i);
							print_PCB(unPCB);
							uint32_t PID = unPCB->pid;
							int fd_consola = buscar_consola_de_proceso(PID);

							datos_proceso* dp = get_datos_proceso(PID);
							dp->rafagas ++;

							pthread_mutex_lock(&mutex_in_exec);
							remove_by_fd_socket(lista_en_ejecucion, i);
							pthread_mutex_unlock(&mutex_in_exec);

							log_info(kernelLog, "Elimine el proceso de la lista de exec\n");

							if(!proceso_para_borrar(PID))
							{
								log_info(kernelLog, "El proceso no estaba para borrar\n");
								end_process(PID, *(int*)buffer_codigo, fd_consola, true);
							}
							else
							{
								log_info(kernelLog, "El proceso estaba para borrar\n");
								end_process(PID, *(int*)buffer_codigo, fd_consola, false);
							}

							log_info(kernelLog, "Ya termine toda la operacion de borrado\n");

							pthread_mutex_lock(&mutex_fd_cpus);
							proceso_conexion* cpu = buscar_conexion_de_cpu(i);
							pthread_mutex_unlock(&mutex_fd_cpus);

							cpu->proceso = 0;

							log_info(kernelLog, "Settee el proceso actual de la CPU en 0\n");
							free(buffer_codigo);
							pthread_mutex_unlock(&mutex_planificacion);
						}
						if(codigo == BORRAR_ARCHIVO)
						{
							uint32_t PID, fd, resultado;
							recibir(i,&PID,sizeof(uint32_t));
							recibir(i,&fd,sizeof(uint32_t));

							datos_proceso* dp = get_datos_proceso(pid);
							dp->syscalls ++;

							log_info(kernelLog, "Me pidieron borrar un archivo con las siguientes caracteristicas:\n");
							log_info(kernelLog, "  PID: %d\n",pid);
							log_info(kernelLog, "  FD: %d\n",fd);

							resultado = execute_delete(PID,fd);
							if(resultado < 0)
								//Posibles errores:
								// > -2 -> El archivo no existe
								// > -12 -> El archivo nunca fue abierto
								// > -15 -> Fallo el FS en el borrado
								// > -16 -> Se quiso borrar un archivo abierto por varios procesos
								abort_process(PID,resultado,fd);
							else
								enviar(i, &resultado, sizeof(int));
						}
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
