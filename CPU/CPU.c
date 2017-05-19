/*
 ============================================================================
 Name        : CPU.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <commons/config.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"
#include <parser/parser.h>
#include <commons/collections/list.h>

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


	t_list* stack_index;
	int primerPaginaStack; //Seria la cant de paginas del codigo porque viene despues del codigo
	int stackPointer;
	int tamanioStack;
} PCB;

typedef struct{
	char id;
	int page;
	int offset;
	int size;
}variable;

PCB* nuevaPCB;
cpu_config data_config;
int fd_kernel;
int fd_memoria;
int tamanioPagina=512; //Provisorio, en realidad lo deberia obtener del Kernel
bool stackOverflow = false;

/*Funciones para Implementar el PARSER (mas adelante emprolijamos y lo metemos en otro archivo)*/

/*Aca defino las primitivas que aparecen en la estructura AnSISOP_funciones adentro del
 * archivo parser.h. Lo unico que hace cada una es decir "soy tal primitiva" asi sabemos
 * a que primitivas se llamaron cuando le mandamos una instruccion a analizadorLinea()
 * (el twt es por the walking thread :))
 */

t_puntero twt_definirVariable (t_nombre_variable identificador_variable)
{
	//printf("Definir variable %c\n", identificador_variable);
	int var_pagina = nuevaPCB->primerPaginaStack; //pagina del stack donde guardo la variable
	int var_offset = nuevaPCB->stackPointer;	   //offset dentro de esa pagina


	//Si offset es mayor que pagina, voy aumentando pagina
	while(var_offset > tamanioPagina)
	{
	var_pagina++;
	var_offset = var_offset - tamanioPagina;
	}
	//Si al guardarla desbordo el stack, stack overflow:

	if((nuevaPCB->stackPointer) + sizeof(int) > (nuevaPCB->tamanioStack * tamanioPagina))
	{
		printf("Stack Overflow al definir variable %c\n", identificador_variable);
		stackOverflow=true;
		return -1;
	}

	//Obtengo el ultimo registro de stack
	registroStack* regStack = list_get(nuevaPCB->stack_index, nuevaPCB->stack_index->elements_count -1);

	if(regStack == NULL)
	{ 	// si no hay registros, creo uno nuevo
		regStack = malloc(sizeof(registroStack));
		regStack->vars = list_create();
		// Guardo el nuevo registro en el índice:
		list_add(nuevaPCB->stack_index, regStack);
	}

	variable* new_var = malloc(sizeof(variable));
	new_var->id = identificador_variable;
	new_var->page = var_pagina;
	new_var->offset = var_offset;
	new_var->size = sizeof(int);

	list_add(regStack->vars, new_var);

	printf("Agregue la variable: %c en: (pagina, offset, size) = (%i, %i, %i)\n",identificador_variable,var_pagina,var_offset,4);

	int posicionVariable = (nuevaPCB->primerPaginaStack * tamanioPagina) + (nuevaPCB->stackPointer);
	//Actualizo stackPointer
	nuevaPCB->stackPointer = (nuevaPCB->stackPointer) + 4;
	return posicionVariable;

	return 0;
}
t_puntero twt_obtenerPosicionVariable(t_nombre_variable identificador_variable)
{
	printf("Soy obtenerPosicionVariable\n");
	return 0;
}
t_valor_variable twt_dereferenciar (t_puntero direccion_variable)
{
	printf("Soy dereferenciar\n");
	return 0;
}
void twt_asignar (t_puntero direccion_variable, t_valor_variable valor)
{
	printf("Soy asignar\n");
	return;
}
t_valor_variable twt_obtenerValorCompartida (t_nombre_compartida variable)
{
	printf("Soy obtenerValorCompartida\n");
	return 0;
}
t_valor_variable twt_asignarValorCompartida (t_nombre_compartida variable, t_valor_variable valor)
{
	printf("Soy asignarValorCompartida\n");
	return 0;
}
void twt_irAlLabel (t_nombre_etiqueta t_nombre_etiqueta)
{
	printf("Soy irAlLabel\n");
	return;
}
void twt_llamarSinRetorno(t_nombre_etiqueta etiqueta)
{
	printf("Soy llamarSinRetorno\n");
	return;
}
void twt_llamarConRetorno (t_nombre_etiqueta etiqueta, t_puntero donde_retornar)
{
	printf("Soy llamarConRetorno\n");
	return;
}
void twt_finalizar (void)
{
	printf("Soy finalizar\n");
	return;
}
void twt_retornar(t_valor_variable retorno)
{
	printf("Soy retornar\n");
	return;
}

/*Y aca abajo defino las primitivas para las operaciones que ejecuta el Kernel
 * Estan en la estructura AnSISOP_kernel
 */
void twt_wait(t_nombre_semaforo identificador_semaforo)
{
	//Por ahora, twt_wait solo le manda al kernel el nombre del semaforo sobre el que hay
	//que hacer wait (serializado-----> (codigo,largo del msj, msj))
	int codigo = 50;
	int messageLength = strlen((char *) identificador_semaforo);
	void* buffer = malloc((sizeof(int)*2)+messageLength);
	memcpy(buffer, &codigo, sizeof(int));
	memcpy(buffer + sizeof(int), &messageLength, sizeof(int));
	memcpy(buffer + sizeof(int) + sizeof(int), (char *) identificador_semaforo, messageLength);

	if(send(fd_kernel,buffer,sizeof(int)*2+messageLength,0)==-1)
			{
				perror("send");
				exit(3);
			}
	free(buffer);
	return;
}
void twt_signal (t_nombre_semaforo identificador_semaforo)
{
	printf("Soy signal\n");
	return;
}
t_puntero twt_reservar (t_valor_variable espacio)
{
	printf("Soy reservar memoria\n");
	return 0;
}
void twt_liberar(t_puntero puntero)
{
	printf("Soy liberar memoria\n");
	return;
}
t_descriptor_archivo twt_abrir (t_direccion_archivo direccion, t_banderas flags)
{
	printf("Soy abrir archivo\n");
	return 0;
}
void twt_borrar (t_descriptor_archivo direccion)
{
	printf("Soy borrar archivo\n");
	return;
}
void twt_cerrar (t_descriptor_archivo descriptor_archivo)
{
	printf("Soy cerrar archivo\n");
	return;
}
void twt_moverCursor (t_descriptor_archivo descriptor_archivo, t_valor_variable posicion)
{
	printf("Soy moverCursor\n");
	return;
}
void twt_escribir (t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio)
{
	printf("Soy escribir archivo\n");
	return;
}
void twt_leer (t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio)
{
	printf("Soy leer archivo\n");
	return;
}

/*Lo que hay en AnSISOP_funciones y AnSISOP_kernel son punteros a funciones, entonces
 * los hago apuntar a mis funciones twt
 */
AnSISOP_funciones funciones =
	{
			.AnSISOP_definirVariable=twt_definirVariable,
			.AnSISOP_obtenerPosicionVariable=twt_obtenerPosicionVariable,
			.AnSISOP_dereferenciar=twt_dereferenciar,
			.AnSISOP_asignar=twt_asignar,
			.AnSISOP_obtenerValorCompartida=twt_obtenerValorCompartida,
			.AnSISOP_asignarValorCompartida=twt_asignarValorCompartida,
			.AnSISOP_irAlLabel=twt_irAlLabel,
			.AnSISOP_llamarConRetorno=twt_llamarConRetorno,
			.AnSISOP_finalizar=twt_finalizar,
			.AnSISOP_retornar=twt_retornar
	};
AnSISOP_kernel fcs_kernel =
	{
			.AnSISOP_wait=twt_wait,
			.AnSISOP_signal=twt_signal,
			.AnSISOP_reservar=twt_reservar,
			.AnSISOP_liberar=twt_liberar,
			.AnSISOP_abrir=twt_abrir,
			.AnSISOP_borrar=twt_borrar,
			.AnSISOP_cerrar=twt_cerrar,
			.AnSISOP_moverCursor=twt_moverCursor,
			.AnSISOP_escribir=twt_escribir,
			.AnSISOP_leer=twt_leer
	};

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

void verificar_conexion_socket(int fd, int estado){
	if(estado == -1){
		perror("recieve");
		exit(3);
		}
	if(estado == 0){
		printf("Se desconecto el socket: %d\n", fd);
		close(fd);
	}
}

//Funciones para que el main quede lindo
void message_handler_for_fd(int fd){
	int messageLength;
	void* realbuf;
	char* message;

	int bytes = recv(fd, &messageLength, sizeof(int), 0);
	if(bytes > 0){
		realbuf = malloc(messageLength+2);
		memset(realbuf,0,messageLength+2);
		recv(fd, realbuf, messageLength, 0);
		message = (char*) realbuf;
		message[messageLength+1]='\0';
		printf("Kernel dice: %d + %s \n", messageLength, message);
		free(realbuf);
	}else{
		if(bytes == -1){
			perror("recieve");
			exit(3);
			}
		if(bytes == 0){
			close(fd);
		}
	}
}

char* pedirCodigoAMemoria(u_int32_t pid, int page_counter)
{
	int codigo = 3;
	int messageLength, bytes;

	void* buffer = malloc(sizeof(int)+sizeof(u_int32_t)+sizeof(int));
	memcpy(buffer,&codigo,sizeof(int));
	memcpy(buffer+sizeof(int),&pid,sizeof(u_int32_t));
	memcpy(buffer+sizeof(int)+sizeof(u_int32_t),&page_counter,sizeof(int));

	send(fd_memoria,buffer,sizeof(int)+sizeof(int)+sizeof(u_int32_t),0);

	bytes = recv(fd_memoria,&messageLength,sizeof(int),0);
	verificar_conexion_socket(fd_memoria,bytes);

	void* aux = malloc(messageLength+2);
	memset(aux,0,messageLength+2);
	bytes =recv(fd_memoria, aux, messageLength, 0);
	verificar_conexion_socket(fd_memoria,bytes);
	memset(aux+messageLength+1,'\0',1);

	char* recibido = (char*) aux;
	free(buffer);
	return recibido;
}

char* obtener_instruccion(PCB *pcb){
	int codigo = 3;
	int messageLength, bytes, instruccion_a_buscar, inicio, offset, pagina_de_codigo = 0;

	instruccion_a_buscar = pcb->program_counter;
	inicio = pcb->indice_de_codigo[instruccion_a_buscar].inicio;
	offset = pcb->indice_de_codigo[instruccion_a_buscar].offset;

	void *buffer = malloc(sizeof(u_int32_t)+ sizeof(int)*4);
	memcpy(buffer,&codigo,sizeof(int));
	memcpy(buffer+sizeof(int),&(pcb->pid),sizeof(u_int32_t));
	memcpy(buffer+sizeof(int)+sizeof(u_int32_t), &pagina_de_codigo, sizeof(int));
	memcpy(buffer+sizeof(int)*2+sizeof(u_int32_t), &inicio, sizeof(int));
	memcpy(buffer+sizeof(int)*3+sizeof(u_int32_t), &offset, sizeof(int));

	send(fd_memoria, buffer, sizeof(u_int32_t)+ sizeof(int)*4,0);

	bytes = recv(fd_memoria,&messageLength,sizeof(int),0);
	verificar_conexion_socket(fd_memoria,bytes);

	char* instruccion = malloc(messageLength+1);

	bytes = recv(fd_memoria, instruccion, messageLength+1, 0);
	verificar_conexion_socket(fd_memoria,bytes);

	free(buffer);

	//Actualizamos el Program counter
	pcb->program_counter += 1;

	return instruccion;
}

void handshake(int idProceso, int fd){
	void* codbuf = malloc(sizeof(int)*2);
	int codigo = 1;
	memcpy(codbuf,&codigo,sizeof(int));
	memcpy(codbuf + sizeof(int),&idProceso, sizeof(int));
	send(fd, codbuf, sizeof(int)*2, 0);
	free(codbuf);

}

PCB* recibirPCB()
{
	int bytes_recv;

	u_int32_t pid;

	int page_counter, direccion_inicio_codigo, program_counter, cantidad_de_instrucciones,
	stack_size, primerPagStack, stack_pointer;

	PCB* pcb = malloc(sizeof(PCB));

	int tamanio_indice_codigo, tamanio_indice_stack;

	bytes_recv = recv(fd_kernel, &pid, sizeof(u_int32_t),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv = recv(fd_kernel, &page_counter, sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv = recv(fd_kernel, &direccion_inicio_codigo, sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv = recv(fd_kernel, &program_counter, sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv = recv(fd_kernel, &cantidad_de_instrucciones, sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv = recv(fd_kernel, &tamanio_indice_codigo, sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);

	entrada_indice_de_codigo *indice_de_codigo = malloc(tamanio_indice_codigo);


	bytes_recv = recv(fd_kernel, indice_de_codigo, tamanio_indice_codigo,0);
	verificar_conexion_socket(fd_kernel,bytes_recv);

	bytes_recv=recv(fd_kernel, &stack_size,sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv=recv(fd_kernel, &primerPagStack,sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);
	bytes_recv=recv(fd_kernel, &stack_pointer,sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);

	bytes_recv = recv(fd_kernel, &tamanio_indice_stack, sizeof(int),0);
	verificar_conexion_socket(fd_kernel,bytes_recv);

	t_list* indice_de_stack = malloc(tamanio_indice_stack);

	bytes_recv = recv(fd_kernel, indice_de_stack, tamanio_indice_stack,0);
	verificar_conexion_socket(fd_kernel,bytes_recv);

	pcb->pid = pid;
	pcb->page_counter = page_counter;
	pcb->direccion_inicio_codigo = direccion_inicio_codigo;
	pcb->program_counter = program_counter;
	pcb->cantidad_de_instrucciones = cantidad_de_instrucciones;
	pcb->indice_de_codigo = indice_de_codigo;
	pcb->tamanioStack = stack_size;
	pcb->primerPaginaStack=primerPagStack;
	pcb->stackPointer=stack_pointer;
	pcb->stack_index=indice_de_stack;

	return pcb;
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


int main(int argc, char **argv) {


	// SETTEO DESDE ARCHIVO DE CONFIGURACION

	//Variables para config
	t_config *config_file;

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../CPU/") + strlen(argv[1])+1);
	*cfgPath = '\0';
	strcpy(cfgPath, "../../CPU/");

	config_file = config_create_from_relative_with_check(argv, cfgPath);

	data_config.ip_kernel = config_get_string_value(config_file, "IP_KERNEL");
	data_config.puerto_kernel = config_get_string_value(config_file, "PUERTO_KERNEL");
	data_config.ip_memoria = config_get_string_value(config_file, "IP_MEMORIA");
	data_config.puerto_memoria = config_get_string_value(config_file, "PUERTO_MEMORIA");

	printf("IP Kernel: %s\n",data_config.ip_kernel);
	printf("Puerto Kernel: %s\n\n",data_config.puerto_kernel);
	printf("IP Memoria: %s\n",data_config.ip_memoria);
	printf("Puerto Memoria: %s\n",data_config.puerto_memoria);

	// CONEXION A KERNEL



	int idProceso = 1;

	//Me aseguro que hints este vacio, lo necesito limpito o el getaddrinfo se puede poner chinchudo

	fd_kernel = get_fd_server(data_config.ip_kernel,data_config.puerto_kernel);

	handshake(idProceso,fd_kernel);

	fd_memoria = get_fd_server(data_config.ip_memoria,data_config.puerto_memoria);

	//Y aqui termina la CPU, esperando e imprimiendo mensajes hasta el fin de los tiempos
	//O hasta que cierres el programa
	//Lo que pase primero

	//analizadorLinea la pongo solo para probar si llama a las primitivas
	//analizadorLinea("variables a, b", &funciones, &fcs_kernel);


	while(1)
	{
		nuevaPCB = recibirPCB();
		print_PCB(nuevaPCB);

		while((nuevaPCB->program_counter) < (nuevaPCB->cantidad_de_instrucciones)){
			char *instruccion = obtener_instruccion(nuevaPCB);
			printf("Instruccion: %s\n", instruccion);
			analizadorLinea(instruccion, &funciones, &fcs_kernel);
			free(instruccion);
		}
		/*registroStack* blabla = malloc(sizeof(registroStack));
		blabla=list_get(nuevaPCB->stack_index,nuevaPCB->stack_index->elements_count-1);
		printf("(%c,%d,%d,%d)",list_get(blabla->vars,0));
		free(blabla);*/
	}


	close(fd_kernel);
	close(fd_memoria);
	free(cfgPath);

	return 0;
}
