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
	uint32_t page;
	uint32_t offset;
	uint32_t size;
}__attribute__((packed)) variable;

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

int32_t enviar(uint32_t socketd, void *buf,int32_t bytestoSend){
	int32_t numbytes;
	if (numbytes = send(socketd, buf, bytestoSend, 0) <= 0){
		perror("Error al Enviar\n");
	}
	return numbytes;
}
int32_t recibir(uint32_t socketd, void *buf,int32_t bytestoRecv){
	int32_t numbytes =recv(socketd, buf, bytestoRecv, 0);
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

int corrimiento;
void *sendbuf;
void *stackbuf;
u_int32_t tamanio_stack;

/*
void serializarVariables(variable* var);
void serializarElestac(registroStack* registro);
void sumar_tamanio_registro(registroStack* unRegistro);
*/
void free_variable(variable *unaVar){
	free(unaVar);
}

void free_Stack(registroStack* registro){
	list_destroy_and_destroy_elements(registro->args, (void *) free_variable);
	list_destroy_and_destroy_elements(registro->vars, (void *) free_variable);
	free(registro);
}

void free_PCB(PCB *unPCB){
	free(unPCB->indice_de_codigo);
	free(unPCB->lista_de_etiquetas);
	list_destroy_and_destroy_elements(unPCB->stack_index, (void *) free_Stack);
	free(unPCB);
}

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
void serializarVariablesV2(variable* var)
{
	memcpy(stackbuf+corrimiento, &(var->id), sizeof(char));
	corrimiento = corrimiento+sizeof(char);
	memcpy(stackbuf+corrimiento, &(var->offset), sizeof(uint32_t));
	corrimiento = corrimiento+sizeof(uint32_t);
	memcpy(stackbuf+corrimiento, &(var->page), sizeof(uint32_t));
	corrimiento = corrimiento+sizeof(uint32_t);
	memcpy(stackbuf+corrimiento, &(var->size), sizeof(uint32_t));
	corrimiento = corrimiento+sizeof(uint32_t);
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

void serializarElestacV2(registroStack* registro)
{

	//Meto en el stackbuf los argumentos
	uint32_t cantArgumentos = list_size(registro->args);
	memcpy(stackbuf+corrimiento, &cantArgumentos, sizeof(uint32_t));
	corrimiento = corrimiento+sizeof(uint32_t);

	list_iterate(registro->args, (void *) serializarVariablesV2); //Funcion casteada a (void*) porque list_iterate lo requiere

	//Meto en el stackbuf las variables

	uint32_t cantVariables = registro->vars->elements_count;
	memcpy(stackbuf+corrimiento, &cantVariables, sizeof(uint32_t));
	corrimiento = corrimiento+sizeof(uint32_t);

	list_iterate(registro->vars, (void *) serializarVariablesV2);

	//Meto en el stackbuf retPos

	memcpy(stackbuf+corrimiento, &(registro->ret_pos), sizeof(uint32_t));
	corrimiento = corrimiento + sizeof(uint32_t);

	//Meto en el stackbuf retVar

	memcpy(stackbuf+corrimiento, &(registro->ret_var.offset),sizeof(uint32_t));
	corrimiento = corrimiento + sizeof(uint32_t);
	memcpy(stackbuf+corrimiento, &(registro->ret_var.page),sizeof(uint32_t));
	corrimiento = corrimiento + sizeof(uint32_t);
	memcpy(stackbuf+corrimiento, &(registro->ret_var.size),sizeof(uint32_t));
	corrimiento = corrimiento + sizeof(uint32_t);
}

registroStack* registro;

void sumar_tamanio_registro(registroStack *unRegistro){

	uint32_t tamanio_argumentos = sizeof(variable)*list_size(unRegistro->args);
	uint32_t tamanio_variables = sizeof(variable)*list_size(unRegistro->vars);

	uint32_t tamanio_resto = sizeof(pagoffsize)+sizeof(uint32_t);

	//uint32_t cantVarsYArgs=0;

	//if(list_size(unRegistro->args)>0){

/*	}
	if(list_size(unRegistro->vars)>0){
		cantVarsYArgs = cantVarsYArgs + sizeof(uint32_t);
	}*/


	tamanio_stack = tamanio_stack + tamanio_argumentos + tamanio_variables + tamanio_resto;

}

void sumar_tamanio_registroV2(registroStack *unRegistro){

	uint32_t tamanio_argumentos = sizeof(variable)*list_size(unRegistro->args);
	uint32_t tamanio_variables = sizeof(variable)*list_size(unRegistro->vars);

	uint32_t tamanio_resto = sizeof(pagoffsize)+sizeof(uint32_t);

	//uint32_t cantVarsYArgs=0;

	//if(list_size(unRegistro->args)>0){
		uint32_t cantVarsYArgs = sizeof(uint32_t)*2;
/*	}
	if(list_size(unRegistro->vars)>0){
		cantVarsYArgs = cantVarsYArgs + sizeof(uint32_t);
	}*/


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

			sendbuf = malloc(sizeof(uint32_t)*10 + sizeof(u_int32_t) +tamanio_indice_etiquetas+ tamanio_indice_codigo + tamanio_stack + 2);
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

void *PCB_cerealV2(script_manager_setup *sms,PCB *pcb,uint32_t *stack_size,uint32_t objetivo){
	uint32_t codigo_cpu, tamanio_indice_codigo, tamanio_indice_stack, tamanio_indice_etiquetas, cantRegistros,tamanio_estado;
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

			tamanio_indice_etiquetas = pcb->lista_de_etiquetas_length;
			cantRegistros = pcb->stack_index->elements_count; //Es la cantidad de registros de Stack
			tamanio_estado = strlen(pcb->estado) + 1;

			tamanio_stack = 0;

			if(cantRegistros>0){
				list_iterate(pcb->stack_index, (void*) sumar_tamanio_registroV2);
			}

			sendbuf = malloc(sizeof(uint32_t)*9/*Propio de PCB*/+ sizeof(int32_t)/*exit_code*/+ sizeof(uint32_t)/*tamanio de i_de_codigo*/+ tamanio_indice_codigo+
					tamanio_indice_etiquetas+ sizeof(uint32_t)/*tamanio de estado*/ + tamanio_estado+ sizeof(uint32_t)/*cant_registros_stack*/+ tamanio_stack);

			stackbuf = malloc(tamanio_stack);
			memcpy(sendbuf, &(pcb->pid), sizeof(u_int32_t));
			memcpy(sendbuf+sizeof(uint32_t), &(pcb->page_counter), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*2, &(pcb->direccion_inicio_codigo), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*3, &(pcb->program_counter), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*4, &(pcb->cantidad_de_instrucciones), sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*5, &tamanio_indice_codigo, sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*6, pcb->indice_de_codigo, tamanio_indice_codigo);
			memcpy(sendbuf+sizeof(uint32_t)*6+tamanio_indice_codigo, &tamanio_indice_etiquetas, sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*7+tamanio_indice_codigo, pcb->lista_de_etiquetas, tamanio_indice_etiquetas);
			memcpy(sendbuf+sizeof(uint32_t)*7+tamanio_indice_codigo+tamanio_indice_etiquetas, &tamanio_estado, sizeof(uint32_t));
			memcpy(sendbuf+sizeof(uint32_t)*8+tamanio_indice_codigo+tamanio_indice_etiquetas, pcb->estado, tamanio_estado);

			memcpy(sendbuf+sizeof(uint32_t)*8+tamanio_indice_codigo+tamanio_indice_etiquetas+tamanio_estado, &(pcb->exit_code), sizeof(int32_t));
			memcpy(sendbuf+sizeof(uint32_t)*9+tamanio_indice_codigo+tamanio_indice_etiquetas+tamanio_estado, &(pcb->primerPaginaStack), sizeof(int32_t));
			memcpy(sendbuf+sizeof(uint32_t)*10+tamanio_indice_codigo+tamanio_indice_etiquetas+tamanio_estado, &(pcb->stackPointer), sizeof(int32_t));
			memcpy(sendbuf+sizeof(uint32_t)*11+tamanio_indice_codigo+tamanio_indice_etiquetas+tamanio_estado, &(pcb->tamanioStack), sizeof(int32_t));

			memcpy(sendbuf+sizeof(uint32_t)*12+tamanio_indice_codigo+tamanio_indice_etiquetas+tamanio_estado, &cantRegistros, sizeof(int32_t));

			corrimiento = 0;
			if(cantRegistros>0)
			{
			list_iterate(pcb->stack_index, (void*) serializarElestacV2);

			memcpy(sendbuf+sizeof(uint32_t)*13+tamanio_indice_codigo+tamanio_indice_etiquetas+tamanio_estado, stackbuf, tamanio_stack);
			}
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
	uint32_t tamanio_indice_etiquetas = pcb->lista_de_etiquetas_length;
	//Creamos nuestro heroico buffer, quien se va a encargar de llevar el PCB a la CPU
	void *ultraBuffer = PCB_cereal(NULL,pcb,NULL,FULLPCB);

	uint32_t tamanio_total_buffer = sizeof(uint32_t)*10 + sizeof(u_int32_t) +tamanio_indice_etiquetas +tamanio_indice_codigo+tamanio_stack;

	//Creamos un buffer completo, que ademas del PCB llevara un codigo.
	void *ultimateBuffer = malloc(tamanio_total_buffer+sizeof(uint32_t));
	memcpy(ultimateBuffer, &codigo, sizeof(uint32_t));
	memcpy(ultimateBuffer+sizeof(uint32_t), ultraBuffer, tamanio_total_buffer);

	send(sock_fd, ultimateBuffer, sizeof(uint32_t) + tamanio_total_buffer,0);

	//printf("Se ha enviado un PCB\n\n");
	free(ultraBuffer);	//Cumpliste con tu mision. Ya eres libre.
	free(ultimateBuffer); //Vos tambien.
}
void send_PCBV2(uint32_t sock_fd, PCB *pcb, int32_t codigo){
	int tamanio_indice_codigo = (pcb->cantidad_de_instrucciones)*sizeof(entrada_indice_de_codigo);
	uint32_t tamanio_indice_etiquetas = pcb->lista_de_etiquetas_length;
	uint32_t tamanio_estado = strlen(pcb->estado) + 1;
	//Creamos nuestro heroico buffer, quien se va a encargar de llevar el PCB a la CPU
	void *ultraBuffer = PCB_cerealV2(NULL,pcb,NULL,FULLPCB);

	uint32_t tamanio_total_buffer = sizeof(uint32_t)*9/*Propio de PCB*/+ sizeof(int32_t)/*exit_code*/+ sizeof(uint32_t)/*tamanio de i_de_codigo*/+ tamanio_indice_codigo+
			tamanio_indice_etiquetas+ sizeof(uint32_t)/*tamanio de estado*/ + tamanio_estado+ sizeof(uint32_t)/*cant_registros_stack*/+ tamanio_stack;

	//Creamos un buffer completo, que ademas del PCB llevara un codigo.
	void *ultimateBuffer = malloc(tamanio_total_buffer+sizeof(int32_t));
	memcpy(ultimateBuffer, &codigo, sizeof(int32_t));
	memcpy(ultimateBuffer+sizeof(int32_t), ultraBuffer, tamanio_total_buffer);

	send(sock_fd, ultimateBuffer, sizeof(int32_t) + tamanio_total_buffer,0);

	//printf("Se ha enviado un PCB\n\n");
	free(ultraBuffer);	//Cumpliste con tu mision. Ya eres libre.
	free(ultimateBuffer); //Vos tambien.
}

PCB* recibirPCB(uint32_t fd_socket)
{
	u_int32_t pid;

	uint32_t page_counter, direccion_inicio_codigo, program_counter, cantidad_de_instrucciones,
	stack_size, primerPagStack, stack_pointer;

	PCB* pcb = malloc(sizeof(PCB));

	uint32_t tamanio_indice_codigo, tamanio_indice_etiquetas;
	uint32_t cantRegistros =0;

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

	//recibo indice etiquetas:

	//printf("\nRECIBI TODO MENOS ETIQUETAS\n");

	recv(fd_socket, &tamanio_indice_etiquetas, sizeof(uint32_t),0);


	//printf("RECIBI TAMANIO ETIQUETAS Y ES: %d\n", tamanio_indice_etiquetas);

	char* indice_de_etiquetas = malloc(tamanio_indice_etiquetas);

	if(tamanio_indice_etiquetas>0)
	{
	recv(fd_socket, indice_de_etiquetas, tamanio_indice_etiquetas,0);


	//printf("RECIBI indice ETIQUETAS\n\n");

	}


	//-----Recibo indice de Stack-----

	pcb->stack_index = list_create();

	int registrosAgregados = 0;

	int cantArgumentos=0, cantVariables=0;

	if(cantRegistros>0){


	while(registrosAgregados < cantRegistros)
	{
		recv(fd_socket, &cantArgumentos, sizeof(int),0);

		//printf("cant argums: %d\n", cantArgumentos);
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
		//printf("cant vars: %d\n", cantVariables);
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

	return pcb;
}
PCB* recibirPCBV2(uint32_t fd_socket)
{
	uint32_t pid, page_counter, direccion_inicio_codigo, program_counter, cantidad_de_instrucciones, stack_size, primerPagStack, stack_pointer, tamanio_estado;
	int32_t exit_code;


	uint32_t tamanio_indice_codigo, tamanio_indice_etiquetas;
	uint32_t cantRegistros =0;

	PCB* pcb = malloc(sizeof(PCB));

	recv(fd_socket, &pid, sizeof(u_int32_t),MSG_WAITALL);

	recv(fd_socket, &page_counter, sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &direccion_inicio_codigo, sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &program_counter, sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &cantidad_de_instrucciones, sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &tamanio_indice_codigo, sizeof(uint32_t),MSG_WAITALL);

	entrada_indice_de_codigo *indice_de_codigo = malloc(tamanio_indice_codigo);

	recv(fd_socket, indice_de_codigo, tamanio_indice_codigo,MSG_WAITALL);

	recv(fd_socket, &tamanio_indice_etiquetas, sizeof(uint32_t),MSG_WAITALL);

	char* indice_de_etiquetas = malloc(tamanio_indice_etiquetas);

	if(tamanio_indice_etiquetas>0)
	{
		recv(fd_socket, indice_de_etiquetas, tamanio_indice_etiquetas,MSG_WAITALL);
	}

	recv(fd_socket, &tamanio_estado, sizeof(uint32_t),MSG_WAITALL);

	char *estado = malloc(tamanio_estado);

	recv(fd_socket, estado, tamanio_estado,MSG_WAITALL);

	recv(fd_socket, &exit_code, sizeof(int32_t),MSG_WAITALL);

	recv(fd_socket, &primerPagStack,sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &stack_pointer,sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &stack_size,sizeof(uint32_t),MSG_WAITALL);

	recv(fd_socket, &cantRegistros,sizeof(uint32_t),MSG_WAITALL);

	//Recibo STACK

	pcb->stack_index = list_create();

	int registrosAgregados = 0;

	int cantArgumentos=0, cantVariables=0;

	if(cantRegistros>0){

	while(registrosAgregados < cantRegistros)
	{
		recv(fd_socket, &cantArgumentos, sizeof(uint32_t),MSG_WAITALL);

		//printf("cant argums: %d\n", cantArgumentos);
		registroStack* nuevoReg = malloc(sizeof(registroStack));

		nuevoReg->args = list_create();

		if(cantArgumentos>0) //Si tiene argumentos
		{
		//Recibo argumentos:

		int argumentosAgregados = 0;

		while(argumentosAgregados < cantArgumentos)
		{
			variable *nuevoArg = malloc(sizeof(variable));

			recv(fd_socket, &(nuevoArg->id), sizeof(char),MSG_WAITALL);

			recv(fd_socket, &(nuevoArg->offset), sizeof(uint32_t),MSG_WAITALL);

			recv(fd_socket, &(nuevoArg->page), sizeof(uint32_t),MSG_WAITALL);

			recv(fd_socket, &(nuevoArg->size), sizeof(uint32_t),MSG_WAITALL);

			list_add(nuevoReg->args, nuevoArg);
			argumentosAgregados++;
			}
		} //Fin recepcion argumentos

			recv(fd_socket, &cantVariables, sizeof(int),MSG_WAITALL);

			nuevoReg->vars= list_create();
			//printf("cant vars: %d\n", cantVariables);
			if(cantVariables>0) //Si tiene variables
			{
			//Recibo variables:

			int variablesAgregadas = 0;

			while(variablesAgregadas < cantVariables)
			{
				variable *nuevaVar = malloc(sizeof(variable));

				recv(fd_socket, &(nuevaVar->id), sizeof(char),MSG_WAITALL);

				recv(fd_socket, &(nuevaVar->offset), sizeof(uint32_t),MSG_WAITALL);

				recv(fd_socket, &(nuevaVar->page), sizeof(uint32_t),MSG_WAITALL);

				recv(fd_socket, &(nuevaVar->size), sizeof(uint32_t),MSG_WAITALL);


				list_add(nuevoReg->vars, nuevaVar);

				variablesAgregadas++;

			}
		} //Fin recepcion variables

			//Recibo retPos

			recv(fd_socket, &(nuevoReg->ret_pos), sizeof(uint32_t),MSG_WAITALL);


			//Recibo retVar

			recv(fd_socket, &(nuevoReg->ret_var.offset), sizeof(uint32_t),MSG_WAITALL);

			recv(fd_socket, &(nuevoReg->ret_var.page), sizeof(uint32_t),MSG_WAITALL);

			recv(fd_socket, &(nuevoReg->ret_var.size), sizeof(uint32_t),MSG_WAITALL);

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
	pcb->exit_code=exit_code;
	pcb->estado = estado;
	//strcpy(pcb->estado, estado);

}
void liberar_PCB(PCB *pcb){
	free(pcb->indice_de_codigo);
	free(pcb->estado);
	free(pcb->lista_de_etiquetas);

	int cant_reg_stack = list_size(pcb->stack_index), i;

	for(i=0; i< cant_reg_stack; i++){
		registroStack *unReg = list_remove(pcb->stack_index, 0);
		int cant_vars = list_size(unReg->vars), cant_args = list_size(unReg->args), j, k;
		for(j=0; j<cant_vars; j++){
			variable *unaVar = list_remove(unReg->vars, 0);
			free(unaVar);
		}
		list_destroy(unReg->vars);
		for(k=0; k<cant_args; k++){
			variable *unArg = list_remove(unReg->args, 0);
			free(unArg);
		}
		list_destroy(unReg->args);
		free(unReg);
	}

	list_destroy(pcb->stack_index);
	free(pcb);
}
