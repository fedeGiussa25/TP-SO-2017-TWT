#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT "9034"

#define MAXDATASIZE 256

//IMPORTANTE: Para poder compilar el Cliente, primero van a tener que hacer lo siguiente. Hecen click derecho en
//la carpeta del proyecto del cliente y van a properties. Una vez ahi, abren la parte que dice C/C++ General
//y ahi se meten en donde dice Paths and Symbols. Van a la pestaña que dice Libraries y ponen Add
//y ahi escriben pthread, le dan aceptar y listo, ya pueden compilar esto.

int sockfd;

void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//Este hilo se va a encargar pura y exclusivamente de mostrar los mensajes enviados por el resto de los clientes conectados
void* thread_Receptor(){
	char buf[MAXDATASIZE];//buffer de mensajes
	int sock_estado;
	for(;;){
		memset(buf, 0, 256*sizeof(char));	//limpiamos el buffer
		if((sock_estado = recv(sockfd, buf, sizeof buf, 0)) <= 0){	//si el recv retorna 0, se desconecto, y si retorna <0, hubo un error
			if (sock_estado == 0) {
				// connection closed
				printf("selectserver: socket %d hung up\n", sockfd);
					} else {
						perror("recv");
						}
				close(sockfd);
		}else{
			printf("Otro usuario escribio: %s", buf);//imprimimos mensajes
		}
	}
}

int main(int argc, char** argv){
	char buf[MAXDATASIZE];
	memset(buf, 0, 256*sizeof(char));
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
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


	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),s, sizeof s);

	printf("client: connecting to %s\n", s);
	freeaddrinfo(servinfo); // all done with this shit

	//Creamos el hilo receptor
	pthread_t hiloReceptor;	//Esta variable representa el hilo que vamos a crear a continuacion
	int valorHilo = -1;

	valorHilo = pthread_create(&hiloReceptor, NULL, thread_Receptor, NULL);	//creamos el hilo, los parametros en NULL no son importantes

	if(valorHilo != 0){
		printf("Error al crear el hilo receptor");
	}

	printf("Ya podes tipear: \n");

	for(;;){
		memset(buf, 0, 256*sizeof(char));	//limpiamos nuestro buffer
		fgets(buf, 256*sizeof(char), stdin);	//Ingresamos nuestro mensaje
		send(sockfd, buf, strlen(buf),0);	//Lo mandamos :D
	}
/*
	buf[numbytes] = '\0';
	printf("client: received '%s'\n",buf);*/
	close(sockfd);	//cerramos la conexion
	return 0;
}
