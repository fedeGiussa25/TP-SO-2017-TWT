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
#include <parser/parser.h>
#include <commons/collections/list.h>
#include "../shared_libs/PCB.h"
#include <ctype.h>



enum{
	HANDSHAKE = 1,
	PEDIR_INSTRUCCION=3,
	ASIGNAR_VALOR = 4,
	ELIMINAR_PROCESO = 5,
	ASIGNAR_VALOR_COMPARTIDA = 5,
	BUSCAR_VALOR = 6,
	WAIT = 7,
	SIGNAL = 8,
	RESERVAR_MEMORIA = 18,
	LIBERAR_MEMORIA = 19,
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
	READ_WRITE_CREATE = 7
};

PCB* nuevaPCB;
cpu_config data_config;
int fd_kernel;
int fd_memoria;
int tamanioPagina; //Provisorio, en realidad lo deberia obtener de la memoria cuando se conectan
bool stackOverflow;
bool programaTerminado;
bool procesoBloqueado;
bool procesoAbortado;

/*Funciones para Implementar el PARSER (mas adelante emprolijamos y lo metemos en otro archivo)*/

/*Aca defino las primitivas que aparecen en la estructura AnSISOP_funciones adentro del
 * archivo parser.h. Lo unico que hace cada una es decir "soy tal primitiva" asi sabemos
 * a que primitivas se llamaron cuando le mandamos una instruccion a analizadorLinea()
 * (el twt es por the walking thread :))
 */

void eliminar_proceso(u_int32_t PID){

	u_int32_t codigo = ELIMINAR_PROCESO;
	void *buffer = malloc(2*sizeof(u_int32_t));

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &PID, sizeof(u_int32_t));

	send(fd_memoria, buffer, sizeof(u_int32_t)*2, 0);

}

void liberar_registro_stack(registroStack *registro){
	int i;
	int cant_vars = list_size(registro->vars);
	//int cant_args = list_size(registro->args);

	for(i=0; i< cant_vars; i++){
		variable *unaVar = list_remove(registro->vars, 0);
		nuevaPCB->stackPointer = nuevaPCB->stackPointer - sizeof(int); //Decremento el stackPointer 4 bytes (un argumento)
		free(unaVar);
	}
/*
	for(j=0; j< cant_args; j++){
		variable *unArg = list_remove(registro->args, 0);
		nuevaPCB->stackPointer = nuevaPCB->stackPointer - sizeof(int); //Decremento el stackPointer 4 bytes (una variable)
		free(unArg);
	}
*/
	free(registro);
}

t_puntero twt_definirVariable (t_nombre_variable identificador_variable)
{
	printf("Definir variable: %c\n", identificador_variable);
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
		regStack->args = list_create();
		// Guardo el nuevo registro en el índice:
		list_add(nuevaPCB->stack_index, regStack);
	}

	variable* new_var = malloc(sizeof(variable));
	new_var->id = identificador_variable;
	new_var->page = var_pagina;
	new_var->offset = var_offset;
	new_var->size = sizeof(int);

	if(isdigit(identificador_variable)) //Si es un digito, es un argumento
	{
		list_add(regStack->args, new_var);
		printf("Agregue el argumento: %c en: (pagina, offset, size) = (%i, %i, %i)\n",identificador_variable,var_pagina,var_offset,4);
	} else
	{
		list_add(regStack->vars, new_var);
		printf("Agregue la variable: %c en: (pagina, offset, size) = (%i, %i, %i)\n",identificador_variable,var_pagina,var_offset,4);
	}

	int posicionVariable = (nuevaPCB->primerPaginaStack * tamanioPagina) + (nuevaPCB->stackPointer);
	//Actualizo stackPointer
	nuevaPCB->stackPointer = (nuevaPCB->stackPointer) + 4;

	return posicionVariable;


}
t_puntero twt_obtenerPosicionVariable(t_nombre_variable identificador_variable)
{
	printf("Soy obtenerPosicionVariable para: %c\n", identificador_variable);
	//Voy al contexto de ejecucion actual (ultimo registro de stack):
	registroStack* registroActual = list_get(nuevaPCB->stack_index, nuevaPCB->stack_index->elements_count -1);

	int i;
	int posicion_variable;

	if(isdigit(identificador_variable)) //Si es un digito, es un argumento
	{
	//Recorro la lista de argumentos hasta encontrarla:
		for(i=0;i<registroActual->args->elements_count;i++)
		{
			variable* argumento = list_get(registroActual->args, i);

			if(argumento->id == identificador_variable) //Cuando lo encuentra:
			{
				posicion_variable = (argumento->page * tamanioPagina) + argumento->offset;
				return posicion_variable;
			}
		}
		} else //Si no, es una variable
		{
		//Recorro la lista de variables hasta encontrarla:
		for(i=0;i<registroActual->vars->elements_count;i++)
		{
			variable* variable = list_get(registroActual->vars, i);

			if(variable->id == identificador_variable) //Cuando la encuentra:
			{
				posicion_variable = (variable->page * tamanioPagina) + variable->offset;
				return posicion_variable;
			}
		}
	}
	return -1;
}
t_valor_variable twt_dereferenciar (t_puntero direccion_variable)
{
	//Esta primitiva devuelve el valor que hay en la direccion de memoria que recibe

	printf("Soy dereferenciar para la direccion: %d\n", direccion_variable);

	//Mandamos a memoria la solicitud de lectura:

	int codigo = BUSCAR_VALOR; //Le puse 6 al codigo para solicitar leer un valor, si quieren lo cambian
	int pag = direccion_variable / tamanioPagina;
	int offset = direccion_variable % tamanioPagina;
	int tamanio = sizeof(int);

	void* buffer = malloc(sizeof(u_int32_t)+sizeof(int)*4);

	memcpy(buffer, &codigo, sizeof(int));
	memcpy(buffer+sizeof(int), &nuevaPCB->pid, sizeof(u_int32_t));
	memcpy(buffer+sizeof(int)+sizeof(u_int32_t), &pag, sizeof(int));
	memcpy(buffer+sizeof(int)*2+sizeof(u_int32_t), &offset, sizeof(int));
	memcpy(buffer+sizeof(int)*3+sizeof(u_int32_t), &tamanio, sizeof(int));

	send(fd_memoria, buffer, sizeof(u_int32_t)+sizeof(int)*4,0);

	//Obtengo el valor:

	int valorVariable;

	recv(fd_memoria, &valorVariable,sizeof(int),0);

	printf("La variable tiene valor %d\n", valorVariable);

	return valorVariable;
}
void twt_asignar (t_puntero direccion_variable, t_valor_variable valor)
{
	/*El parametro direccion_variable es lo que devuelve la primitiva obtenerPosicion, que no esta hecha
	 * por eso vale 0*/

	printf("Soy asignar\n");
	int pagina, offset, value;
	int codigo = ASIGNAR_VALOR; //codigo 4 es solicitud escritura a memoria

	printf("Tengo que asignar el valor: %d en: %d\n", valor, direccion_variable);

	pagina = direccion_variable / tamanioPagina;
	offset = direccion_variable % tamanioPagina; //el resto de la division
	value = valor;

	//Le envio a Memoria:
	void* buffer = malloc(sizeof(int)*4+sizeof(u_int32_t));

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &nuevaPCB->pid, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int), &pagina, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*2, &offset, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*3, &value, sizeof(int));

	send(fd_memoria, buffer, sizeof(int)*4+sizeof(u_int32_t),0);

	printf("Envie a guardar en memoria: (pag, off, valor) = (%d,%d,%d)\n", pagina, offset, value);

	free(buffer);
	return;
}
t_valor_variable twt_obtenerValorCompartida (t_nombre_compartida variable)
{
	printf("Soy obtenerValorCompartida\n");
	printf("Tengo que obtener el valor de %s\n",variable);

	int value;
	int codigo = BUSCAR_VALOR;
	int largo = strlen(variable)+1;
	void *buffer = malloc(sizeof(u_int32_t)*2 + largo);

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &largo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t)*2, variable, largo);

	send(fd_kernel, buffer, sizeof(u_int32_t)*2 + largo, 0);

	recv(fd_kernel, &value, sizeof(u_int32_t),0);

	printf("La variable %s tiene valor %d\n",variable,value);

	return value;
}
t_valor_variable twt_asignarValorCompartida (t_nombre_compartida variable, t_valor_variable valor)
{
	printf("Soy asignarValorCompartida\n");
	printf("Tengo que asignar el valor: %d en: %s\n", valor, variable);

/*	int aldope;
	recv(fd_kernel, &aldope, sizeof(u_int32_t), 0);
	printf("El loco del kernel me tiro un: %d\n",aldope);*/

	int codigo = ASIGNAR_VALOR_COMPARTIDA;
	int largo = strlen(variable)+1;
	void *buffer = malloc(sizeof(u_int32_t)*3 + largo);

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &valor, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t)*2, &largo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t)*3, variable, largo);

	send(fd_kernel, buffer, sizeof(u_int32_t)*3 + largo, 0);

	free(buffer);
	return 0;
}
void twt_irAlLabel (t_nombre_etiqueta t_nombre_etiqueta)
{
	printf("Soy irAlLabel para etiqueta: %s\n", t_nombre_etiqueta);
	t_puntero_instruccion posicionEtiqueta = metadata_buscar_etiqueta(t_nombre_etiqueta,nuevaPCB->lista_de_etiquetas,nuevaPCB->lista_de_etiquetas_length);
	nuevaPCB->program_counter = posicionEtiqueta;
	printf("Cambio el PC a: %d\n", posicionEtiqueta);

	return;
}
void twt_llamarSinRetorno(t_nombre_etiqueta etiqueta)
{
	printf("Soy llamarSinRetorno para funcion: %s\n", etiqueta);
	registroStack* nuevoReg = malloc(sizeof(registroStack));
	nuevoReg->args=list_create();
	nuevoReg->vars=list_create();
	nuevoReg->ret_pos = nuevaPCB->program_counter;
	list_add(nuevaPCB->stack_index,nuevoReg);
	twt_irAlLabel(etiqueta);
	return;

}
void twt_llamarConRetorno (t_nombre_etiqueta etiqueta, t_puntero donde_retornar)
{
	printf("Soy llamarConRetorno para funcinon: %s \n", etiqueta);
	//Crea un nuevo contexto de ejecucion
	registroStack* nuevoReg = malloc(sizeof(registroStack));
	nuevoReg->args=list_create();
	nuevoReg->vars=list_create();

	nuevoReg->ret_pos = nuevaPCB->program_counter;
	nuevoReg->ret_var.page=donde_retornar / tamanioPagina;
	nuevoReg->ret_var.offset=donde_retornar % tamanioPagina;
	nuevoReg->ret_var.size=sizeof(int);

	list_add(nuevaPCB->stack_index,nuevoReg);

	twt_irAlLabel(etiqueta);

	return;

}

void twt_finalizar (void)
{
	printf("Soy finalizar\n");

	registroStack* registroActual = list_remove(nuevaPCB->stack_index,nuevaPCB->stack_index->elements_count-1);


	if(list_size(nuevaPCB->stack_index)==0) //Si finalizó el main (begin en ansisop)
	{
		programaTerminado = true;
		printf("Finalizo el programa\n");
	} else
	{
		nuevaPCB->program_counter = registroActual->ret_pos;
		printf("Al finalizar la funcion, el PC vuelve a: %d\n", nuevaPCB->program_counter);
	}

	liberar_registro_stack(registroActual);
	printf("Se ha ejecutado el end\n");
	return;
}
void twt_retornar(t_valor_variable retorno)
{
	printf("Soy retornar con este valor: %i\n", retorno);
	//Obtengo registro actual:

	registroStack* regActual = list_get(nuevaPCB->stack_index,nuevaPCB->stack_index->elements_count-1);

	t_puntero direccionRetorno = regActual->ret_var.page * tamanioPagina + regActual->ret_var.offset;

	twt_asignar(direccionRetorno, retorno);
	return;
}

/*Y aca abajo defino las primitivas para las operaciones que ejecuta el Kernel
 * Estan en la estructura AnSISOP_kernel
 */
void twt_wait(t_nombre_semaforo identificador_semaforo)
{
	printf("Soy wait\n");
	uint32_t codigo = WAIT;
	int32_t valor;
	uint32_t messageLength = strlen((char *) identificador_semaforo) + 1;
	void* buffer = malloc((sizeof(uint32_t)*3)+messageLength);

	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &nuevaPCB->pid, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, &messageLength, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*3, (char *) identificador_semaforo, messageLength);

	send(fd_kernel,buffer,sizeof(int)*3+messageLength,0);

	free(buffer);

	recv(fd_kernel, &valor, sizeof(int32_t), 0);

	if(valor < 0){
		procesoBloqueado = true;
		printf("Upa, se bloqueo el proceso!\n");
	}

	return;
}
void twt_signal (t_nombre_semaforo identificador_semaforo)
{
	printf("Soy signal\n");

	uint32_t codigo = SIGNAL;
	uint32_t messageLength = strlen((char *) identificador_semaforo) + 1;
	void* buffer = malloc((sizeof(uint32_t)*2)+messageLength);

	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &messageLength, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, (char *) identificador_semaforo, messageLength);

	send(fd_kernel,buffer,sizeof(int)*2+messageLength,0);
	free(buffer);

	return;
}
t_puntero twt_reservar (t_valor_variable espacio)
{
	printf("Soy reservar memoria\n");
	uint32_t codigo = RESERVAR_MEMORIA;
	int32_t puntero;
	void *buffer = malloc(sizeof(uint32_t)*2 + sizeof(int));
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, &espacio, sizeof(int));
	enviar(fd_kernel, buffer, sizeof(int)+ sizeof(uint32_t)*2);
	recibir(fd_kernel, &puntero, sizeof(int32_t));

	if(puntero < 0){
		procesoAbortado=true;
	}

	return puntero;
}
void twt_liberar(t_puntero puntero)
{
	printf("Soy liberar memoria\n");
	uint32_t resp, codigo = LIBERAR_MEMORIA;
	void *buffer = malloc(sizeof(uint32_t)*2 + sizeof(u_int32_t));
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, &puntero, sizeof(u_int32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)+ sizeof(uint32_t)*2);
	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
	}

	return;
}
t_descriptor_archivo twt_abrir (t_direccion_archivo direccion, t_banderas flags)
{
	printf("Soy abrir archivo con el path: %s\n", direccion);
	uint32_t codigo = ABRIR_ARCHIVO;
	uint32_t path_length = strlen(direccion)+1;
	void* buffer = malloc(sizeof(uint32_t)*3 + path_length + sizeof(bool)*3);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, &path_length, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*3, direccion, path_length);

	//Envio los permisos
	memcpy(buffer + sizeof(uint32_t)*3+path_length, &(flags.creacion), sizeof(bool));
	memcpy(buffer + sizeof(uint32_t)*3+path_length+sizeof(bool), &(flags.lectura), sizeof(bool));
	memcpy(buffer + sizeof(uint32_t)*3+path_length+sizeof(bool)*2, &(flags.escritura), sizeof(bool));
	t_descriptor_archivo fd_archivo;

	enviar(fd_kernel, buffer, sizeof(u_int32_t)*3+path_length+sizeof(bool)*3);

	recibir(fd_kernel, &fd_archivo, sizeof(uint32_t));

	if(fd_archivo < 0){
		procesoAbortado=true;
	}

	return fd_archivo;
}
void twt_borrar (t_descriptor_archivo direccion)
{
	printf("Soy borrar para el archivo:%d\n", direccion);
	uint32_t fd_a_borrar = direccion;
	uint32_t codigo = BORRAR_ARCHIVO;
	uint32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*3);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &fd_a_borrar, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)*3);
	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
	}

	free(buffer);
	return;
}
void twt_cerrar (t_descriptor_archivo descriptor_archivo)
{
	printf("Soy cerrar archivo: %d\n", descriptor_archivo);
	uint32_t codigo = CERRAR_ARCHIVO;
	uint32_t fd_a_cerrar = descriptor_archivo;
	uint32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*3);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &fd_a_cerrar, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)*3);

	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
		printf("No se pudo cerrar, respuesta obtenida: %d\n", resp);
	}
	free(buffer);
	return;
}
void twt_moverCursor (t_descriptor_archivo descriptor_archivo, t_valor_variable posicion)
{
	printf("Soy moverCursor para el archivo: %d a posicion: %d\n", descriptor_archivo, posicion);
	uint32_t codigo = MOVER_CURSOR;
	uint32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*4);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &descriptor_archivo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*3, &posicion, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)*4);

	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
	}
	free(buffer);
	return;
}
void twt_escribir (t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio)
{
	memset(informacion+tamanio, '\0', 1);
	printf("Soy escribir en archivo: %d la informacion: %s con tamanio: %d\n", descriptor_archivo, informacion, tamanio);
	int desc_salida=descriptor_archivo;
	uint32_t resp;
	if(desc_salida==0)
	{
		desc_salida = 1; //El parser devuelve 0	como FD de salida, no se por que
	}
	uint32_t codigo = ESCRIBIR_ARCHIVO;
	void* buffer = malloc(sizeof(uint32_t)*4+tamanio);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &desc_salida, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*3, &tamanio, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*4, informacion, tamanio);
	enviar(fd_kernel, buffer, sizeof(uint32_t)*4+tamanio);

	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
		printf("No se pudo escribir, respuesta obtenida: %d\n", resp);
	}
	free(buffer);
	return;
}
void twt_leer (t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio)
{
	printf("Soy leer el archivo: %d\n", descriptor_archivo);
	uint32_t codigo = LEER_ARCHIVO;
	uint32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*4);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &descriptor_archivo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*3, &tamanio, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(uint32_t)*4);
	void* informacion_leida; //Suponiendo que los archivos guardan solo enteros

	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
		printf("No se pudo leer, respuesta obtenida: %d\n", resp);
	} else{
	recibir(fd_kernel, &informacion_leida, tamanio);
	//twt_asignar(informacion, informacion_leida);     //Voy a preguntar bien si esto es asi
	}
	free(buffer);
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
	int codigo = PEDIR_INSTRUCCION;
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
	int cod = 1, tamanio;
	send(fd_memoria, &cod, sizeof(int), 0);
	recv(fd_memoria, &tamanio, sizeof(int), 0);
	tamanioPagina = tamanio;

	//Y aqui termina la CPU, esperando e imprimiendo mensajes hasta el fin de los tiempos
	//O hasta que cierres el programa
	//Lo que pase primero

	uint32_t codigo, quantum_sleep;
	int32_t  quantum;

	while(1)
	{
		recv(fd_kernel, &quantum, sizeof(int32_t), 0);
		recv(fd_kernel, &quantum_sleep, sizeof(uint32_t), 0);
		recv(fd_kernel, &codigo, sizeof(uint32_t),0);
		printf("Se recibio codigo: %d\n", codigo); //Cuando CPU reciba codigos diferentes a un PCB, recibimos dependiendo el codigo
		nuevaPCB = recibirPCB(fd_kernel);
		printf("RECIBI PCB\n");
		print_PCB(nuevaPCB);
		stackOverflow = false;
		programaTerminado = false;
		procesoBloqueado = false;
		procesoAbortado = false;

		if(quantum < 0){
			printf("Estoy ejecutando en FIFO\n");
			while((nuevaPCB->program_counter) < (nuevaPCB->cantidad_de_instrucciones) && (programaTerminado == false) && (stackOverflow==false) && (procesoBloqueado == false) && (procesoAbortado == false))
			{
				char *instruccion = obtener_instruccion(nuevaPCB);
				printf("Instruccion: %s\n", instruccion);
				analizadorLinea(instruccion, &funciones, &fcs_kernel);
				free(instruccion);
			}
			if(programaTerminado == true){
				codigo = 10;
				send_PCB(fd_kernel, nuevaPCB, codigo);
			}
			else{
				codigo = 100;
				send_PCB(fd_kernel, nuevaPCB, codigo);
			}
		}
		else{
			printf("Estoy ejecutando en Round Robin\n");
			while((quantum > 0)/* && (nuevaPCB->program_counter) < (nuevaPCB->cantidad_de_instrucciones)*/ && (programaTerminado == false) && (stackOverflow==false) && (procesoBloqueado == false) && (procesoAbortado == false))
			{
				char *instruccion = obtener_instruccion(nuevaPCB);
				printf("Instruccion: %s\n", instruccion);
				analizadorLinea(instruccion, &funciones, &fcs_kernel);
				free(instruccion);
				quantum --;
			}
			if((programaTerminado == true) || (stackOverflow==true) || (procesoBloqueado == true)){
				codigo = 10;
				send_PCB(fd_kernel, nuevaPCB, codigo);
			}
			if((quantum == 0) && (programaTerminado == false) && (stackOverflow==false) && (procesoBloqueado == false)){
				codigo = 13;
				send_PCB(fd_kernel, nuevaPCB, codigo);
			}
		}
	}

	close(fd_kernel);
	close(fd_memoria);
	free(cfgPath);

	return 0;
}
