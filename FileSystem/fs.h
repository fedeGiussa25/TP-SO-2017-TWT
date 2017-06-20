uint32_t sizeFile(char *nombreArchivo,char *rutaBase){
	FILE *archivo;
	char *fullPath = malloc(strlen(nombreArchivo) + strlen(rutaBase));
	strcpy(fullPath,rutaBase);
	strcat(fullPath,nombreArchivo);
	archivo = fopen(fullPath,"r");
	if(!archivo){
		//Agrego el free para evitar memory leaks
		free(fullPath);
		return -1;
	}
	fseek(archivo,0,SEEK_END);
	uint32_t dimension = ftell(archivo);
	fclose(archivo);
	free(fullPath);
	return dimension;
}

uint32_t exist(char *nombreArchivo,char *rutaBase){
	FILE *archivo;
	char *fullPath = malloc(strlen(nombreArchivo) + strlen(rutaBase));
	strcpy(fullPath,rutaBase);
	strcat(fullPath,nombreArchivo);
	archivo = fopen(fullPath,"r");
	uint32_t var = true;
	if(!archivo){
		//Antes tratabas de cerrar el archivo aunque no estuviese abierto y rompia
		free(fullPath);
		return var = false;
	}
	fclose(archivo);
	free(fullPath);
	return var;
}

char* obtieneNombreArchivo(uint32_t socketReceiver){
	uint32_t size_name;
	char *nombre;
	recibir(socketReceiver,(void *)&size_name,sizeof(uint32_t));
	nombre = malloc(size_name);
	recibir(socketReceiver,(void *)nombre,size_name);
	return nombre;
}

char *unir_str(char* str1,char* str2){
	char *retorno = malloc(strlen(str1) + strlen(str2) + 1);
	strcpy(retorno,str1);
	strcat(retorno,str2);
	return retorno;
}
char *path_sin_bar(char* path){
	int32_t dimension = strlen(path);
	char *retorno = malloc(dimension);

	if(path[dimension -1] != '/'){
		strcpy(retorno,path);
		return retorno;
	}
	strncpy(retorno,path,dimension-1);
	return retorno;
}

int32_t find_or_create(char *montaje,char* nameDir){
	DIR *mount = opendir(montaje);
	if(!mount)
		return -3;
	int32_t dir = 1;
	struct dirent *aux;
	char *justifiquedPath = path_sin_bar(nameDir);
	while((aux=readdir(mount))!=NULL){
		if(aux->d_type == 4 && !strcmp(aux->d_name,justifiquedPath))
			dir = 0;
	}
	char *fullPath = unir_str(montaje,nameDir);
	if(dir)
		mkdir(fullPath,0777);
	free(fullPath);
	free(justifiquedPath);
	closedir(mount);
	return 1;
}

