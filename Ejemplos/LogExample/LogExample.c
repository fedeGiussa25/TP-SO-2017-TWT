#include <commons/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main()
{
	//Lo que esta al final del log_create, segun lo que dice el log.h, es el nivel de detalle del mensaje (sigo sin saber que es eso)

	//Los log_xxx lo unico que cambian es lo que aparece al principio del mensaje en el log -->
	// --> [xxx] fecha - proceso - mensaje  (asi aparecen las cosas en el log)

	//No es obligatorio que ponga log_error() con el log creado con ese nivel de detalle, se pueden intercambiar

	//Otra cosa es que lo log_xxx le aplican color al mensaje que imprimen por pantalla (menos en info)

	t_log *errorLog = log_create("../Errors.log", "LogExample", false, LOG_LEVEL_ERROR);
	log_error(errorLog, "This is an error message\n");

	t_log *infoLog = log_create("../Info.log", "LogExample", false, LOG_LEVEL_INFO);
	log_info(infoLog, "This is an information message\n");
	log_error(infoLog, "This is an error message\n");

	t_log *traceLog = log_create("../Trace.log", "LogExample", false, LOG_LEVEL_TRACE);
	log_trace(traceLog, "This is a trace message\n");

	t_log *debugLog = log_create("../Debug.log", "LogExample", false, LOG_LEVEL_DEBUG);
	log_debug(debugLog, "This is a debug message\n");

	t_log *warningLog = log_create("../Warning.log", "LogExample", false, LOG_LEVEL_WARNING);
	log_warning(warningLog, "This is a warning message\n");

	//Log que muestra por pantalla

	t_log *printLog = log_create("../Print.log", "LogExample", true, LOG_LEVEL_INFO);
	log_info(printLog, "This is a message\n");

	printf("Press return to exit...");
	getchar();
	return 1;
}
