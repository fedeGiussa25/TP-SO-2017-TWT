typedef struct{
	int sock_fd;
	int proceso;
}proceso_conexion;

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


void *PCB_cereal(script_manager_setup *sms,uint32_t *pidPCB,uint32_t *stack_size ){
	uint32_t codigo_cpu =2;
	sendbuf = malloc(sizeof(uint32_t)*4 + sms->messageLength);
	memcpy(sendbuf,&codigo_cpu,sizeof(int));
	memcpy(sendbuf+sizeof(int),(pidPCB),sizeof(uint32_t));
	memcpy(sendbuf+sizeof(int)+sizeof(uint32_t),(stack_size),sizeof(uint32_t));
	memcpy(sendbuf+sizeof(int)*2+sizeof(uint32_t),&(sms->messageLength),sizeof(uint32_t));
	memcpy(sendbuf+sizeof(int)*3+sizeof(uint32_t),sms->realbuf,sms->messageLength);

	return sendbuf;
}

void guardado_en_memoria(script_manager_setup* sms, PCB* pcb_to_use){
	void *sendbuf;
	uint32_t codigo_cpu = 2, numbytes, page_counter, direccion;

	//Le mando el codigo y el largo a la memoria
	//INICIO SERIALIZACION PARA MEMORIAAAAA
	sendbuf = PCB_cereal(sms,&(pcb_to_use->pid),&(data_config.stack_size));
	
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
			pcb_to_use->primerPaginaStack=page_counter-tamanio_del_stack; //pagina donde arranca el stack
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
}

void send_PCB(proceso_conexion *cpu, PCB *pcb){
	int tamanio_indice_codigo = (pcb->cantidad_de_instrucciones)*sizeof(entrada_indice_de_codigo);
	int tamanio_indice_stack = 1*sizeof(registroStack); //Esto es solo para probar
	//Creamos nuestro heroico buffer, quien se va a encargar de llevar el PCB a la CPU
	void *ultraBuffer = malloc(sizeof(int)*9 + sizeof(u_int32_t) + tamanio_indice_codigo+tamanio_indice_stack);

	memcpy(ultraBuffer, &(pcb->pid), sizeof(u_int32_t));
	memcpy(ultraBuffer+sizeof(u_int32_t), &(pcb->page_counter), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+sizeof(int), &(pcb->direccion_inicio_codigo), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+2*sizeof(int), &(pcb->program_counter), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+3*sizeof(int), &(pcb->cantidad_de_instrucciones), sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+4*sizeof(int), &tamanio_indice_codigo, sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+5*sizeof(int), pcb->indice_de_codigo, tamanio_indice_codigo);
	memcpy(ultraBuffer+sizeof(u_int32_t)+5*sizeof(int)+tamanio_indice_codigo,&(pcb->tamanioStack),sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+6*sizeof(int)+tamanio_indice_codigo,&(pcb->primerPaginaStack),sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+7*sizeof(int)+tamanio_indice_codigo,&(pcb->stackPointer),sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+8*sizeof(int)+tamanio_indice_codigo, &tamanio_indice_stack, sizeof(int));
	memcpy(ultraBuffer+sizeof(u_int32_t)+9*sizeof(int)+tamanio_indice_codigo,pcb->stack_index,tamanio_indice_stack);


	send(cpu->sock_fd, ultraBuffer, sizeof(int)*9 + sizeof(u_int32_t) + tamanio_indice_codigo+tamanio_indice_stack,0);

	printf("Mande un PCB a una CPU :D\n\n");
	free(ultraBuffer);	//Cumpliste con tu mision. Ya eres libre.
}
