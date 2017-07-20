#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <string.h>
#include <commons/collections/queue.h>

struct sockaddr_in initAddr(in_addr_t address,int32_t puerto){
	struct sockaddr_in my_addr;
	my_addr.sin_family = AF_INET;
	// Ordenación de máquina
	my_addr.sin_port = htons(puerto);
	// short, Ordenación de la red
	my_addr.sin_addr.s_addr = address;
	if(my_addr.sin_addr.s_addr == -1){
		perror("BroadcastError\n");
		exit(1);
	}
	memset(&(my_addr.sin_zero), '\0', 8);
	return my_addr;
}

int32_t enviar(int32_t socketd, void *buf,int32_t bytestoSend){
	int32_t numbytes =0;
    int32_t tempSend;
    while (bytestoSend - numbytes >0){
        if (tempSend = send(socketd, buf+numbytes, bytestoSend-numbytes, 0) < 0){
            return -1;
        }
        numbytes+=tempSend;
        if(tempSend == 0)
            return 0;
    }
	return numbytes;
}

int32_t recibir(int32_t socketd, void *buf,int32_t bytestoRecv){
	int32_t numbytes =recv(socketd, buf, bytestoRecv,MSG_WAITALL);
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
int32_t initSocket(void){
	int32_t sockfd;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);// ¡Comprueba que no hay errores!
	if(sockfd ==-1 ){
		perror("Socket:");
		exit(1);
	}
	return sockfd;
}
void setsocket(int32_t sockfd,int32_t *num){
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, num,sizeof(*num)) == -1) {
		perror("setsockopt");
		exit(1);
	}
}
void bindeo(int32_t sockfd,struct sockaddr_in *hostServer ){
	if (bind(sockfd, (struct sockaddr *)hostServer, sizeof(*hostServer)) == -1) {
		perror("bind");
		exit(1);
	}
}

void makeConnection(int32_t sockfd,struct sockaddr_in *hostServer,int32_t handshake){
	if (connect(sockfd, (struct sockaddr *)hostServer,sizeof(struct sockaddr)) == -1) {
		perror("connect");
		exit(1);
	}
	int32_t numbytes;
	int32_t aux = handshake;
	if(numbytes= enviar(sockfd,&aux,sizeof(aux)) <0 ){
		perror("connect -- 2");
		exit(1);
	}
	int32_t var;
	printf("envio HANDSHAKE\n");
	if(numbytes = recibir(sockfd,&var,sizeof(var)) <=0){
		if(var!=1){
			printf("FALLA HANDSHAKE DISCONECT AND END\n");
			exit(1);
		}
	}
}

void escuchando(int32_t sockfd,int32_t cantConnections)
{
	if (listen(sockfd, cantConnections) == -1) {
		perror("listen");
		exit(1);
	}
}

void selecteando(int32_t fdmax, fd_set *read_fds){

	if (select(fdmax+1, read_fds, NULL, NULL, NULL) == -1) {
		perror("select");
		exit(1);
	}
}

int32_t servidor(int32_t puerto,int32_t cantidadConexiones){
	int32_t socketServer = initSocket();
	int32_t yes=1;
	setsocket(socketServer,&yes);
	struct sockaddr_in direccion;
	direccion = initAddr(INADDR_ANY,puerto);
	bindeo(socketServer,&direccion);
	escuchando(socketServer,cantidadConexiones);
	return socketServer;
}

void cliente(char *ip,int32_t puerto,int32_t handshake){
	int32_t sockCliente = initSocket();
	struct sockaddr_in direccion = initAddr(inet_addr(ip),puerto);
	makeConnection(sockCliente,&direccion,handshake);
}

int32_t verificarPaquete(int32_t sockCliente,int32_t handshake){
	int32_t recibido;
	int32_t bytes = recibir(sockCliente,&recibido,sizeof(recibido));
	if( bytes >0 && recibido==handshake){
		return 0;
	}else{
		perror("FALLA -- NO SE ASOCIA -- ");
		return -1;
	}
}
int32_t aceptarCliente(int32_t sockfd, int32_t handshake){
	struct sockaddr_in remoteaddr;
	int32_t newfd;
	int32_t addrlen = sizeof(remoteaddr);

	if ((newfd = accept(sockfd, (struct sockaddr*)&remoteaddr,&addrlen)) == -1)
	{
		perror("accept");
		return -1;
	} else {
		printf("aceptando A.. %s\n",inet_ntoa(remoteaddr.sin_addr));
		if(!verificarPaquete(newfd,handshake)){
			printf("selectserver: new connection from %s on socket %d\n", inet_ntoa(remoteaddr.sin_addr),newfd);
			int32_t var = 1;
			enviar(newfd,&var,sizeof(var));
			return newfd;
		}
		else{
			perror("HandShake FAIL");
			close(newfd);
		}
		return -1;
	}
	
}

