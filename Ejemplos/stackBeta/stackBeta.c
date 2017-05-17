/*
 * stack-y-pcb.c
 *
 *  Created on: 30/4/2017
 *      Author: utnso
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <parser/parser.h>

/*Esto es solo una version beta de como se encara el stack, particularmente con la primitiva definir variable */

//Estas estructuras son lo mas chicas posibles(no van a ser asi) porque todavia se esta probando esto
//PD: Volo no quiero cambiarte el pcb, solo creo que es mas comodo usar lista en algunos campos :)

int cantPaginasCodigo = 3;
bool stackOverflow = false;
int tamanioPagina;
int tamanioStack=3;
int fd_memoria;

typedef struct{
	t_list* stack_index;
	int primerPaginaStack;
	int stackPointer;
}pcb;


typedef struct {
	char nombre;
	int pagina;
	int offset;
	int size;
} variable;

//El indice de stack es una lista de registros de stack. Cada funcion tiene su registro de stack
//O sea si hay llamada a funcion, se agrega un registroStack al stack_index

typedef struct {
	t_list* vars;
} registroStack;

pcb* pcbActual;
pcb mipcb;

typedef struct {
	int pagina;
	int offset;
	int longitud;
} pedidoEscritura;

void obtenerTamanioPaginaDeMemoria()
{
	int* tamPagina = malloc(sizeof(int));
	int bytes;
	bytes=recv(fd_memoria, tamPagina, sizeof(int),0);
	if(bytes<=0)
	{
		if(bytes==-1)
		{
			perror("receive tamPagina");
			exit(1);
		}
		else
		{
			printf("Se desconecto memoria\n");
			close(fd_memoria);
			exit(1);
		}
	}
	tamanioPagina = (*tamPagina);
	free(tamPagina);
	return;
}
//Solo inicializando las globales asi me funca, no se porque pero bue

void inicializar() {

	mipcb.primerPaginaStack=cantPaginasCodigo;
	mipcb.stackPointer=0;	   //arranca al principio del stack
	mipcb.stack_index=list_create();
	pcbActual=&mipcb;
	return;
}



t_puntero beta_definirVariable (t_nombre_variable identificador_variable)
{

	int var_pagina = pcbActual->primerPaginaStack; //pagina del stack donde guardo la variable
	int var_offset = pcbActual->stackPointer;	   //offset dentro de esa pagina


	//Si offset es mayor que pagina, voy aumentando pagina
	while(var_offset > tamanioPagina){
	var_pagina++;
	var_offset = var_offset - tamanioPagina;
	}
	//Si al guardarla (4 bytes al ser int) desbordo el stack, stack overflow:

	if((pcbActual->stackPointer) + 4 > (tamanioStack * tamanioPagina))
	{
		printf("Stack Overflow al definir variable %c\n", identificador_variable);
		stackOverflow=true;
		return -1;
	}

	//Obtengo el ultimo registro de stack
	registroStack* regStack = list_get(pcbActual->stack_index, pcbActual->stack_index->elements_count -1);

	if(regStack == NULL)
	{ 	// si no hay registros, creo uno nuevo
		regStack = malloc(sizeof(registroStack));
		regStack->vars = list_create();
		// Guardo el nuevo registro en el índice:
		list_add(pcbActual->stack_index, regStack);
	}

	variable* new_var = malloc(sizeof(variable));
	new_var->nombre = identificador_variable;
	new_var->pagina = var_pagina;
	new_var->offset = var_offset;
	new_var->size = sizeof(int);

	list_add(regStack->vars, new_var);

	printf("Agregue la variable: %c en: (pagina, offset, size) = (%i, %i, %i)\n",identificador_variable,var_pagina,var_offset,4);

	//Actualizo stackPointer
	pcbActual->stackPointer = (pcbActual->stackPointer) + 4;
	int posicionVariable = (cantPaginasCodigo * tamanioPagina) + (pcbActual->stackPointer);
	return posicionVariable;
}

AnSISOP_funciones funciones =
{

		.AnSISOP_definirVariable=beta_definirVariable,

};


int main()
{

	inicializar();
	analizadorLinea("variables a, b,c,d,      e", &funciones,NULL);
	if(stackOverflow==true)
	{
	printf("Hubo StackOverflow\n");
	return -1;
	}
	return 0;
}












