#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commons/config.h>

typedef struct{
	int puerto;
	char *montaje;
}fs_config;

int main(int argc, char** argv){

	if(argc == 0){
		printf("Debe ingresar ruta de .config y archivo\n");
		exit(1);
	}

	t_config *config;
	fs_config data_config;
	char *montaje, *path, *ruta, *nombre_archivo;
	int puerto;

	ruta = argv[1];		//Guardamos el primer parametro que es la ruta del archivo
	nombre_archivo = argv[2];		//Guardamos el segundo parametro que es el nombre del archivo

	//Formamos la path del config
	path = malloc(strlen("../../")+strlen(ruta)+strlen(nombre_archivo)+1);
	strcpy(path, "../../");
	strcat(path,ruta);
	strcat(path, nombre_archivo);

	char *key1 = "PUERTO";
	char *key2 = "PUNTO_MONTAJE";

	config = config_create(path);		//Creamos el t_config

	//Leemos los datos
	data_config.puerto = config_get_int_value(config, key1);
	data_config.montaje = config_get_string_value(config, key2);

	printf("PORT = %d\n", data_config.puerto);
	printf("Montaje = %s\n", data_config.montaje);

	config_destroy(config);		//Eliminamos fs_config, linberamos la memoria que utiliza
	free(path);		//liberamos lo que alocamos previamente
	return 0;
}
