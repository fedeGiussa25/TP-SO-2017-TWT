#include <commons/log.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

t_log *messagesLog;

typedef struct
{
	int hours;
	int minutes;
	int seconds;
	char *dateTime;
}dateTime;

void printData(dateTime *start, dateTime *end, int printCount, int pid)
{
	int startTimeSeconds = ((start->hours)*3600) + ((start->minutes)*60) + (start->seconds);
	int endTimeSeconds = ((end->hours)*3600) + ((end->minutes)*60) + (end->seconds);
	int elapsedTime = endTimeSeconds - startTimeSeconds;

	printf("Imprimiendo datos de inicio y fin del proceso %d en el log\n", pid);

	log_info(messagesLog, "Informacion sobre el programa %d:", pid);
	log_info(messagesLog, "\n\tLa fecha de inicio del programa es: %s", start->dateTime);
	log_info(messagesLog, "\n\tLa fecha de finalizacion del programa es: %s", end->dateTime);
	log_info(messagesLog, "\n\tLa cantidad de impresiones por pantalla es: %d", printCount);
	log_info(messagesLog, "\n\tEl tiempo total de ejecucion del programa en segundos es: %d\n", elapsedTime);

	free(start->dateTime);
	free(end->dateTime);
	free(start);
	free(end);
}

int main()
{
	int printCount = 2;
	int pid = 1;

	messagesLog = log_create("../Tiempou.log", "Tiempou", false, LOG_LEVEL_INFO);

	struct tm *local1, *local2;
	time_t t1, t2;
	dateTime *start, *end;

	printf("Arrancando programa\n");

	t1 = time(NULL);
	local1 = localtime(&t1);
	start = (dateTime*) malloc(sizeof(dateTime));
	char *date1 = malloc(35);
	sprintf(date1, "%d/%d/%d  %d:%d:%d", local1->tm_mday, local1->tm_mon + 1, local1->tm_year + 1900, local1->tm_hour, local1->tm_min, local1->tm_sec);
	start->hours = local1->tm_hour;
	start->minutes = local1->tm_min;
	start->seconds = local1->tm_sec;
	start->dateTime = date1;

	printf("Me voy a dormir 10 segundos\n");

	sleep(10);

	t2 = time(NULL);
	local2 = localtime(&t2);
	end = (dateTime*) malloc(sizeof(dateTime));
	char *date2 = malloc(35);
	sprintf(date2, "%d/%d/%d  %d:%d:%d", local2->tm_mday, local2->tm_mon + 1, local2->tm_year + 1900, local2->tm_hour, local2->tm_min, local2->tm_sec);
	end->hours = local2->tm_hour;
	end->minutes = local2->tm_min;
	end->seconds = local2->tm_sec;
	end->dateTime = date2;

	printData(start, end, printCount, pid);
	printf("Presione enter para salir...");
	getchar();
	return 1;
}

