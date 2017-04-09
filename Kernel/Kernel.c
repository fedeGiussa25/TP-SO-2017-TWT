/*
 ============================================================================
 Name        : kernelproto.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>
#include <commons/string.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

typedef struct {
	char* puerto_prog;
	char* puerto_cpu;
	char* ip_memoria;
	char* puerto_memoria;
	char* ip_fs;
	char* puerto_fs;
	int quantum;
	int quantum_sleep;
	char* algoritmo;
	int grado_multiprog;
	char** sem_ids;
	int* sem_init;
	char** shared_vars;
	int stack_size;
}kernel_config;

//VARIABLES GLOBALES
int mem_sock;

fd_set fd_programas;
fd_set fd_CPUs;

kernel_config data_config;

//FUNCIONES
void *get_in_addr(struct sockaddr *sa)
{
if (sa->sa_family == AF_INET) {
return &(((struct sockaddr_in*)sa)->sin_addr);
}
return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *hilo_CPUs(){
	//variiables para conexiones con CPUs
	int listener, fdmax, newfd, nbytes, i;
	fd_set read_fds;
	char remoteIP[INET6_ADDRSTRLEN];
	char buf[256];

	listener = get_fd_listener(data_config.puerto_cpu);

	FD_SET(listener, &fd_CPUs);

	fdmax = listener;

	for(;;) {
		read_fds = fd_CPUs;
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) {
				if (i == listener) {
					newfd = sock_accept_new_connection(listener, &fdmax, &fd_CPUs);
				} else {
					memset(buf, 0, 256*sizeof(char));	//limpiamos el buffer
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {

						if (nbytes == 0) {
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						close(i); // bye!
						FD_CLR(i, &fd_CPUs); // remove from master set
					} else {
						printf("Se recibio: %s\n", buf);
						memset(buf, 0, 256*sizeof(char));
					}
				}
			}
		}
	}
}


int sock_accept_new_connection(int listener, int *fdmax, fd_set *master){
	int newfd, addrlen;
	struct sockaddr_in direcServ;
	char remoteIP[INET6_ADDRSTRLEN];

	// manejamos las conexiones
	addrlen = sizeof direcServ;
	newfd = accept(listener,(struct sockaddr *)&direcServ,&addrlen);
	if (newfd == -1) {
		perror("accept");
			} else {
				FD_SET(newfd, master);
				if (newfd > *fdmax) {
				*fdmax = newfd;
					}
			printf("selectserver: new connection from %s on ""socket %d\n",inet_ntop(direcServ.sin_family,get_in_addr((struct sockaddr*)&direcServ),remoteIP, INET6_ADDRSTRLEN),newfd);
			}
	return newfd;
}

//Recibe como parametros la ip y el puerto de la conexion y crea el socket que escuchara las nuevas conexiones.
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
		if (listener < 0) {
			continue;
				}
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

//Pasas la ip y el puerto para la conexion y devuelve el fd del servidor correspondiente
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

int main(int argc, char** argv) {

	if(argc == 0){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}
	//Variables para config
	t_config *config_file;
	char *path, *ruta, *nombre_archivo;
	int i=0, k=0, y=0;

	//Variables para conexiones con servidores
	char buf[256];
	int sockfd_memoria, sockfd_fs;	//File descriptors de los sockets correspondientes a la memoria y al filesystem

	//variiables para conexiones con clientes
	int listener, fdmax, newfd, addrlen, nbytes;
	fd_set read_fds;
	char remoteIP[INET6_ADDRSTRLEN];

	FD_ZERO(&fd_programas);
	FD_ZERO(&fd_CPUs);
	FD_ZERO(&read_fds);

	struct sockaddr_in direcServ;

	ruta = argv[1];
	nombre_archivo = argv[2];
	path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
	strcpy(path, "../../");
	strcat(path,ruta);
	strcat(path, nombre_archivo);

	config_file = config_create(path);

	//Configuro al kernel
	data_config.puerto_prog = config_get_string_value(config_file, "PUERTO_PROG");
	data_config.puerto_cpu = config_get_string_value(config_file, "PUERTO_CPU");
	data_config.ip_memoria = config_get_string_value(config_file,"IP_MEMORIA");
	data_config.puerto_memoria = config_get_string_value(config_file, "PUERTO_MEMORIA");
	data_config.ip_fs = config_get_string_value(config_file,"IP_FS");
	data_config.puerto_fs = config_get_string_value(config_file, "PUERTO_FS");
	data_config.quantum = config_get_int_value(config_file, "QUANTUM");
	data_config.quantum_sleep = config_get_int_value(config_file, "QUANTUM_SLEEP");
	data_config.algoritmo = config_get_string_value(config_file,"ALGORITMO");
	data_config.grado_multiprog = config_get_int_value(config_file, "GRADO_MULTIPROG");
	data_config.sem_ids = config_get_array_value(config_file, "SEM_IDS");
	data_config.sem_init = (int*) config_get_array_value(config_file, "SEM_INIT");
	//Sino hago el atoi me los toma como strings por alguna razon

	while(data_config.sem_init[y]!=NULL)
	{
		data_config.sem_init[y] = atoi(data_config.sem_init[y]);
		y++;
	}

	data_config.shared_vars = config_get_array_value(config_file, "SHARED_VARS");
	data_config.stack_size = config_get_int_value(config_file, "STACK_SIZE");

	//Imprimo los datos
	printf("Puerto Programas: %s\n", data_config.puerto_prog);
	printf("Puerto CPUs: %s\n", data_config.puerto_cpu);
	printf("IP Memoria: %s\n", data_config.ip_memoria);
	printf("Puerto Memoria: %s\n", data_config.puerto_memoria);
	printf("IP FS: %s\n", data_config.ip_fs);
	printf("Puerto FS: %s\n", data_config.puerto_fs);
	printf("Quantum: %i\n", data_config.quantum);
	printf("Quantum Sleep: %i\n", data_config.quantum_sleep);
	printf("Algoritmo: %s\n", data_config.algoritmo);
	printf("Grado Multiprog: %i\n", data_config.grado_multiprog);
	while(data_config.sem_ids[i]!=NULL){
		printf("Semaforo %s = %i\n",data_config.sem_ids[i],data_config.sem_init[i]);
		i++;
	}
	while(data_config.shared_vars[k]!=NULL){
		printf("Variable Global %i: %s\n",k,data_config.shared_vars[k]);
		k++;
	}
	printf("Tamaño del Stack: %i\n", data_config.stack_size);

	//********************************Conexiones***************************************//
	//Servidores

	sockfd_memoria = get_fd_server(data_config.ip_memoria,data_config.puerto_memoria);		//Nos conectamos a la memoria
	//sockfd_fs= get_fd_server(data_config.ip_fs,data_config.puerto_fs);		//Nos conectamos al fs

	memset(buf, 0, 256*sizeof(char));	//limpiamos nuestro buffer
	fgets(buf, 256*sizeof(char), stdin);	//Ingresamos nuestro mensaje
	send(sockfd_memoria, buf, strlen(buf),0);	//Le mandamos a la memoria
	//send(sockfd_fs, buf, strlen(buf),0);	//Le mandamos al filesystem

	//CPUs
	pthread_t hiloProgramas;
	int valorHilo = -1;

	valorHilo = pthread_create(&hiloProgramas, NULL, hilo_CPUs, NULL);

	if(valorHilo != 0){
			printf("Error al crear el hilo receptor");
		}

	//Consolas
	listener = get_fd_listener(data_config.puerto_prog);

	FD_SET(listener, &fd_programas);

	fdmax = listener;

	for(;;) {
		read_fds = fd_programas;
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) {
				if (i == listener) {
					newfd = sock_accept_new_connection(listener, &fdmax, &fd_programas);
				} else {
					memset(buf, 0, 256*sizeof(char));	//limpiamos el buffer
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {

						if (nbytes == 0) {
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						close(i); // bye!
						FD_CLR(i, &fd_programas); // remove from master set
					} else {
						printf("Se recibio: %s\n", buf);
						memset(buf, 0, 256*sizeof(char));
					}
				}
			}
		}
	}

	close(sockfd_memoria);
	config_destroy(config_file);
	free(path);
	return EXIT_SUCCESS;
}
