#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../config_shortcuts/config_shortcuts.c"

mem_config data_config;

void *memoria;

typedef struct{
	int page_counter;
	int direccion;
} espacio_reservado;

typedef struct {
	int32_t frame;
	int32_t PID;
	int32_t pagina;
} entrada_tabla;

/*Funciones*/

//Backlog es la cantidad maxima que quiero de conexiones pendientes

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

	void poner_a_escuchar(int sockfd, struct sockaddr* server, int backlog)
	{
		if((bind(sockfd, server, sizeof(struct sockaddr)))==-1)
			{
				perror("bind");
				exit(1);
			}
		if(listen(sockfd,backlog)==-1)
			{
				perror("listen");
				exit(1);
			}
		return;
	}

	struct sockaddr_in crear_estructura_server (int puerto)
	{
		struct sockaddr_in server;
		server.sin_family=AF_INET;
		server.sin_port=htons(puerto);
		server.sin_addr.s_addr=INADDR_ANY;
		memset(&(server.sin_zero),'\0',8);
		return server;
	}

	int aceptar_conexion(int sockfd, struct sockaddr* clien)
	{
		int socknuevo;
		socklen_t clie_len=sizeof(struct sockaddr_in);
		if((socknuevo=accept(sockfd, clien, &clie_len))==-1)
		{
			perror("accept");
		}
		return socknuevo;
	}

	void cargar_config(t_config *config){
		data_config.puerto = config_get_string_value(config, "PUERTO");
		data_config.marcos = config_get_int_value(config, "MARCOS");
		data_config.marco_size = config_get_int_value(config, "MARCO_SIZE");
		data_config.entradas_cache = config_get_int_value(config, "ENTRADAS_CACHE");
		data_config.cache_x_proceso = config_get_int_value(config, "CACHE_X_PROC");
		data_config.reemplazo_cache = config_get_string_value(config, "REEMPLAZO_CACHE");
		data_config.retardo_memoria = config_get_int_value(config, "RETARDO_MEMORIA");
	}

	void print_config(){
		printf("PORT = %s\n", data_config.puerto);
		printf("MARCOS = %d\n", data_config.marcos);
		printf("MARCO_SIZE = %d\n", data_config.marco_size);
		printf("ENTRADAS_CACHE = %d\n", data_config.entradas_cache);
		printf("CACHE_X_PROCESO = %d\n", data_config.cache_x_proceso);
		printf("REEMPLAZO_CACHE = %s\n", data_config.reemplazo_cache);
		printf("RETARDO_MEMORIA = %d\n", data_config.retardo_memoria);
	}

void inicializar_tabla(entrada_tabla *tabla){
	int i, espacio_total_admin, paginas_de_admin;
	div_t paginas_para_tabla;

	espacio_total_admin = data_config.marcos * sizeof(entrada_tabla);

	for(i=0; i < data_config.marcos; i++){
		tabla[i].frame = i;
		tabla[i].PID = -2;
		tabla[i].pagina = -2;
	}

	paginas_para_tabla = div(espacio_total_admin, data_config.marco_size);

	paginas_de_admin = paginas_para_tabla.quot;

	if(paginas_para_tabla.rem > 0){
		paginas_de_admin = paginas_de_admin + 1;
	}

	for(i=0; i < paginas_de_admin; i++){
		tabla[i].PID = -1;
		tabla[i].pagina = i;
	}

}

void dump_de_tabla(){
	int i;
	entrada_tabla *tabla = (entrada_tabla*) memoria;

	for(i=0; i < data_config.marcos; i++){
		printf("Frame %d: PID = %d, Pagina = %d\n", tabla[i].frame, tabla[i].PID, tabla[i].pagina);
	}
}

int espacio_encontrado(int paginas_necesarias, int posicion, entrada_tabla *tabla){
	int encontrado = 1;
	int ultimo_marco = data_config.marcos -1;
	int i;

	for(i=0; i< paginas_necesarias; i++){
		if((posicion + i) > ultimo_marco){
			encontrado = 0;
		}
		if(tabla[posicion + i].PID != -2){
			encontrado = 0;
		}
	}

	return encontrado;
}

espacio_reservado *buscar_espacio(u_int32_t PID, int size, void *script){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	espacio_reservado *espacio = malloc(sizeof(espacio_reservado));
	int encontrado = 0, i = 0, j=0;
	div_t paginas_necesarias;
	int marcos_size = data_config.marco_size;
	int paginas_usadas;

	paginas_necesarias = div(size, marcos_size);
	paginas_usadas = paginas_necesarias.quot;
	if(paginas_necesarias.rem > 0){
		paginas_usadas = paginas_usadas + 1;
	}

	while(encontrado == 0 && i < data_config.marcos){
		encontrado = espacio_encontrado(paginas_usadas, i, tabla);
		if(encontrado == 0){
			i++;
		}
	}

	if(encontrado == 0){
		printf("No hay espacio suficiente para el pedido\n");
		espacio->direccion = -1;
		espacio->page_counter = -1;
		return espacio;
	}else{
		printf("Se ha guardado el script correctamente\n");
	}

	//copiamos el script (POR FIIIIN!)
	memcpy(memoria + marcos_size*i, script, size);

	//Actualizamos las tabla ;)
	for(j=0; j<paginas_usadas; j++){
		tabla[i+j].PID = PID;
		tabla[i+j].pagina = j;
	}

	espacio->page_counter = paginas_usadas;
	espacio->direccion = marcos_size*i;

	return espacio;
}

char *buscar_codigo(u_int32_t PID, int page_counter){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	int i=0, j, primer_frame, encontrado = 0;
	int tamanio_pagina = data_config.marco_size;

	while(encontrado == 0 && i < data_config.marcos){
		if(tabla[i].PID == PID){
			primer_frame = tabla[i].frame;
			encontrado = 1;
		}else{
			i++;
		}
	}

	void *codigo = malloc(tamanio_pagina * page_counter);
	memset(codigo, 0, data_config.marco_size * page_counter);
	for(j=0; j< page_counter; j++){
		memcpy(codigo + (j*tamanio_pagina), memoria+(tamanio_pagina*primer_frame), tamanio_pagina);
		primer_frame += 1;
	}

	char *script = (char *) codigo;
	return script;
}

void *thread_consola(){
	printf("Ingrese un comando \nComandos disponibles:\n dump - Muestra tabla de paginas\n Y eso son todos los comandos que hay, por ahora...\n");
	while(1){
		char *command = malloc(20);
		scanf("%s", command);
		if((strcmp(command, "dump")) == 0){
			dump_de_tabla();
		}else{
			printf("Comando incorrecto\n");
		}
		free(command);
	}
}

void *thread_proceso(int fd){
	printf("Nueva conexion en socket %d\n", fd);
	int bytes, codigo, messageLength, page_counter;
	u_int32_t PID;

	while(1){
		bytes = recv(fd,&codigo,sizeof(int),0);
		verificar_conexion_socket(fd, bytes);

		if(codigo == 2){
			espacio_reservado *espacio;
			bytes = recv(fd, &PID, sizeof(u_int32_t), 0);
			verificar_conexion_socket(fd, bytes);
			recv(fd, &messageLength, sizeof(int), 0);
			void* aux = malloc(messageLength+2);
			memset(aux,0,messageLength+2);
			recv(fd, aux, messageLength, 0);
			memset(aux+messageLength+1,'\0',1);

			//char* charaux = (char*) aux;
			//printf("Y este es el script:\n %s\n", charaux);

			//Ahora buscamos espacio
			espacio = buscar_espacio(PID, messageLength+2, aux);

			send(fd, &espacio->page_counter, sizeof(int), 0);
			send(fd, &espacio->direccion, sizeof(int), 0);

			free(espacio);
			free(aux);
		}
		if(codigo == 3){
			bytes = recv(fd, &PID, sizeof(u_int32_t), 0);
			verificar_conexion_socket(fd, bytes);
			recv(fd, &page_counter, sizeof(int), 0);
			char *script = buscar_codigo(PID, page_counter);
			int tamanio = strlen(script);
			void *buffer = malloc(sizeof(int) + tamanio);
			memcpy(buffer, &tamanio, sizeof(int));
			memcpy(buffer+sizeof(int), script, tamanio);
			send(fd, buffer, sizeof(int) + tamanio, 0);
			free(script);
			free(buffer);
		}
	}
}

int main(int argc, char** argv){

	int espacio_total_tabla;
	t_config *config;

	checkArguments(argc);
	char *cfgPath = malloc(sizeof("../../Memoria/") + strlen(argv[1])+1);
	*cfgPath = '\0';
	strcpy(cfgPath, "../../Memoria/");

	config = config_create_from_relative_with_check(argv, cfgPath);
	cargar_config(config);
	print_config();

	espacio_total_tabla = data_config.marcos * sizeof(entrada_tabla);

	int portnum;
	portnum = atoi(data_config.puerto); /*Lo asigno antes de destruir config*/

	/*Creacion de tabla de memoria*/

	entrada_tabla *tabla_de_paginas = malloc(sizeof(entrada_tabla) * data_config.marcos);

	inicializar_tabla(tabla_de_paginas);

	/*Creacion del espacio de memoria*/

	memoria = calloc(data_config.marcos, data_config.marco_size);

	memcpy(memoria, tabla_de_paginas, espacio_total_tabla);

	int valorhiloConsola;
	pthread_t hiloConsola;
	valorhiloConsola = pthread_create(&hiloConsola, NULL,(void *) thread_consola, NULL);
	if(valorhiloConsola != 0){
		printf("Error al crear el hilo programa");
	}

	/*Sockets para recibir mensaje del Kernel*/

	int listener, newfd, bytes;
	struct sockaddr_in server, cliente;

	/*inicializo el buffer*/

	memset(&(cliente.sin_zero),'\0',8);

	/*Creo estructura server*/

	server = crear_estructura_server(portnum);

	/*socket()*/

	if((listener=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		perror("socket");
		exit(1);
	}

	/*bind() y listen()*/

	poner_a_escuchar(listener, (struct sockaddr *) &server, 1);

	/*accept()*/

	newfd = aceptar_conexion(listener, (struct sockaddr*) &cliente);

	/*Handshake*/

	int handshake;
	bytes = recv(newfd,&handshake,sizeof(int),0);
	if(bytes > 0 && handshake == 1){
				printf("%d\n",handshake);
				send(newfd, &handshake, sizeof(int), 0);
	}else{
		if(bytes == -1){
			perror("recieve");
			exit(3);
			}
		if(bytes == 0){
			printf("Se desconecto el socket: %d\n", newfd);
			close(newfd);
		}
	}

	/*creamos el hilo que maneja la conexion con el kernel*/

	int valorHilo;

	valorHilo = -1;

	pthread_t hiloKernel;
	valorHilo = pthread_create(&hiloKernel, NULL, thread_proceso, newfd);
	if(valorHilo != 0){
		printf("Error al crear el hilo programa");
	}

	/*Creamos los hilos que manejan CPUs cada vez que una se conecta*/

	while(1){
		valorHilo = -1;
		newfd = aceptar_conexion(listener, (struct sockaddr*) &cliente);
		pthread_t hiloProceso;
		valorHilo = pthread_create(&hiloProceso, NULL, thread_proceso, newfd);
		if(valorHilo != 0){
			printf("Error al crear el hilo programa");
		}
	}

	/*Mostramos el mensaje recibido*/
/*
	buf[bytes_leidos]='\0';
	printf("%s",buf);*/

	free(memoria);
	free(cfgPath);
	config_destroy(config);
	return 0;
}
