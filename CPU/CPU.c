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
#include <commons/log.h>



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
	READ_WRITE_CREATE = 7,
	//Finalizaciones
	PROCESO_FINALIZO_CORRECTAMENTE = 10,
	FIN_DE_QUANTUM = 13,
	PROCESO_FINALIZO_ERRONEAMENTE = 22
};

PCB* nuevaPCB;
cpu_config data_config;
int fd_kernel;
int fd_memoria;
int tamanioPagina;
bool excepcionMemoria;
bool programaTerminado;
bool procesoBloqueado;
bool procesoAbortado;
int32_t codigo_error;
t_log* messagesLog;

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
	//printf("Definir variable: %c\n", identificador_variable);
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
		//printf("Stack Overflow al definir variable %c\n", identificador_variable);
		log_info(messagesLog, "StackOverflow al definir la variable o argumento: %c\n", identificador_variable);
		excepcionMemoria=true;
		codigo_error = -20;
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
		log_info(messagesLog, "Definir argumento: %c en: (pagina, offset)=(%d,%d)\n", identificador_variable, var_pagina, var_offset);
		list_add(regStack->args, new_var);
		//printf("Agregue el argumento: %c en: (pagina, offset, size) = (%i, %i, %i)\n",identificador_variable,var_pagina,var_offset,4);
	} else
	{
		log_info(messagesLog, "Definir variable: %c en: (pagina, offset)=(%d,%d)\n", identificador_variable, var_pagina, var_offset);
		list_add(regStack->vars, new_var);
		//printf("Agregue la variable: %c en: (pagina, offset, size) = (%i, %i, %i)\n",identificador_variable,var_pagina,var_offset,4);
	}

	int posicionVariable = (nuevaPCB->primerPaginaStack * tamanioPagina) + (nuevaPCB->stackPointer);
	//Actualizo stackPointer
	nuevaPCB->stackPointer = (nuevaPCB->stackPointer) + 4;

	return posicionVariable;


}
t_puntero twt_obtenerPosicionVariable(t_nombre_variable identificador_variable)
{
	//Voy al contexto de ejecucion actual (ultimo registro de stack):
	registroStack* registroActual = list_get(nuevaPCB->stack_index, nuevaPCB->stack_index->elements_count -1);

	int i;
	int posicion_variable;

	if(isdigit(identificador_variable)) //Si es un digito, es un argumento
	{
		log_info(messagesLog, "Obtener posicion del argumento: %c\n", identificador_variable);
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
		log_error(messagesLog, "No existe el argumento: %c\n", identificador_variable);
		} else //Si no, es una variable
		{
			log_info(messagesLog, "Obtener posicion de la variable: %c\n", identificador_variable);
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
		log_error(messagesLog, "No existe la variable: %c\n", identificador_variable);
	}
	return -1;
}
t_valor_variable twt_dereferenciar (t_puntero direccion_variable)
{
	if(excepcionMemoria == false){
		log_info(messagesLog,"Dereferenciar la direccion: %d\n", direccion_variable);

		//Mandamos a memoria la solicitud de lectura:

		//int codigo = BUSCAR_VALOR;
		int codigo = PEDIR_INSTRUCCION;
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
		int valorVariable,error;
		recv(fd_memoria, &error, sizeof(int),0);
		if(error > 0){
			recv(fd_memoria, &tamanio,sizeof(int),0);
			//recv(fd_memoria, &valorVariable,sizeof(int),0);
			recv(fd_memoria, &valorVariable,tamanio+1,0);
			log_info(messagesLog,"La variable fue dereferenciada, tiene valor: %d\n", valorVariable);
		}else{
			printf("La posicion accedida es invalida, se terminara el proceso en breve\n");
			log_error(messagesLog,"La posicion accedida es invalida\n");
			valorVariable = -1;
			excepcionMemoria = true;
		}

		return valorVariable;
	}else{
		return -1;
	}
}
void twt_asignar (t_puntero direccion_variable, t_valor_variable valor)
{
	/*El parametro direccion_variable es lo que devuelve la primitiva obtenerPosicion, que no esta hecha
	 * por eso vale 0*/

	int pagina, offset, value;
	int codigo = ASIGNAR_VALOR; //codigo 4 es solicitud escritura a memoria

	pagina = direccion_variable / tamanioPagina;
	offset = direccion_variable % tamanioPagina; //el resto de la division
	value = valor;

	//Le envio a Memoria:
	void* buffer = malloc(sizeof(int)*5+sizeof(u_int32_t));

	int size_dato = sizeof(int);

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &nuevaPCB->pid, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int), &pagina, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*2, &offset, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*3, &size_dato, sizeof(int));
	memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*4, &value, size_dato);

	send(fd_memoria, buffer, sizeof(int)*5+sizeof(u_int32_t),0);

	log_info(messagesLog,"Asignar valor %d en pagina: %d offset: %d\n", value, pagina, offset);

	free(buffer);
	return;
}
t_valor_variable twt_obtenerValorCompartida (t_nombre_compartida variable)
{
	log_info(messagesLog,"Solicitar al Kernel el valor de la variable compartida: %s\n", variable);
	int32_t value;
	int codigo = BUSCAR_VALOR;
	int largo = strlen(variable)+1;
	void *buffer = malloc(sizeof(u_int32_t)*2 + largo);
	int32_t respuesta;

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &largo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t)*2, variable, largo);

	send(fd_kernel, buffer, sizeof(u_int32_t)*2 + largo, 0);

	recv(fd_kernel, &respuesta, sizeof(int32_t), 0);

	if(respuesta>0){

	recv(fd_kernel, &value, sizeof(int32_t),0);

	log_info(messagesLog,"Valor de la variable compartida %s: %d\n", variable, value);
	} else {
		procesoAbortado=true;
		log_error(messagesLog,"No existe la variable compartida: %s\n", variable);
	}
	return value;
}
t_valor_variable twt_asignarValorCompartida (t_nombre_compartida variable, t_valor_variable valor)
{
	log_info(messagesLog,"Asignar el valor: %d en la variable compartida: %s\n", valor, variable);

	int codigo = ASIGNAR_VALOR_COMPARTIDA;
	int largo = strlen(variable)+1;
	void *buffer = malloc(sizeof(u_int32_t)*3 + largo);
	int32_t respuesta;

	memcpy(buffer, &codigo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t), &valor, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t)*2, &largo, sizeof(u_int32_t));
	memcpy(buffer+sizeof(u_int32_t)*3, variable, largo);

	send(fd_kernel, buffer, sizeof(u_int32_t)*3 + largo, 0);

	recv(fd_kernel, &respuesta, sizeof(int32_t), 0);
	if(respuesta < 0)
	{
		procesoAbortado = true;
		log_error(messagesLog, "Proceso: %d abortado, variable compartida inexistente", nuevaPCB->pid);
	}

	free(buffer);
	return 0;
}
void twt_irAlLabel (t_nombre_etiqueta t_nombre_etiqueta)
{
	log_info(messagesLog,"Ir al label: %s\n", t_nombre_etiqueta);
	t_puntero_instruccion posicionEtiqueta = metadata_buscar_etiqueta(t_nombre_etiqueta,nuevaPCB->lista_de_etiquetas,nuevaPCB->lista_de_etiquetas_length);
	nuevaPCB->program_counter = posicionEtiqueta;
	log_info(messagesLog,"Program Counter actualizado a: %d\n", posicionEtiqueta);

	return;
}
void twt_llamarSinRetorno(t_nombre_etiqueta etiqueta) //NO SE TIENE QUE IMPLEMENTAR
{
	/*printf("Soy llamarSinRetorno para funcion: %s\n", etiqueta);
	registroStack* nuevoReg = malloc(sizeof(registroStack));
	nuevoReg->args=list_create();
	nuevoReg->vars=list_create();
	nuevoReg->ret_pos = nuevaPCB->program_counter;
	list_add(nuevaPCB->stack_index,nuevoReg);
	twt_irAlLabel(etiqueta);
	return;*/

}
void twt_llamarConRetorno (t_nombre_etiqueta etiqueta, t_puntero donde_retornar)
{
	log_info(messagesLog,"Llamar con retorno a la funcion: %s\n", etiqueta);
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
	registroStack* registroActual = list_remove(nuevaPCB->stack_index,nuevaPCB->stack_index->elements_count-1);


	if(list_size(nuevaPCB->stack_index)==0) //Si finalizó el main (begin en ansisop)
	{
		programaTerminado = true;
		log_info(messagesLog,"Finalizar contexto de ejecucion del main\n");
	} else
	{
		nuevaPCB->program_counter = registroActual->ret_pos;
		log_info(messagesLog,"Finalizar contexto de ejecucion de la funcion\n");
		log_info(messagesLog,"Program Counter actualizado a: %d\n", nuevaPCB->program_counter);
	}

	liberar_registro_stack(registroActual);
	return;
}
void twt_retornar(t_valor_variable retorno)
{
	log_info(messagesLog,"La funcion retorna el valor: %d\n", retorno);
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
	log_info(messagesLog,"Proceso: %d hace wait en el semaforo: %s\n", nuevaPCB->pid, identificador_semaforo);
	uint32_t codigo = WAIT;
	int32_t valor, respuesta;
	uint32_t messageLength = strlen((char *) identificador_semaforo) + 1;
	void* buffer = malloc((sizeof(uint32_t)*3)+messageLength);

	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &nuevaPCB->pid, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, &messageLength, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*3, (char *) identificador_semaforo, messageLength);

	send(fd_kernel,buffer,sizeof(int)*3+messageLength,0);

	free(buffer);

	recv(fd_kernel, &respuesta, sizeof(int32_t), 0);

	if(respuesta>0)
	{
		recv(fd_kernel, &valor, sizeof(int32_t), 0);

		if(valor < 0)
		{
			procesoBloqueado = true;
			log_info(messagesLog,"El proceso: %d se bloqueo al hacer wait en el semaforo: %s\n", nuevaPCB->pid, identificador_semaforo);
		}
	}
	else
	{
		procesoAbortado=true;
		log_error(messagesLog, "El proceso: %d fue abortado, semaforo %s inexistente", nuevaPCB->pid, identificador_semaforo);
	}

	return;
}
void twt_signal (t_nombre_semaforo identificador_semaforo)
{
	uint32_t codigo = SIGNAL;
	int32_t respuesta;
	uint32_t messageLength = strlen((char *) identificador_semaforo) + 1;
	void* buffer = malloc((sizeof(uint32_t)*2)+messageLength);

	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &messageLength, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, (char *) identificador_semaforo, messageLength);

	send(fd_kernel,buffer,sizeof(int)*2+messageLength,0);

	recv(fd_kernel, &respuesta, sizeof(int32_t), 0);

	if(respuesta>0)
	{
		log_info(messagesLog,"Proceso: %d hace signal en el semaforo: %s\n", nuevaPCB->pid, identificador_semaforo);
	}
	else
	{
		procesoAbortado=true;
		log_error(messagesLog, "El proceso: %d fue abortado, semaforo %s inexistente", nuevaPCB->pid, identificador_semaforo);
	}

	free(buffer);
	return;
}
t_puntero twt_reservar (t_valor_variable espacio)
{
	log_info(messagesLog,"Proceso: %d solicita reservar %d bytes en heap\n", nuevaPCB->pid, espacio);
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
		codigo_error = puntero;
		log_error(messagesLog,"Proceso: %d abortado, no se pudo reservar el espacio en heap\n", nuevaPCB->pid, espacio);
	}

	return puntero;
}
void twt_liberar(t_puntero puntero)
{
	log_info(messagesLog,"Proceso: %d libera memoria\n", nuevaPCB->pid);
	uint32_t codigo = LIBERAR_MEMORIA;
	int32_t resp;
	void *buffer = malloc(sizeof(uint32_t)*2 + sizeof(u_int32_t));
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer + sizeof(uint32_t)*2, &puntero, sizeof(u_int32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)+ sizeof(uint32_t)*2);
	recibir(fd_kernel, &resp, sizeof(uint32_t));

	if(resp < 0){
		procesoAbortado=true;
		log_error(messagesLog,"Proceso: %d abortado al intentar liberar\n", nuevaPCB->pid);
	}

	return;
}
t_descriptor_archivo twt_abrir (t_direccion_archivo direccion, t_banderas flags)
{
	log_info(messagesLog,"Abrir archivo: %s\n", direccion);
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
	int32_t fd_archivo;

	enviar(fd_kernel, buffer, sizeof(u_int32_t)*3+path_length+sizeof(bool)*3);

	recibir(fd_kernel, &fd_archivo, sizeof(int32_t));

	if(fd_archivo < 0){
		procesoAbortado=true;
		log_error(messagesLog,"Proceso: %d abortado al intentar abrir el archivo: %s\n", nuevaPCB->pid, direccion);
	}

	return fd_archivo;
}
void twt_borrar (t_descriptor_archivo direccion)
{
	log_info(messagesLog,"Borrar archivo: %d\n", direccion);
	uint32_t fd_a_borrar = direccion;
	uint32_t codigo = BORRAR_ARCHIVO;
	int32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*3);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &fd_a_borrar, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)*3);
	recibir(fd_kernel, &resp, sizeof(int32_t));

	if(resp < 0){
		procesoAbortado=true;
		log_error(messagesLog,"Proceso: %d abortado al intentar borrar el archivo: %s\n", nuevaPCB->pid, direccion);
	}

	free(buffer);
	return;
}
void twt_cerrar (t_descriptor_archivo descriptor_archivo)
{
	log_info(messagesLog,"Cerrar archivo, file descriptor: %d\n", descriptor_archivo);
	uint32_t codigo = CERRAR_ARCHIVO;
	uint32_t fd_a_cerrar = descriptor_archivo;
	int32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*3);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &fd_a_cerrar, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)*3);

	recibir(fd_kernel, &resp, sizeof(int32_t));

	if(resp < 0){
		procesoAbortado=true;
		log_error(messagesLog,"Proceso: %d abortado al intentar cerrar el archivo, file descriptor: %d\n", nuevaPCB->pid, descriptor_archivo);
	}
	free(buffer);
	return;
}
void twt_moverCursor (t_descriptor_archivo descriptor_archivo, t_valor_variable posicion)
{
	log_info(messagesLog,"Mover el cursor en el archivo con: %d a posicion: %d\n", descriptor_archivo, posicion);
	uint32_t codigo = MOVER_CURSOR;
	int32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*4);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &descriptor_archivo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*3, &posicion, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(u_int32_t)*4);

	recibir(fd_kernel, &resp, sizeof(int32_t));

	if(resp < 0){
		procesoAbortado=true;
		log_error(messagesLog,"Proceso: %d abortado al intentar mover el cursor en el archivo, file descriptor: %d\n", nuevaPCB->pid, descriptor_archivo);
	}
	free(buffer);
	return;
}
void twt_escribir (t_descriptor_archivo descriptor_archivo, void* informacion, t_valor_variable tamanio)
{
	if(excepcionMemoria == false){
		//memset(informacion+tamanio, '\0', 1);
		log_info(messagesLog,"Escribir '%s'en el archivo, file decriptor: %d\n", (char*) informacion, descriptor_archivo);
		int desc_salida=descriptor_archivo;
		int32_t resp;
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

		recibir(fd_kernel, &resp, sizeof(int32_t));

		if(resp < 0){
			procesoAbortado=true;
			log_error(messagesLog,"Proceso: %d abortado al intentar escribir en el archivo, file descriptor: %d\n", nuevaPCB->pid, descriptor_archivo);
		}
		free(buffer);
	}

	return;
}
void twt_leer (t_descriptor_archivo descriptor_archivo, t_puntero informacion, t_valor_variable tamanio)
{
	log_info(messagesLog,"Leer archivo, file decriptor: %d\n", descriptor_archivo);
	uint32_t codigo = LEER_ARCHIVO;
	int32_t resp;
	void* buffer = malloc(sizeof(uint32_t)*4);
	memcpy(buffer, &codigo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t), &(nuevaPCB->pid), sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*2, &descriptor_archivo, sizeof(uint32_t));
	memcpy(buffer+sizeof(uint32_t)*3, &tamanio, sizeof(uint32_t));
	enviar(fd_kernel, buffer, sizeof(uint32_t)*4);
	char* informacion_leida = malloc(tamanio);

	recibir(fd_kernel, &resp, sizeof(int32_t));

	if(resp < 0){
		procesoAbortado=true;
		log_error(messagesLog,"Proceso: %d abortado al intentar leer en el archivo, file descriptor: %d\n", nuevaPCB->pid, descriptor_archivo);
	} else{
		recibir(fd_kernel, informacion_leida, tamanio);
		int pagina, offset;
		int cod = ASIGNAR_VALOR; //codigo 4 es solicitud escritura a memoria

		pagina = informacion / tamanioPagina;
		offset = informacion % tamanioPagina; //el resto de la division

		//Le envio a Memoria:
		void* buffer = malloc(sizeof(int)*4+sizeof(u_int32_t)+tamanio);

		memcpy(buffer, &cod, sizeof(u_int32_t));
		memcpy(buffer+sizeof(u_int32_t), &nuevaPCB->pid, sizeof(int));
		memcpy(buffer+sizeof(u_int32_t)+sizeof(int), &pagina, sizeof(int));
		memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*2, &offset, sizeof(int));
		memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*3, &tamanio, sizeof(int));
		memcpy(buffer+sizeof(u_int32_t)+sizeof(int)*4, informacion_leida,tamanio);

		send(fd_memoria, buffer, sizeof(int)*5+sizeof(u_int32_t),0);

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
				log_error(messagesLog, "client: socket\n");
				continue;
				}
				if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
					close(sockfd);
					perror("client: connect");
					log_error(messagesLog, "client: connect\n");
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
	int codigo = PEDIR_INSTRUCCION, error;
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

	recv(fd_memoria,&error,sizeof(int),0);
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

	//Creacion del archivo de log

	messagesLog = log_create("../../Files/Logs/CPUMessages.log", "CPU", false, LOG_LEVEL_INFO);

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
		log_info(messagesLog, "Esperando proceso para ejecutar...\n");
		printf("Esperando proceso para ejecutar...\n");
		recv(fd_kernel, &quantum, sizeof(int32_t), 0);
		recv(fd_kernel, &quantum_sleep, sizeof(uint32_t), 0);
		recv(fd_kernel, &codigo, sizeof(uint32_t),0);
		printf("Se recibio codigo: %d\n", codigo);
		log_info(messagesLog, "Se recibio codigo: %d\n", codigo);
		nuevaPCB = recibirPCBV2(fd_kernel);
		printf("RECIBI PCB\n");
		log_info(messagesLog, "Se recibio un PCB\n");

		print_PCB(nuevaPCB);
		excepcionMemoria = false;
		programaTerminado = false;
		procesoBloqueado = false;
		procesoAbortado = false;
		codigo_error = 0;

		if(quantum < 0){
			printf("Estoy ejecutando en FIFO\n");
			while((nuevaPCB->program_counter) < (nuevaPCB->cantidad_de_instrucciones) && (programaTerminado == false) && (excepcionMemoria==false) && (procesoBloqueado == false) && (procesoAbortado == false))
			{
				char *instruccion = obtener_instruccion(nuevaPCB);
				printf("Instruccion: %s\n", instruccion);
				log_info(messagesLog, "Instruccion: %s\n", instruccion);
				analizadorLinea(instruccion, &funciones, &fcs_kernel);
				free(instruccion);
			}
			if(programaTerminado == true){
				codigo = PROCESO_FINALIZO_CORRECTAMENTE;
				send_PCBV2(fd_kernel, nuevaPCB, codigo);
				log_info(messagesLog, "Proceso %d terminado correctamente\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			}else if(procesoBloqueado == true){
				codigo = PROCESO_FINALIZO_CORRECTAMENTE;
				send_PCBV2(fd_kernel, nuevaPCB, codigo);
				printf("Se bloqueo el proceso\n");
				liberar_PCB(nuevaPCB);
			}else if(excepcionMemoria == true){
				codigo = PROCESO_FINALIZO_ERRONEAMENTE;
				enviar(fd_kernel, &codigo, sizeof(uint32_t));
				int32_t error_cod = -5;
				send_PCBV2(fd_kernel, nuevaPCB, error_cod);
				log_error(messagesLog, "Terminacion fallida del proceso: %d\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			}
			else{
				log_error(messagesLog, "Terminacion fallida del proceso: %d\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			}
		}
		else{
			printf("Estoy ejecutando en Round Robin\n");
			while((quantum > 0)/* && (nuevaPCB->program_counter) < (nuevaPCB->cantidad_de_instrucciones)*/ && (programaTerminado == false) && (excepcionMemoria==false) && (procesoBloqueado == false) && (procesoAbortado == false))
			{
				char *instruccion = obtener_instruccion(nuevaPCB);
				printf("Instruccion: %s\n", instruccion);
				log_info(messagesLog, "Instruccion: %s\n", instruccion);
				analizadorLinea(instruccion, &funciones, &fcs_kernel);
				free(instruccion);
				quantum --;
			}
			if((programaTerminado == true))
			{
				codigo = PROCESO_FINALIZO_CORRECTAMENTE;
				send_PCBV2(fd_kernel, nuevaPCB, codigo);
				log_info(messagesLog, "Proceso %d terminado correctamente\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			} else if(procesoBloqueado == true)
			{
				codigo = PROCESO_FINALIZO_CORRECTAMENTE;
				send_PCBV2(fd_kernel, nuevaPCB, codigo);
				printf("Se bloqueo el proceso\n");
				liberar_PCB(nuevaPCB);

			} else if((quantum == 0) && procesoAbortado == false && excepcionMemoria == false){
				codigo = FIN_DE_QUANTUM;
				usleep(quantum_sleep*1000);
				send_PCBV2(fd_kernel, nuevaPCB, codigo);
				log_info(messagesLog, "Fin de quantum del proceso: %d\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			} else if(excepcionMemoria == true){
				codigo = PROCESO_FINALIZO_ERRONEAMENTE;
				enviar(fd_kernel, &codigo, sizeof(uint32_t));
				int32_t error_cod = -5;
				send_PCBV2(fd_kernel, nuevaPCB, error_cod);
				log_error(messagesLog, "Terminacion fallida del proceso: %d\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			}
			else{
				log_error(messagesLog, "Terminacion fallida del proceso: %d\n", nuevaPCB->pid);
				liberar_PCB(nuevaPCB);
			}
		}
	}

	log_destroy(messagesLog);
	close(fd_kernel);
	close(fd_memoria);
	free(cfgPath);

	return 0;
}
