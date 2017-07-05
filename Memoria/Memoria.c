#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../config_shortcuts/config_shortcuts.h"
#include "../shared_libs/PCB.h"

mem_config data_config;
pthread_mutex_t mutex_memoria;
void *memoria;
t_list** hash_index;

enum{
	HANDSHAKE = 1,
	NUEVO_PROCESO = 2,
	BUSCAR_INSTRUCCION = 3,
	GUARDAR_VALOR = 4,
	ELIMINAR_PROCESO = 5,
	OBTENER_VALOR = 6,
	SOLICITAR_HEAP = 7,
	ALOCAR = 8,
	LIBERAR = 9
};

typedef struct{
	int page_counter;
	int direccion;
} espacio_reservado;

typedef struct {
	int32_t frame;
	int32_t PID;
	int32_t pagina;
} entrada_tabla;

typedef struct {
	uint32_t size;
	_Bool isFree;
} __attribute__((packed)) heapMetadata;

typedef struct{
	uint32_t pid;
	uint32_t pagina;
	void *contenido;
} entrada_cache;

typedef struct{
	uint32_t t_last_ref;
	uint32_t pid;
} entrada_admin_cache;

void *contenido_cache;
entrada_cache *cache;
entrada_admin_cache *admin_cache;

/*Funciones*/

//Backlog es la cantidad maxima que quiero de conexiones pendientes

int verificar_conexiones_socket(int fd, int estado){
	if(estado <= 0){
		if(estado == -1){
			perror("recieve");
			}
		if(estado == 0){
			printf("Se desconecto el socket: %d\n", fd);
		}
		close(fd);
		return 0;
	}
	return 1;
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
		printf("Frame %d:	PID = %d	Pagina = %d	\n", tabla[i].frame, tabla[i].PID, tabla[i].pagina);
	}
}

void memory_size(){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	printf("La memoria tiene %d frames\n", data_config.marcos);
	int i=0, frames_ocupados = 0, frames_libres = 0;
	while(i < data_config.marcos){
		if(tabla[i].PID == -2 && tabla[i].pagina == -2){
			frames_libres++;
			i++;
		}else{
			frames_ocupados++;
			i++;
		}
	}
	printf("Hay %d frames libres y %d ocupados\n", frames_libres, frames_ocupados);
}

void process_size(int pid){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	int i=0, paginas_de_proceso=0;
	while(i < data_config.marcos){
		if(tabla[i].PID == pid){
			paginas_de_proceso++;
		}
		i++;
	}
	printf("El proceso %d tiene %d paginas\n", pid, paginas_de_proceso);
}

void dump_de_memoria(){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	int i;
	for(i=0; i<data_config.marcos; i++){
		if(tabla[i].PID != -2){
			void *contenido_frame = malloc(data_config.marco_size);
			memcpy(contenido_frame, memoria+(i*data_config.marco_size), data_config.marco_size);
			printf("Frame %d:\n%s\n", i, (char *) contenido_frame);
			free(contenido_frame);
		}
		else{
			printf("Frame %d:\n\n", i);
		}
	}
}

void dump_de_proceso(int pid){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	int i;
	for(i=0; i<data_config.marcos; i++){
		if(tabla[i].PID == pid){
			void *contenido_frame = malloc(data_config.marco_size);
			memcpy(contenido_frame, memoria+(i*data_config.marco_size), data_config.marco_size);
			printf("Frame %d, pagina: %d\n%s\n", i, tabla[i].pagina,(char *) contenido_frame);
			free(contenido_frame);
		}
	}
}

//Funciones de indice de Hash

unsigned int calcular_posicion(int pid, int num_pagina) {
	char str1[20];
	char str2[20];
	sprintf(str1, "%d", pid);
	sprintf(str2, "%d", num_pagina);
	strcat(str1, str2);
	unsigned int indice = atoi(str1) % data_config.marcos;
	return indice;
}

int es_pagina_correcta(int pos_candidata, int pid, int pagina) {
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	if(tabla[pos_candidata].PID == pid && tabla[pos_candidata].pagina == pagina){
		return 1;
	}else{
		return 0;
	}
}

void inicializar_indice() {
	hash_index = malloc(sizeof(t_list*) * data_config.marcos);
	int i;
	for (i = 0; i < data_config.marcos; ++i){
		hash_index[i] = list_create();
	}
}

int buscar_en_indice(int indice, int pid, int pagina) {
	int i = 0, frame_buscado, encontrado = 0;
	while(encontrado == 0 && i < list_size(hash_index[indice])){
		if (es_pagina_correcta((int)list_get(hash_index[indice], i), pid, pagina)== 1) {
			frame_buscado = (int) list_get(hash_index[indice], i);
			encontrado = 1;
		}else{
			i++;
		}
	}
	if(encontrado == 1){
		return frame_buscado;
	}else{
		return -1;
	}
}

void agregar_frame_en_indice(int pos_inicial, int nro_frame) {
	list_add(hash_index[pos_inicial], nro_frame);
}

void borrar_de_indice(int posicion, int frame) {
	int i = 0;
	int index_frame;

	for (i = 0; i < list_size(hash_index[posicion]); i++) {
		if (frame == (int) list_get(hash_index[posicion], i)) {
			index_frame = i;
			i = list_size(hash_index[posicion]);
		}
	}

	list_remove(hash_index[posicion], index_frame);
}

//Fin de funciones de hash

//Funciones de Cache

//Flush: vacia la cache
void flush(){
	int i;
	for(i=0; i<data_config.entradas_cache; i++){
		cache[i].pid = -1;
		cache[i].pagina = -1;
		memset(cache[i].contenido, '\0',data_config.marco_size);
		admin_cache[i].pid = -1;
		admin_cache[i].t_last_ref = -1;
	}

}

//Elimina las entradas especificas al proceso
void flush_process(uint32_t pid){
	int i;
	for(i=0; i<data_config.entradas_cache; i++){
		if(admin_cache[i].pid == pid){
			cache[i].pid = -1;
			cache[i].pagina = -1;
			memset(cache[i].contenido, '\0',data_config.marco_size);
			admin_cache[i].pid = -1;
			admin_cache[i].t_last_ref = -1;
		}
	}
}

void print_cache(){
	int i;
	for(i=0; i<data_config.entradas_cache; i++){
		printf("Entrada %d:		PID: %d		Pagina: %d		Contenido:\n%s\n", i, cache[i].pid, cache[i].pagina, (char *) cache[i].contenido);
	}
}

void inicializar_cache(){
	int i;
	for(i=0; i<data_config.entradas_cache; i++)
	{
		cache[i].pagina = -1;
		cache[i].pid = -1;
		cache[i].contenido = (contenido_cache+i*data_config.marco_size);

		admin_cache[i].pid = -1;
		admin_cache[i].t_last_ref = -1;
	}
}

_Bool esta_en_cache(uint32_t pid, uint32_t pagina){
	int i;
	_Bool encontrado = false;
	for(i=0; i<data_config.entradas_cache; i++){
		if(cache[i].pagina == pagina && cache[i].pid == pid){
			encontrado = true;
		}
	}
	return encontrado;
}

uint32_t cantidad_de_entradas(uint32_t pid){
	int i, cantidad=0;
	for(i=0; i<data_config.entradas_cache; i++){
		if(cache[i].pid == pid){
			cantidad ++;
		}
	}
	return cantidad;
}

entrada_cache buscar_entrada(uint32_t pid, uint32_t pagina){
	int i;
	entrada_cache entrada_buscada;
	for(i=0; i<data_config.entradas_cache; i++){
		if(cache[i].pagina == pagina && cache[i].pid == pid){
			entrada_buscada = cache[i];
		}
	}
	return entrada_buscada;
}

_Bool hay_espacio_en_cache(){
	int i;
	_Bool hay_espacio = false;
	for(i=0; i<data_config.entradas_cache; i++){
		if(cache[i].pagina == -1 || cache[i].pid ==-1){
			hay_espacio = true;
		}
	}
	return hay_espacio;
}

int entrada_libre(){
	int i=0, entrada_libre;
	_Bool encontrado = false;

	while(encontrado == false && i<data_config.entradas_cache){
		if(cache[i].pid == -1 || cache[i].pagina == -1){
			encontrado = true;
			entrada_libre = i;
		}else{
			i++;
		}
	}
	return entrada_libre;
}

int buscar_victima_global(){
	int i, max_ref = 0, victima;
	for(i=0;i<data_config.entradas_cache; i++){
		if((admin_cache[i].t_last_ref) > max_ref){
			victima = i;
			max_ref = admin_cache[i].t_last_ref;
		}
	}
	return victima;
}

int buscar_victima_local(uint32_t pid){
	int i, max_ref = 0, victima;
	for(i=0;i<data_config.entradas_cache; i++){
		if((admin_cache[i].pid == pid) && (admin_cache[i].t_last_ref) > max_ref){
			victima = i;
			max_ref = admin_cache[i].t_last_ref;
		}
	}
	return victima;
}

void insertar_entrada(uint32_t pid, uint32_t pagina, uint32_t frame){
	int aux;
	int direccion = frame*data_config.marco_size;

	if(data_config.entradas_cache > 0 && data_config.cache_x_proceso > 0){
		printf("Agregamos la pagina a la cache\n");
		if(hay_espacio_en_cache() == true){
			if(cantidad_de_entradas(pid) < data_config.cache_x_proceso){
				printf("Buscamos una entrada libre\n");
				aux = entrada_libre();
			}else{
				printf("Buscamos una victima local\n");
				aux = buscar_victima_local(pid);
			}
		}else{
			if(cantidad_de_entradas(pid) < data_config.cache_x_proceso){
				printf("Buscamos una victima global\n");
				aux = buscar_victima_global();
			}else{
				printf("Buscamos una victima local\n");
				aux = buscar_victima_local(pid);
			}
		}

		cache[aux].pagina = pagina;
		cache[aux].pid = pid;
		//Copiamos el contenido de la pagina en la cache

		memcpy(cache[aux].contenido, (memoria+direccion), data_config.marco_size);

		admin_cache[aux].pid = pid;
		admin_cache[aux].t_last_ref = 0;
	}
}

void aumentar_referencias(){
	int i;
	for(i=0;i<data_config.entradas_cache; i++){
		if(admin_cache[i].t_last_ref != -1){
			admin_cache[i].t_last_ref ++;
		}
	}
}

void set_zero_ref(uint32_t pid, uint32_t pagina){
	int i, posicion;
	for(i=0; i<data_config.entradas_cache; i++){
		if(cache[i].pid == pid && cache[i].pagina == pagina){
			posicion = i;
		}
	}
	admin_cache[posicion].t_last_ref = 0;
}

/*Fin de Funciones de Cache*/

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

void finalizar_proceso(int PID){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	int i;
	int cant_paginas = data_config.marcos;

	for(i= 0; i<cant_paginas; i++){
		if(tabla[i].PID == PID){
			int posicion_heap = calcular_posicion(tabla[i].PID, tabla[i].pagina);
			borrar_de_indice(posicion_heap, tabla[i].frame);

			tabla[i].PID = -2;
			tabla[i].pagina = -2;
		}
	}
	flush_process(PID);
}

int buscar_espacio_stack(u_int32_t PID, int paginas, int paginas_ocupadas){
	int encontrado = 0, i = 0, j=0;
	int numero_de_pagina = paginas_ocupadas;

	entrada_tabla *tabla = (entrada_tabla *) memoria;

	while(encontrado == 0 && i < data_config.marcos){
		encontrado = espacio_encontrado(paginas, i, tabla);
		if(encontrado == 0){
			i++;
		}
	}

	if(encontrado == 0){
		printf("No hay espacio para el stack\n");
		return -1;
	}

	for(j=0; j<paginas; j++){
		tabla[i+j].PID = PID;
		tabla[i+j].pagina = numero_de_pagina+j;
		int posicion_hash = calcular_posicion(PID, numero_de_pagina+j);
		agregar_frame_en_indice(posicion_hash, (i+j));
	}

	return paginas;
}

uint32_t paginas_de_proceso(uint32_t pid){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	int i, cant_marcos = data_config.marcos;
	uint32_t cant_paginas = 0;

	for(i=0; i<cant_marcos; i++){
		if(tabla[i].PID == pid){
			cant_paginas ++;
		}
	}

	return cant_paginas;
}

espacio_reservado *buscar_espacio(u_int32_t PID, int size, void *script, int stack){
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
		//Actualizamos las tabla ;)
		for(j=0; j<paginas_usadas; j++){
			tabla[i+j].PID = PID;
			tabla[i+j].pagina = j;
			int posicion_hash = calcular_posicion(PID, j);
			agregar_frame_en_indice(posicion_hash, (i+j));
		}

		int guardado_de_stack = buscar_espacio_stack(PID, stack, paginas_usadas);
		if(guardado_de_stack == -1){
			finalizar_proceso(PID);
			espacio->direccion = -1;
			espacio->page_counter = -1;
			return espacio;
		}else{
			printf("Se ha guardado el script correctamente\n");
		}
	}

	//copiamos el script (POR FIIIIN!)
	memcpy(memoria + marcos_size*i, script, size);

	espacio->page_counter = paginas_usadas+stack;
	espacio->direccion = marcos_size*i;

	return espacio;
}

_Bool process_last_page(uint32_t pid, uint32_t pagina){
	int ultima_pagina_de_proceso = paginas_de_proceso(pid) - 1;
	return pagina == ultima_pagina_de_proceso;
}

espacio_reservado *buscar_espacio_para_heap(uint32_t pid){
	entrada_tabla *tabla = (entrada_tabla *) memoria;
	espacio_reservado *espacio = malloc(sizeof(espacio_reservado));
	int encontrado = 0, i = 0;
	int marcos_size = data_config.marco_size;

	while(encontrado == 0 && i < data_config.marcos){
		encontrado = espacio_encontrado(1, i, tabla);
		if(encontrado == 0){
			i++;
		}
	}

	uint32_t numero_de_pagina = paginas_de_proceso(pid);

	if(encontrado == 0){
		printf("No hay espacio suficiente para el pedido\n");
		espacio->direccion = -1;
		espacio->page_counter = -1;
		return espacio;
	}else{
		tabla[i].PID = pid;
		tabla[i].pagina = numero_de_pagina;
		int posicion_hash = calcular_posicion(pid, numero_de_pagina);
		agregar_frame_en_indice(posicion_hash, i);
	}

	heapMetadata *unEspacio = malloc(sizeof(heapMetadata));
	unEspacio->isFree = true;
	unEspacio->size = data_config.marco_size - sizeof(heapMetadata);

	memcpy(memoria + marcos_size*i, unEspacio, sizeof(heapMetadata));

	printf("Se ha reservado memoria para el Heap\n");

	espacio->page_counter = paginas_de_proceso(pid);
	espacio->direccion = marcos_size*i;

	return espacio;
}

int32_t buscar_espacio_libre(uint32_t espacio, uint32_t direccion_fisica){
	int encontrado = 0;
	uint32_t aux;
	int corrimiento = 0;
	int32_t direccion_definitiva;
	while(encontrado == 0 && corrimiento < data_config.marco_size){
		heapMetadata *actual = (heapMetadata *) (memoria+direccion_fisica+corrimiento);
		if(actual->isFree == true && actual->size >= (espacio+sizeof(heapMetadata))){
			aux = actual->size - (espacio+sizeof(heapMetadata));
			actual->isFree = false;
			actual->size = espacio;

			//Creamos otro heapMetadata para el espacio libre
			heapMetadata *nuevo = malloc(sizeof(heapMetadata));
			nuevo->isFree = true;
			nuevo->size = aux;

			direccion_definitiva = corrimiento + sizeof(heapMetadata);

			memcpy(memoria + direccion_fisica + corrimiento + sizeof(heapMetadata) + espacio, nuevo, sizeof(heapMetadata));
			encontrado = 1;
		}else{
			corrimiento = corrimiento + sizeof(heapMetadata) + actual->size;
		}
	}
	if(encontrado == 1){
		return direccion_definitiva;
	}else{
		return -1;
	}
}

void *lectura(u_int32_t PID, int pagina, int inicio, int offset){
	int frame, posicion_en_indice;
	int tamanio_pagina = data_config.marco_size;

	void *instruccion = malloc(offset);
	if(esta_en_cache(PID, pagina) == false){
		usleep(data_config.retardo_memoria * 1000);

		posicion_en_indice = calcular_posicion(PID, pagina);
		frame = buscar_en_indice(posicion_en_indice, PID, pagina);
		if(frame < 0){
			printf("Page not Found\n");
			memcpy(instruccion, "", strlen(""));
			return instruccion;
		}


		int direccion_fisica = frame*tamanio_pagina;
		//Copiamos la instruccion
		memcpy(instruccion, memoria+direccion_fisica+inicio, offset);

		//Insertamos la pagina en la cache
		aumentar_referencias();
		insertar_entrada(PID, pagina, frame);

		}else{
			printf("La pagina esta en cache\n");
			entrada_cache unaCache = buscar_entrada(PID, pagina);
			memcpy(instruccion, (unaCache.contenido+inicio), offset);
			aumentar_referencias();
			set_zero_ref(unaCache.pid, unaCache.pagina);
		}
	return instruccion;
}

void escritura(u_int32_t PID, int pagina, int offset, int tamanio, void *value){
	int frame, tamanio_pagina = data_config.marco_size, posicion_en_indice;

	usleep(data_config.retardo_memoria * 1000);

	posicion_en_indice = calcular_posicion(PID, pagina);
	frame = buscar_en_indice(posicion_en_indice, PID, pagina);

	//printf("El frame buscado es el %d\n", frame);

	int direccion_fisica = frame*tamanio_pagina;

	memcpy(memoria+direccion_fisica+offset, value, tamanio);

	if(esta_en_cache(PID, pagina) == true){
		printf("Actualizamos la pagina en la cache\n");
		entrada_cache unaCache = buscar_entrada(PID, pagina);
		memcpy((unaCache.contenido+offset), value, tamanio);
		aumentar_referencias();
		set_zero_ref(unaCache.pid, unaCache.pagina);
	}else{
		aumentar_referencias();
		insertar_entrada(PID, pagina, frame);
	}

}

espacio_reservado *alocar(uint32_t PID, uint32_t pagina, uint32_t espacio){
	int frame, posicion_hash, tamanio_pagina = data_config.marco_size;
	espacio_reservado *unEspacio = malloc(sizeof(espacio_reservado));

	posicion_hash = calcular_posicion(PID, pagina);
	frame = buscar_en_indice(posicion_hash, PID, pagina);

	int direccion_fisica = frame*tamanio_pagina;

	int32_t puntero = 0;
	_Bool no_hay_espacio = false;

	puntero = buscar_espacio_libre(espacio, direccion_fisica);

	while(puntero < 0 && no_hay_espacio == false){
		if(puntero < 0 && process_last_page(PID, pagina) == true){
			printf("Creamos un nuevo Espacio\n");
			espacio_reservado *unEspacio = buscar_espacio_para_heap(PID);
			if(unEspacio->direccion < 0){
				no_hay_espacio = true;
			}else{
				pagina ++;

				posicion_hash = calcular_posicion(PID, pagina);
				frame = buscar_en_indice(posicion_hash, PID, pagina);

				int direccion_fisica = frame*tamanio_pagina;
				puntero = buscar_espacio_libre(espacio, direccion_fisica);
			}
		}else if(puntero < 0 && process_last_page(PID, pagina) == false){
			printf("Buscamos la siguiente pagina\n");
			pagina ++;

			posicion_hash = calcular_posicion(PID, pagina);
			frame = buscar_en_indice(posicion_hash, PID, pagina);

			printf("El frame buscado para alocar heap es el %d\n", frame);

			int direccion_fisica = frame*tamanio_pagina;
			puntero = buscar_espacio_libre(espacio, direccion_fisica);
		}
	}

	if(esta_en_cache(PID, pagina) == true){
		printf("Actualizamos la pagina en la cache\n");
		entrada_cache unaCache = buscar_entrada(PID, pagina);
		memcpy((unaCache.contenido), (memoria + frame*tamanio_pagina), tamanio_pagina);
		aumentar_referencias();
		set_zero_ref(unaCache.pid, unaCache.pagina);
	}else{
		aumentar_referencias();
		insertar_entrada(PID, pagina, frame);
	}

	unEspacio->direccion = puntero;
	unEspacio->page_counter = pagina;
	return unEspacio;
}

void clean_heap(uint32_t direccion){
	uint32_t corrimiento = 0;
	while(corrimiento < data_config.marco_size){
		heapMetadata *unPuntero = (heapMetadata *) (memoria+direccion+corrimiento);
		if(unPuntero->isFree == true && (corrimiento+sizeof(heapMetadata)+(unPuntero->size)) < data_config.marco_size){
			uint32_t aux = (unPuntero->size) + sizeof(heapMetadata);
			heapMetadata *otroPuntero = (heapMetadata *) (memoria+direccion+corrimiento+aux);
			if(otroPuntero->isFree == true){
				uint32_t valor_anterior = (unPuntero->size);
				unPuntero->size = (unPuntero->size) + (otroPuntero->size) + sizeof(heapMetadata);
				printf("El heapMetadata que apuntaba a %d ya no existe y el puntero que apuntaba a %d ahora apunta a %d\n", otroPuntero->size, valor_anterior,unPuntero->size);
			}else{
				corrimiento = corrimiento + sizeof(heapMetadata) + unPuntero->size;
			}
		}else{
			corrimiento = corrimiento + sizeof(heapMetadata) + unPuntero->size;
		}
	}
}

void liberar(uint32_t PID, uint32_t pagina, uint32_t direccion){
	int frame, posicion_hash, tamanio_pagina = data_config.marco_size;

	posicion_hash = calcular_posicion(PID, pagina);
	frame = buscar_en_indice(posicion_hash, PID, pagina);

	printf("El frame buscado para liberar heap es el %d\n", frame);

	int direccion_fisica = (frame*tamanio_pagina + direccion) - sizeof(heapMetadata);

	printf("Y la direccion del puntero es %d\n", direccion_fisica);

	//Marcamos espacio como libre
	heapMetadata *unPuntero = (heapMetadata *) (memoria + direccion_fisica);

	printf("El puntero localizado tiene %d bytes de tamanio y flag en %d\n", unPuntero->size, unPuntero->isFree);

	unPuntero->isFree = true;

	clean_heap(frame*tamanio_pagina);

	if(esta_en_cache(PID, pagina) == true){
		printf("Actualizamos la pagina en la cache\n");
		entrada_cache unaCache = buscar_entrada(PID, pagina);
		memcpy((unaCache.contenido), (memoria + frame*tamanio_pagina), tamanio_pagina);
		aumentar_referencias();
		set_zero_ref(unaCache.pid, unaCache.pagina);
	}else{
		aumentar_referencias();
		insertar_entrada(PID, pagina, frame);
	}
}

void *thread_consola(){
	printf("Ingrese un comando \nComandos disponibles:\n dump tabla		Muestra tabla de paginas\n dump cache		Muestra la cache\n"
			" dump memoria		Muestra la memoria\n dump proceso <PID>	Muestra memoria de un proceso\n"
			" flush			Limpia la cache\n clear			Limpia la consola de mensajes\n size memoria		Total de frames, ocupados y libres\n"
			" size proceso <PID>	Paginas de proceso\n retardo <miliseg>	Modifica el retardo de Memoria\n\n");
	while(1){
		char *command = malloc(20);
		char *subcommand = malloc(20);
		int *numero = malloc(sizeof(int));
		scanf("%s", command);
		if((strcmp(command, "dump")) == 0){
			scanf("%s", subcommand);
			if((strcmp(subcommand, "tabla")) == 0){
				pthread_mutex_lock(&mutex_memoria);
				dump_de_tabla();
				pthread_mutex_unlock(&mutex_memoria);
			} else if(strcmp(subcommand, "cache")== 0){
				pthread_mutex_lock(&mutex_memoria);
				print_cache();
				pthread_mutex_unlock(&mutex_memoria);
			} else if(strcmp(subcommand, "memoria") == 0){
				pthread_mutex_lock(&mutex_memoria);
				dump_de_memoria();
				pthread_mutex_unlock(&mutex_memoria);
			} else if(strcmp(subcommand, "proceso") == 0){
				scanf("%d", numero);
				pthread_mutex_lock(&mutex_memoria);
				dump_de_proceso(*numero);
				pthread_mutex_unlock(&mutex_memoria);
			}
			else{
				printf("No existe tal estructura\n");
			}
		}
		else if(strcmp(command, "size") == 0){
			scanf("%s",subcommand);
			if(strcmp(subcommand, "memoria")==0){
				pthread_mutex_lock(&mutex_memoria);
				memory_size();
				pthread_mutex_unlock(&mutex_memoria);
			}else if(strcmp(subcommand, "proceso")==0){
				scanf("%d", numero);
				pthread_mutex_lock(&mutex_memoria);
				process_size(*numero);
				pthread_mutex_unlock(&mutex_memoria);
			}else{
				printf("No existe tal estructura\n");
			}
		}
		else if(strcmp(command, "clear") == 0)
		{
			system("clear");
		}
		else if(strcmp(command, "flush") == 0){
			pthread_mutex_lock(&mutex_memoria);
			flush();
			pthread_mutex_unlock(&mutex_memoria);
			printf("Se ha limpiado la cache\n");
		}
		else if(strcmp(command, "retardo") == 0){
			scanf("%d",numero);
			if(*numero>=0){
				pthread_mutex_lock(&mutex_memoria);
				data_config.retardo_memoria = *numero;
				pthread_mutex_unlock(&mutex_memoria);
				printf("Retardo modificado\n");
			}else{
				printf("Error: el retardo debe ser mayor o igual a 0\n");
			}
		}
		else{
			printf("Comando incorrecto\n");
		}
		free(numero);
		free(subcommand);
		free(command);
	}
}

void *thread_proceso(int fd){
	printf("Nueva conexion en socket %d\n", fd);
	int bytes, codigo, messageLength, flag = 1;
	u_int32_t PID;
	int pagina, paginas_stack, offset, inicio, value;

	while(flag == 1){
		bytes = recv(fd,&codigo,sizeof(int),0);
		flag = verificar_conexiones_socket(fd, bytes);
		if(flag == 1){
			pthread_mutex_lock(&mutex_memoria);
			if(codigo == HANDSHAKE){
				send(fd, &data_config.marco_size, sizeof(int), 0);
			}
			if(codigo == NUEVO_PROCESO){
				espacio_reservado *espacio;
				bytes = recv(fd, &PID, sizeof(u_int32_t), 0);
				recv(fd, &paginas_stack, sizeof(int), 0);
				recv(fd, &messageLength, sizeof(int), 0);
				void* aux = malloc(messageLength+2);
				memset(aux,0,messageLength+2);
				recv(fd, aux, messageLength, 0);
				memset(aux+messageLength+1,'\0',1);

				//char* charaux = (char*) aux;
				//printf("Y este es el script:\n %s\n", charaux);

				//Ahora buscamos espacio
				espacio = buscar_espacio(PID, messageLength+2, aux, paginas_stack);

				send(fd, &espacio->page_counter, sizeof(int), 0);
				send(fd, &espacio->direccion, sizeof(int), 0);

				free(espacio);
				free(aux);
			}
			if(codigo == BUSCAR_INSTRUCCION){

				recv(fd, &PID, sizeof(u_int32_t), 0);
				recv(fd, &pagina, sizeof(int), 0);
				recv(fd, &inicio, sizeof(int), 0);
				recv(fd, &offset, sizeof(int), 0);

				char *instruccion = (char *) lectura(PID, pagina, inicio, offset);
				int tamanio = offset - 1;
				void *buffer = malloc(sizeof(int) + tamanio +1);
				memset(buffer, 0, sizeof(int) + tamanio +1);
				memcpy(buffer, &tamanio, sizeof(int));
				memcpy(buffer+sizeof(int), instruccion, tamanio);
				memset(buffer+sizeof(int)+tamanio,'\0',1);

				send(fd, buffer, sizeof(int) + tamanio + 1, 0);
				free(instruccion);
				free(buffer);
			}
			if(codigo == GUARDAR_VALOR){
				recv(fd, &PID, sizeof(u_int32_t), 0);
				recv(fd, &pagina, sizeof(int), 0);
				recv(fd, &offset, sizeof(int), 0);
				recv(fd, &value, sizeof(int), 0);

				escritura(PID, pagina, offset, sizeof(int), &value);

			}
			if(codigo == ELIMINAR_PROCESO){
				recv(fd, &PID, sizeof(int), 0);
				finalizar_proceso(PID);
				printf("Proceso %d borrado correctamente\n", PID);
			}
			if(codigo == OBTENER_VALOR){
				recv(fd, &PID, sizeof(u_int32_t), 0);
				recv(fd, &pagina, sizeof(int), 0);
				recv(fd, &inicio, sizeof(int), 0);
				recv(fd, &offset, sizeof(int), 0);

				int *valor = malloc(sizeof(int));
				valor = (int *) lectura(PID, pagina, inicio, offset);

				value = *valor;
				send(fd, &value, sizeof(int), 0);
				free(valor);
			}
			if(codigo == SOLICITAR_HEAP){
				recibir(fd, &PID, sizeof(uint32_t));
				espacio_reservado *espacio;
				espacio = buscar_espacio_para_heap(PID);

				enviar(fd, &(espacio->page_counter), sizeof(int32_t));
			}
			if(codigo == ALOCAR){
				uint32_t espacio, pagina;
				espacio_reservado *puntero;
				recibir(fd, &PID, sizeof(uint32_t));
				recibir(fd, &espacio, sizeof(uint32_t));
				recibir(fd, &pagina, sizeof(uint32_t));

				puntero = alocar(PID, pagina, espacio);

				enviar(fd, &(puntero->direccion), sizeof(int32_t));
				enviar(fd, &(puntero->page_counter), sizeof(int32_t));

				free(puntero);
			}
			if(codigo == LIBERAR){
				uint32_t direccion, pagina;
				recibir(fd, &PID, sizeof(uint32_t));
				recibir(fd, &pagina, sizeof(uint32_t));
				recibir(fd, &direccion, sizeof(uint32_t));

				liberar(PID, pagina, direccion);
			}
		}
		pthread_mutex_unlock(&mutex_memoria);
	}
	pthread_exit(NULL);
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

	/*Creacion de la Cache, el contenido y el Admin de cache*/

	contenido_cache = calloc(data_config.entradas_cache, data_config.marco_size);
	cache = malloc(data_config.entradas_cache*sizeof(entrada_cache));
	admin_cache = malloc(data_config.entradas_cache*sizeof(entrada_admin_cache));
	inicializar_cache();

	/*Creacion de Indice de Hash*/

	inicializar_indice();

	/*Creacion de Hilos*/

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
	bytes = recv(newfd,&handshake,sizeof(u_int32_t),0);
	if(bytes > 0 && handshake == HANDSHAKE){
				send(newfd, &data_config.marco_size, sizeof(u_int32_t), 0);
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

	free(contenido_cache);
	free(admin_cache);
	free(cache);
	free(memoria);
	free(cfgPath);
	config_destroy(config);
	return 0;
}
