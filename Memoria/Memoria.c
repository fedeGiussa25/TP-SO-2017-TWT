#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commons/config.h>


typedef struct{
	int puerto;
	int marcos;
	int marco_size;
	int entradas_cache;
	int cache_x_proceso;
	char *reemplazo_cache;
	int retardo_memoria;
} mem_config;


int main(int argc, char** argv){

	if(argc == 0){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}

	t_config *config;
	mem_config data_config;
	char *path, *ruta, *nombre_archivo;

	ruta = argv[1];
	nombre_archivo = argv[2];

	path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
	strcpy(path, "../../");
	strcat(path,ruta);
	strcat(path, nombre_archivo);

	char *key1 = "PUERTO";
	char *key2 = "MARCOS";
	char *key3 = "MARCO_SIZE";
	char *key4 = "ENTRADAS_CACHE";
	char *key5 = "CACHE_X_PROC";
	char *key6 = "REEMPLAZO_CACHE";
	char *key7 = "RETARDO_MEMORIA";

	config = config_create(path);

	data_config.puerto = config_get_int_value(config, key1);
	data_config.marcos = config_get_int_value(config, key2);
	data_config.marco_size = config_get_int_value(config, key3);
	data_config.entradas_cache = config_get_int_value(config, key4);
	data_config.cache_x_proceso = config_get_int_value(config, key5);
	data_config.reemplazo_cache = config_get_string_value(config, key6);
	data_config.retardo_memoria = config_get_int_value(config, key7);

	printf("PORT = %d\n", data_config.puerto);
	printf("MARCOS = %d\n", data_config.marcos);
	printf("MARCO_SIZE = %d\n", data_config.marco_size);
	printf("ENTRADAS_CACHE = %d\n", data_config.entradas_cache);
	printf("CACHE_X_PROCESO = %d\n", data_config.cache_x_proceso);
	printf("REEMPLAZO_CACHE = %s\n", data_config.reemplazo_cache);
	printf("RETARDO_MEMORIA = %d\n", data_config.retardo_memoria);

	config_destroy(config);
	free(path);
	return 0;
}
