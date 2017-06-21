#include <dirent.h>
#include <sys/types.h>
#include <commons/bitarray.h>
typedef struct{
	int32_t tamanio;
	int32_t *array;
	uint32_t cantidadElementos; //es posible sacarlo con un algoritmo pero no me jode ya tenerlo
}Archivo_t;


uint32_t sizeFile(char *nombreArchivo,char *rutaBase){
	FILE *archivo;
	char *fullPath= 
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
	int32_t retorno = 1;
	if(!mount)
		return 3;
	int32_t dir = 1;
	struct dirent *aux;
	char *justifiquedPath = path_sin_bar(nameDir);
	while((aux=readdir(mount))!=NULL){
		if(aux->d_type == 4 && !strcmp(aux->d_name,justifiquedPath))
			dir = 0;
	}
	char *fullPath = unir_str(montaje,nameDir);
	if(dir){
		mkdir(fullPath,0777);
		retorno = 0;
	}
	free(fullPath);
	free(justifiquedPath);
	closedir(mount);
	return retorno;
}

void print_bit_array_x_byte(t_bitarray *byte){
	int i,j;
	for(i=0;i<byte->size ;i++){
		printf("byte %d ->",i );
		for(j=0;j<8;j++){
			printf("%d",bitarray_test_bit(byte,j + 8*i) );
		}
		printf("\n" );
	}
	printf("\n");
}

int32_t create_binFile(char* montaje,char* name,uint32_t sizeInBytes){
	char *fullName = unir_str(montaje,name);
	char *path = unir_str(fullName,".bin");
	free(fullName);
	FILE *blk = fopen(path,"wb");
	if(blk == NULL)
		return 2;
	int i = 0;
	for(i=0;i<sizeInBytes;i++){
		char aux = 0;
		fwrite(&aux,sizeof(aux),1,blk);
	}
	fclose(blk);
	free(path);
	return 0;
}

int32_t create_block(char *montaje,char* name,int32_t tamanio){
	return create_binFile(montaje,name,tamanio);
}

int32_t primer_bloque_libre(t_bitarray *miBit){
	int i = 0;
	for (i = 0; i <bitarray_get_max_bit(miBit); i++)
	{
		if(bitarray_test_bit(miBit,i) == 0)
			return i;
	}
	return -1;
}

int potencia(int nro,int potencia){
	if(nro == 0)
		return 0;
	
	int i;
	int aux = 1;
	for( i =0;i<potencia;i++)
		aux*=nro;
	return aux;
}	

Archivo_t *get_data_Archivo(char *path){
	t_config *config;
	
	config = config_create(path);
	Archivo_t *aux = malloc(sizeof(Archivo_t));

	char** bloques=config_get_array_value(config,"BLOQUES");
	aux->tamanio=config_get_int_value(config,"TAMANIO");
	aux->array = malloc(sizeof(int32_t)); 
	int i;
	for (i = 0;bloques[i] != NULL; ++i)
	{
		aux->array = realloc(aux->array,sizeof(int32_t)*(i+1));
		aux->array[i] = atoi(bloques[i]);
	}
	if(i==0){
		config_destroy(config);
		return NULL;
	}
	aux->cantidadElementos = i;
	config_destroy(config);
	return aux;
}
char *int_to_str(int32_t number){
	int flag = 0;
	int i;
	for (i = 1; !flag; i++)
	{
		float aux = ((float)number) / ((float)potencia(10,i));
		if(aux<1)
			flag = 1;
	}

	int digitos = i-1;
	char *retorno = malloc(i);
	int nro = number;
	for (i =0; i < digitos; i++)
	{
		int resto=0;
		if(nro>0){
			resto = nro/potencia(10,digitos-i-1);
		}
			

		retorno[i]=resto + '0';
		nro-=resto * potencia(10,digitos-i-1);
	}
	retorno[digitos] = '\0';
	return retorno;
}



void createDefaultMetadata(char* rutaBase){
	char* path;
	if(rutaBase[strlen(rutaBase)-1]=='/')
		path = unir_str(rutaBase,"Metadata.bin");
	else
		path = unir_str(rutaBase,"/Metadata.bin");

	FILE *archivo = fopen(path,"w");
	fputs("TAMANIO_BLOQUES=64\n",archivo);
	fputs("CANTIDAD_BLOQUES=5192\n",archivo);
	fputs("MAGIC_NUMBER=SADICA\n",archivo);
	fclose(archivo);
	free(path);
}