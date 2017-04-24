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


/*Funciones para Implementar el PARSER (mas adelante emprolijamos y lo metemos en otro archivo)*/

/*Aca defino las primitivas que aparecen en la estructura AnSISOP_funciones adentro del
 * archivo parser.h. Lo unico que hace cada una es decir "soy tal primitiva" asi sabemos
 * a que primitivas se llamaron cuando le mandamos una instruccion a analizadorLinea()
 * (el twt es por the walking thread :))
 */

t_puntero twt_definirVariable (t_nombre_variable identificador_variable)
{
	printf("Definir Variable: %c\n",identificador_variable);
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
	printf("Soy wait\n");
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

void handshake(int codigo, int idProceso, int fd){
	void* codbuf = malloc(sizeof(int)*2);
	codigo = 1;
	memcpy(codbuf,&codigo,sizeof(int));
	memcpy(codbuf + sizeof(int),&idProceso, sizeof(int));
	send(fd, codbuf, sizeof(int)*2, 0);
	free(codbuf);
}

int main(int argc, char **argv) {

	//analizadorLinea la pongo solo para probar si llama a las primitivas
	analizadorLinea("variables a, b", &funciones, &fcs_kernel);

	// SETTEO DESDE ARCHIVO DE CONFIGURACION

	//Variables para config
	t_config *config_file;
	cpu_config data_config;

	config_file = config_create_from_relative_with_check(argc,argv);

	data_config.ip_kernel = config_get_string_value(config_file, "IP_KERNEL");
	data_config.puerto_kernel = config_get_string_value(config_file, "PUERTO_KERNEL");

	printf("IP Kernel: %s\n",data_config.ip_kernel);
	printf("Puerto Kernel: %s\n\n",data_config.puerto_kernel);

	// CONEXION A KERNEL

	char buf[256];
	int statusgetaddrinfo, fd, bytes, codigo;
	struct addrinfo hints, *sockinfo, *aux;
	int idProceso = 1;

	//Me aseguro que hints este vacio, lo necesito limpito o el getaddrinfo se puede poner chinchudo
	memset(&hints,0,sizeof(hints));

	//Setteo (como dice el TP) un addrinfo con los datos del socket que quiero
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	//Esta funcion settea sockinfo con una lista de addrinfos que voy a usar despues.
	//statusgetaddrinfo solo me sirve para saber si se ejecuto la funcion correctamente.
	statusgetaddrinfo = getaddrinfo(data_config.ip_kernel,data_config.puerto_kernel,&hints,&sockinfo);
	if(statusgetaddrinfo != 0)
	{
		//Si me da distinto de 0 => Hubo un error y lo printeo, gai_strerror me indica el tipo de error.
		printf("getaddrinfo: %s\n",gai_strerror(statusgetaddrinfo));
		exit(1);
	}

	//Como sockinfo tiene varios addrinfo tengo que sacar el que me deje conectarme.
	for(aux = sockinfo; aux != NULL; aux = aux->ai_next)
	{
		//Paso la info de uno de los sockaddr para recibir el file descriptor (FD) de un socket.
		fd = socket(aux->ai_family, aux->ai_socktype, aux->ai_protocol);
		if(fd==-1)
		{
			perror("socket");
	        continue;
	    }

		//Si el FD esta bien, trato de conectarme.
		//Si esto falla no necesariamente esta mal, itero la funcion porque tal vez otro de los
		//sockaddr si me conecta.
	    if (connect(fd, aux->ai_addr, aux->ai_addrlen) == -1)
	    {
	        close(fd);
	        perror("connect");
	        continue;
	    }

	    break;
	}

	//Si aux es NULL entonces no me pude conectar con ninguno de los sockaddr y aborto el programa.
	if(aux == NULL)
	{
		printf("connection status: failed\n");
		exit(2);
	} else fprintf(stderr,"connection status: success\n");

	//Sockinfo debe irse, su planeta lo necesita
	free(sockinfo);

	/*codigo = 1;
	if(send(fd,&codigo,sizeof(int),0)==-1)
		{
			perror("send");
			exit(3);
		}*/

	handshake(codigo,idProceso,fd);

	//Y aqui termina la CPU, esperando e imprimiendo mensajes hasta el fin de los tiempos
	//O hasta que cierres el programa
	//Lo que pase primero
	while(1)
	{
		message_handler_for_fd(fd);
	}

	close(fd);
	//analizadorLinea la pongo solo para probar si llama a las primitivas
	analizadorLinea("variables a, b", &funciones, &fcs_kernel);
	return 0;
}
