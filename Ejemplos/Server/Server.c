#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define PORT "9034"
#define MAX_DATA_PACKAGE 256

fd_set master;

void *get_in_addr(struct sockaddr *sa)
{
if (sa->sa_family == AF_INET) {
return &(((struct sockaddr_in*)sa)->sin_addr);
}
return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int main(int argc, char** argv){
	fd_set read_fds; // conjunto temporal de descriptores de fichero para select()
	// conjunto maestro de descriptores de fichero

	int fdmax;        // número máximo de descriptores de fichero
	int listener;

	FD_ZERO(&master);    // borra los conjuntos maestro y temporal
	FD_ZERO(&read_fds);

	char buf[MAX_DATA_PACKAGE]; // buffer for client data
	memset(buf, 0, 256*sizeof(char));
	int nbytes;

	int yes=1;
	int newfd; // nuevo socket luego del acept()
	struct sockaddr_in direcServ; // server address
	struct addrinfo hints, *ai, *p;
	socklen_t addrlen;
	int i, j, rv;

	char remoteIP[INET6_ADDRSTRLEN];

	//configuramos el tipo de socket
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	//getaddrinfo() - cargamos la config de los sockets
	if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
	fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
	exit(1);
	}

	//Socket() - obtenemos el file descriptor del socket()
	for(p = ai; p != NULL; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
			}
		// Bla,bla, bla para ignorar la boludez de socket en uso
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		//Hacemos el bendito bind() - socket() listo para escuchar
		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
			}
		break;
		}

	//Si p es null significa que fracasamos en el bind
	if (p == NULL) {
	fprintf(stderr, "selectserver: failed to bind\n");
	exit(2);
	}

	freeaddrinfo(ai); // all done with this shit

	// listen - esperamos nuevas conexiones
	if (listen(listener, 10) == -1) {
	perror("listen");
	exit(3);
	}

	// agregamos el listener al set de FDs
	FD_SET(listener, &master);

	fdmax = listener;

	for(;;) {
		read_fds = master; // copy it
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) { // we got one!!
				if (i == listener) {
					// manejamos las conexiones
					addrlen = sizeof direcServ;
					newfd = accept(listener,(struct sockaddr *)&direcServ,&addrlen);
					if (newfd == -1) {
						perror("accept");
					} else {
						FD_SET(newfd, &master); // add to master set
						if (newfd > fdmax) { // keep track of the max
							fdmax = newfd;
						}
					printf("selectserver: new connection from %s on ""socket %d\n",inet_ntop(direcServ.sin_family,get_in_addr((struct sockaddr*)&direcServ),remoteIP, INET6_ADDRSTRLEN),newfd);
					}
				} else {
					// handle data from a client
					memset(buf, 0, 256*sizeof(char));	//limpiamos el buffer
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						close(i); // bye!
						FD_CLR(i, &master); // remove from master set
					} else {
						//printf("Cliente %d dice: %s\n", i, buf);
						for(j = 0; j <= fdmax; j++) {
							// send to everyone!
							if (FD_ISSET(j, &master)) {
								// except the listener and ourselves
								if (j != listener && j != i) {
									if (send(j, buf, nbytes, 0) == -1) {
										perror("send");
									}
								}
							}
						}
						memset(buf, 0, 256*sizeof(char));
					}
				} // END handle data from client
			} // END got new incoming connection
		} // END looping through file descriptors
	} // END for(;;)--and you thought it would never end!
return 0;
}
