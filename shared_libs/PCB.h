#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>

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
#include "../config_shortcuts/config_shortcuts.h"
#include <parser/metadata_program.h>

#define FULLPCB 123

#define MEMPCB 10101010

typedef struct{
	uint32_t inicio;
	uint32_t offset;
} entrada_indice_de_codigo;

typedef struct{
	uint32_t page;
	uint32_t offset;
	uint32_t size;
}pagoffsize;

typedef struct{
	t_list* args;
	t_list* vars;
	uint32_t ret_pos;
	pagoffsize ret_var;
}registroStack;

typedef struct{
	char id;
	int page;
	int offset;
	int size;
}variable;

typedef struct{
	int sock_fd;
	int proceso;
}proceso_conexion;

typedef struct{
	uint32_t pid;
	uint32_t page_counter;
	uint32_t direccion_inicio_codigo;
	uint32_t program_counter;
	uint32_t cantidad_de_instrucciones;
	entrada_indice_de_codigo* indice_de_codigo;
	char* lista_de_etiquetas;
	uint32_t lista_de_etiquetas_length;
	uint32_t exit_code;
	char* estado;
	t_list* stack_index;
	uint32_t primerPaginaStack;
	uint32_t stackPointer;
	uint32_t tamanioStack;
}PCB;

typedef struct { //Estructura auxiliar para ejecutar el manejador de scripts
	uint32_t fd_consola; //La Consola que me mando el script
	uint32_t fd_mem; //La memoria
	uint32_t grado_multiprog; //El grado de multiprog actual
	uint32_t messageLength; //El largo del script
	void* realbuf; //El script serializado
}script_manager_setup;

uint32_t enviar(uint32_t socketd, void *buf,uint32_t bytestoSend){
	uint32_t numbytes;
	if (numbytes = send(socketd, buf, bytestoSend, 0) <= 0){
		perror("Error al Enviar\n");
	}
	return numbytes;
}
uint32_t recibir(uint32_t socketd, void *buf,uint32_t bytestoRecv){
	uint32_t numbytes =recv(socketd, buf, bytestoRecv, 0);
	if(numbytes <=0){
		if ((numbytes) <0) {
			perror("recv");	
		}else{
			printf("DESCONECTADO socket %d\n",socketd);
		}
		close(socketd);
	}
	return numbytes;
}

void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) 
		return &(((struct sockaddr_in*)sa)->sin_addr);
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
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

void remove_and_destroy_by_fd_socket(t_list *lista, int sockfd){
	bool _remove_socket(void* unaConex)
  	    {
			proceso_conexion *conex = (proceso_conexion*) unaConex;
			return conex->sock_fd == sockfd;
	    }
	proceso_conexion* conexion_encontrada =  list_remove_by_condition(lista,*_remove_socket);
	free(conexion_encontrada);
}


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

int corrimiento;
void *sendbuf;
u_int32_t tamanio_stack;

void serializarVariables(variable* var);
void serializarElestac(registroStack* registro);
void sumar_tamanio_registro(registroStack* unRegistro);

void serializarVariables(variable* var)
{
	memcpy(sendbuf+corrimiento, &(var->id), sizeof(char));
	corrimiento = corrimiento+sizeof(char);
	memcpy(sendbuf+corrimiento, &(var->offset), sizeof(int));
	corrimiento = corrimiento+sizeof(int);
	memcpy(sendbuf+corrimiento, &(var->page), sizeof(int));
	corrimiento = corrimiento+sizeof(int);
	memcpy(sendbuf+corrimiento, &(var->size), sizeof(int));
	corrimiento = corrimiento+sizeof(int);
}
void serializarElestac(registroStack* registro)
{

	//Meto en el sendbuf los argumentos
	int cantArgumentos = registro->args->elements_count;
	memcpy(sendbuf+corrimiento, &cantArgumentos, sizeof(int));
	corrimiento = corrimiento+sizeof(int);

	list_iterate(registro->args, (void *) serializarVariables); //Funcion casteada a (void*) porque list_iterate lo requiere

	//Meto en el sendbuf las variables

	int cantVariables = registro->vars->elements_count;
	memcpy(sendbuf+corrimiento, &cantVariables, sizeof(int));
	corrimiento = corrimiento+sizeof(int);

	list_iterate(registro->vars, (void *) serializarVariables);

	//Meto en el sendbuf retPos

	memcpy(sendbuf+corrimiento, &(registro->ret_pos), sizeof(uint32_t));
	corrimiento = corrimiento + sizeof(uint32_t);

	//Meto en el sendbuf retVar

	memcpy(sendbuf+corrimiento, &(registro->ret_var.offset),sizeof(int));
	corrimiento = corrimiento + sizeof(int);
	memcpy(sendbuf+corrimiento, &(registro->ret_var.page),sizeof(int));
	corrimiento = corrimiento + sizeof(int);
	memcpy(sendbuf+corrimiento, &(registro->ret_var.size),sizeof(int));
	corrimiento = corrimiento + sizeof(int);
}

registroStack* registro;

void sumar_tamanio_registro(registroStack *unRegistro){

	uint32_t tamanio_argumentos = sizeof(variable)*list_size(unRegistro->args);
	uint32_t tamanio_variables = sizeof(variable)*list_size(unRegistro->vars);

	uint32_t tamanio_resto = sizeof(pagoffsize)+sizeof(uint32_t);

	uint32_t cantVarsYArgs = sizeof(uint32_t)*2;


	tamanio_stack = tamanio_stack + tamanio_argumentos + tamanio_variables + tamanio_resto + cantVarsYArgs;

}

void *PCB_cereal(script_manager_setup *sms,PCB *pcb,uint32_t *stack_size,uint32_t objetivo){
	uint32_t codigo_cpu, tamanio_indice_codigo, tamanio_indice_stack, tamanio_indice_etiquetas, cantRegistros;
	switch(objetivo){
		case MEMPCB:
			codigo_cpu = 2;
			sendbuf = malloc(sizeof(uint32_t)*4 + sms->messageLength);
			memcpy(sendbuf,&codigo_cpu,sizeof(uint32_t));
			memcpy(sendbuf+sizeof(int),&(pcb->pid),sizeof(uint32_t));
			memcpy(sendbuf+sizeof(int)+sizeof(uint32_t),(stack_size),sizeof(uint32_t));
			memcpy(sendbuf+sizeof(int)*2+sizeof(uint32_t),&(sms->messageLength),sizeof(uint32_t));
			memcpy(sendbuf+sizeof(int)*3+sizeof(uint32_t),sms->realbuf,sms->messageLength);
			break;
		case FULLPCB:
			tamanio_indice_codigo = (pcb->cantidad_de_instrucciones)*sizeof(entrada_indice_de_codigo);
			//tamanio_indice_stack = calcularTamañoStack();
			tamanio_indice_etiquetas = pcb->lista_de_etiquetas_length;
			cantRegistros = pcb->stack_index->elements_count; //Es la cantidad de registros de Stack

			tamanio_stack = 0;

			if(cantRegistros>0){
				list_iterate(pcb->stack_index, (void*) sumar_tamanio_registro);
			}

			sendbuf = malloc(sizeof(uint32_t)*10 + sizeof(u_int32_t) +tamanio_indice_etiquetas+ tamanio_indice_codigo + tamanio_stack);
			memcpy(sendbuf, &(pcb->pid), sizeof(u_int32_t));
			memcpy(sendbuf+sizeof(uint32_t), &(pcb->page_counter), sizeof(uint32_t));
			//memcpy(sendbuf+sizeof(uint32_t)+2*sizeof(uint32_t), pcb->lista_de_etiquetas, tamanio_indice_etiquetas);
			memcpy(sendbuf+sizeof(uint32_t)+sizeof(uint32_t), &(pcb->direccion_inicio_codigo), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+2*sizeof(uint32_t), &(pcb->program_counter), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+3*sizeof(uint32_t), &(pcb->cantidad_de_instrucciones), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+4*sizeof(uint32_t), &tamanio_indice_codigo, sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+5*sizeof(uint32_t), pcb->indice_de_codigo, tamanio_indice_codigo);
			memcpy(sendbuf+sizeof(uint32_t)+5*sizeof(uint32_t)+tamanio_indice_codigo,&(pcb->tamanioStack),sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+6*sizeof(uint32_t)+tamanio_indice_codigo,&(pcb->primerPaginaStack),sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+7*sizeof(uint32_t)+tamanio_indice_codigo,&(pcb->stackPointer),sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+8*sizeof(uint32_t)+tamanio_indice_codigo,&cantRegistros, sizeof(uint32_t));
			//memcpy(sendbuf+sizeof(uint32_t)+9*sizeof(uint32_t)+tamanio_indice_codigo,pcb->stack_index,tamanio_indice_stack);

			memcpy(sendbuf+sizeof(uint32_t)+9*sizeof(uint32_t)+tamanio_indice_codigo,&tamanio_indice_etiquetas, sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)+10*sizeof(uint32_t)+tamanio_indice_codigo, pcb->lista_de_etiquetas, tamanio_indice_etiquetas);

			//Corrimiento es el tamaño de lo que se guardo hasta ahora (falta el stack)

			corrimiento = sizeof(uint32_t)+10*sizeof(uint32_t)+tamanio_indice_codigo+tamanio_indice_etiquetas;

			list_iterate(pcb->stack_index, (void*) serializarElestac);
			break;
	}

	return sendbuf;
}

/*void guardado_en_memoria(script_manager_setup* sms, PCB* pcb_to_use){
	void *sendbuf;
	uint32_t codigo_cpu = 2, numbytes, page_counter, direccion;

	//Le mando el codigo y el largo a la memoria
	//INICIO SERIALIZACION PARA MEMORIAAAAA
	sendbuf = PCB_cereal(sms,pcb_to_use,&(data_config.stack_size),MEMPCB);
	
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
}*/

void send_PCB(uint32_t sock_fd, PCB *pcb, uint32_t codigo){
	int tamanio_indice_codigo = (pcb->cantidad_de_instrucciones)*sizeof(entrada_indice_de_codigo);
	//int tamanio_indice_stack=calcularTamañoStack();
	uint32_t tamanio_indice_etiquetas = pcb->lista_de_etiquetas_length;
	//Creamos nuestro heroico buffer, quien se va a encargar de llevar el PCB a la CPU
	void *ultraBuffer = PCB_cereal(NULL,pcb,NULL,FULLPCB);

	uint32_t tamanio_total_buffer = sizeof(uint32_t)*10 + sizeof(u_int32_t) +tamanio_indice_etiquetas +tamanio_indice_codigo+tamanio_stack;

	//Creamos un buffer completo, que ademas del PCB llevara un codigo.
	void *ultimateBuffer = malloc(tamanio_total_buffer+sizeof(uint32_t));
	memcpy(ultimateBuffer, &codigo, sizeof(uint32_t));
	memcpy(ultimateBuffer+sizeof(uint32_t), ultraBuffer, tamanio_total_buffer);

	send(sock_fd, ultimateBuffer, sizeof(uint32_t) + tamanio_total_buffer,0);

	printf("Mande un PCB :D\n\n");
	free(ultraBuffer);	//Cumpliste con tu mision. Ya eres libre.
	free(ultimateBuffer); //Vos tambien.
}
