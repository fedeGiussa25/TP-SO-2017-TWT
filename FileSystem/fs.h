#include <dirent.h>
#include <sys/types.h>
#include <commons/bitarray.h>
#include <commons/log.h>
#define PATH_BLQ "Bloques/"
#define FNF 20 // FILE NOT FOUND
typedef struct{
	int32_t tamanio;
	char** array;
	uint32_t cantidadElementos; //es posible sacarlo con un algoritmo pero no me jode ya tenerlo
}Archivo_t;

typedef struct {
    int32_t id;
    int32_t offset;
    int32_t size;
}Bloque_t;

int32_t bloque_offset(int32_t offset,int32_t sizeBloque){
    int32_t i;
    for (i = 0; sizeBloque *i <= offset;i++);
    return i;
}

Bloque_t *blocks_to_process(int32_t sizeBloque,int32_t offset,int32_t sizeOp){
    int32_t readed = 0,counter = 0;
    Bloque_t *returned = NULL;
    while(readed < sizeOp){
        if(readed == 0)
            returned = malloc(sizeof(Bloque_t));
        else
            returned = realloc(returned,sizeof(Bloque_t)*(counter+1));
        int32_t data = bloque_offset(offset+readed,sizeBloque);
        int32_t proximoLeer;
        if(readed == 0){
            if(sizeOp > sizeBloque)
                proximoLeer=(offset-(data-1)*sizeBloque);
            else
                proximoLeer = sizeOp;
        }
        else{
            if(sizeOp-readed >sizeBloque)
                proximoLeer = sizeBloque;
            else
                proximoLeer = sizeOp - readed;
        }
        int32_t actualOffset;
        if(readed ==0){
            actualOffset = proximoLeer;
            proximoLeer = sizeBloque - proximoLeer;
            readed+=proximoLeer;
        }
        else{
            actualOffset = 0;
            readed +=proximoLeer;
        }
        returned[counter].id = data -1;
        returned[counter].offset = actualOffset;
        returned[counter].size = proximoLeer;
        counter++;
    }
    returned = realloc(returned,sizeof(Bloque_t)*(counter+1));
    returned[counter].id = -1;
    return returned;
}

typedef struct {
    char* puerto;
    char* montaje;
} fs_config;

char *unir_str(char* str1,char* str2){
    char *retorno = malloc(strlen(str1) + strlen(str2) + 1);
    strcpy(retorno,str1);
    strcat(retorno,str2);
    return retorno;
}

void checkArguments(int argc){
    if(argc == 1)
    {
        printf("Debe ingresar el nombre del archivo de configuracion");
        exit(1);
    }

    if(argc != 2)
    {
        printf("Numero incorrecto de argumentos\n");
        exit(1);
    }
}

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
	char *fullPath = unir_str(rutaBase,nombreArchivo);
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
	
	Archivo_t *aux = malloc(sizeof(Archivo_t));
	t_config *config = config_create(path);
	int32_t i;
	char **data =config_get_array_value(config,"BLOQUES");
	aux->tamanio=config_get_int_value(config,"TAMANIO");
	for (i = 0;data[i] != NULL; i++);
	aux->cantidadElementos = i;
	aux->array = malloc(aux->cantidadElementos * sizeof(char*));
	for (i = 0;data[i] != NULL; i++){
		aux->array[i] = malloc(strlen(data[i])+1);
		strcpy(aux->array[i],data[i]);
	}
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

void agregarBloque(Archivo_t *aux,uint32_t bloque,t_log *log){
	char* str_bloque = int_to_str(bloque);
	print_data_archivo(aux,log);
	if(aux->array == NULL){

        aux->array = malloc((aux->cantidadElementos + 1) * sizeof(char*)+1);
    }
	else
		aux->array = realloc(aux->array,(aux->cantidadElementos + 1) * sizeof(char*));
	aux->array[aux->cantidadElementos] =(char*)malloc(strlen(str_bloque)+1);
	strcpy(aux->array[aux->cantidadElementos],str_bloque);
	aux->cantidadElementos+=1; 
	free(str_bloque);
}

char* array_to_write(Archivo_t *aux){
	int32_t tamanioReal=0,i;
	for (i = 0;i<aux->cantidadElementos; i++)
		tamanioReal+=strlen((aux->array[i]));
	char* charArray= (char*)malloc(tamanioReal*2+2);
	strcpy(charArray,"[");
	for (i = 0;i<aux->cantidadElementos; i++){
		strcat(charArray,(aux->array[i]));
		if(i+1!=aux->cantidadElementos)
			strcat(charArray,",");
	}
	strcat(charArray,"]");
	return charArray;
}

int32_t Archivo_guardar(char* path,char* montaje,Archivo_t *aux,t_log *log){
    char *fullPath = unir_str(montaje,path);
    log_info(log,"el Path de todos lo Pathes %s hijo de %s y %s",fullPath,montaje,path);
    FILE *archivo = fopen(fullPath,"r");
    if(archivo == NULL){
        free(fullPath);
        return false;
    }
    fclose(archivo);
    t_config *config = config_create(fullPath);
    free(fullPath);
    if(config == NULL)
    {
        log_error(log,"FALLA CON LA CONFIG");
    }
    char *dataChar = int_to_str(aux->tamanio);
    config_set_value(config,"TAMANIO",dataChar);
    free(dataChar);
    dataChar = array_to_write(aux);
    config_set_value(config,"BLOQUES",dataChar);
    free(dataChar);
    config_save(config);
    config_destroy(config);
    return true;
}

int32_t create_archivo(char* path,char* montaje,t_bitarray *data,t_log *log){
	if(exist(path,montaje)==true)
		return false;
	log_info(log,"Tratando de Crear un Archivo en %s\n",path );
	char* fullPath = unir_str(montaje,path);
	FILE *archivo = fopen(fullPath,"w");
	if(archivo == NULL)
		return false;
	fclose(archivo);
	Archivo_t *aux = malloc(sizeof(Archivo_t));
	aux->tamanio = 0;
	aux->cantidadElementos = 0;
	aux->array = NULL;
	int bloque = primer_bloque_libre(data);
    log_info(log,"El primer bloque libre fue el %d\n",bloque);
	agregarBloque(aux,bloque,log);
	bitarray_set_bit(data,bloque);
	t_config *config = config_create(fullPath);
	config_set_value(config,"TAMANIO", "0");
	char *array = array_to_write(aux);
	config_set_value(config,"BLOQUES",array);

	config_save(config);
	free(array);
	config_destroy(config);

	free(fullPath);
	return true;
}

void print_data_archivo(Archivo_t *aux,t_log *log){
	int i = 0;
	log_info(log,"CANTIDAD ELEMNTOS == %d -- TAMAÑO %d",aux->cantidadElementos,aux->tamanio);
	for(i=0;i<aux->cantidadElementos;i++)
		log_info(log,"elemnto \t%d\t\'%s\'\n",i,aux->array[i] );
}

void kill_archivo(Archivo_t *aux){
	char **array = aux->array;
	int i=0;
	for(i = 0;i<aux->cantidadElementos;i++)
		free(aux->array[i]);
	free(aux->array);
	free(aux);
}

void setBloques(Archivo_t *aux,t_bitarray *data){
    int i;
    for(i=0;i<aux->cantidadElementos;i++){
        int posicion = atoi((aux->array[i]));
        bitarray_set_bit(data,posicion);
    }
}
void cleanBloques(Archivo_t *aux,t_bitarray *data){
    int i;
    for(i=0;i<aux->cantidadElementos;i++){
        int posicion = atoi((aux->array[i]));
        bitarray_clean_bit(data,posicion);
    }
}
int32_t delete_archivo(char* path,char* montaje,t_bitarray *data){
	char* fullPath = unir_str(montaje,path);
    t_config *config = config_create(fullPath);
    if(config == NULL){
        free(fullPath);
        return false;
    }
    if((!config_has_property(config,"BLOQUES") ||  !config_has_property(config,"TAMANIO"))){
        remove(fullPath);
        config_destroy(config);
        free(fullPath);
        return true;
    }
    config_destroy(config);
	Archivo_t *aux = get_data_Archivo(fullPath);
    if(aux == NULL)
        return false;
	int i;
	cleanBloques(aux,data);
	remove(fullPath);
	free(fullPath);
    return true;
}
int32_t validar_archivo(char* path,char* montaje,t_log *log){
	char *fullPath = unir_str(montaje,path);
	int32_t valida=false;;
	
	t_config *config = config_create(fullPath);
	if(config == NULL){
		log_info(log,"%s es un archivo inexistente\n",path );
		free(fullPath);
		return valida;
	}
	if(config_has_property(config,"BLOQUES") && config_has_property(config,"TAMANIO")){
        valida = true;
        log_info(log,"%s es un archivo Valido y completo\n",path );
    }
	config_destroy(config);
	free(fullPath);
	return valida;
}

void* readFromBlock(char* block,char* montajeBlck,int32_t offset,int32_t size){
    char* pathBlock = unir_str(block,".bin");
    char* fullPath = unir_str(montajeBlck,pathBlock);
    free(pathBlock);
    FILE* leer = fopen(fullPath,"rb");
    if(leer == NULL)
        return NULL;
    int i;
    void *data = malloc(size);

    fseek(leer,offset,SEEK_SET);
    for(i=0;i<size;i++)
        fread(data+i,sizeof(char),1,leer);
    fclose(leer);
    free(fullPath);
    return data;
}

int32_t writeToBlock(char* block,char* montajeBlck,int32_t offset,int32_t size,void* buffer,t_log *log){
    char* pathBlock = unir_str(block,".bin");
    char* fullPath = unir_str(montajeBlck,pathBlock);
    free(pathBlock);
    FILE* writer = fopen(fullPath,"rb+");
    if(writer == NULL)
        return NULL;
    int i;
    fseek(writer,offset,SEEK_SET);
    for(i=0;i<size;i++)
        fwrite(buffer+i,sizeof(char),1,writer);
    fclose(writer);
    free(fullPath);
    return true;
}


void* obtener_datos(char* path,char* montaje, int32_t offset,int32_t size,int32_t sizeBloque,char* pathBloques,t_log *log,int8_t *error){
	*error = 0;
    if(validar_archivo(path,montaje,log) == false){
        log_info(log,"ARCHIVO %s NO VALIDO",path);
        *error =-FNF;
		return NULL;
	}
    if(size < 1) {
        *error= -14;
        return NULL;
    }
    if(offset<0) {
        *error=-15;
        return NULL;

    }
    char* fullPath = unir_str(montaje,path);
    Archivo_t *archivo = get_data_Archivo(fullPath);
    if(archivo->tamanio - size-offset >0) {
        kill_archivo(archivo);
        free(fullPath);
        *error = -16;
        return NULL;
    }
    int32_t i;
	Bloque_t *aux = blocks_to_process(sizeBloque,offset,size);
    uint32_t cargado=0;
    void *miDato = malloc(size);
	for (i = 0; aux[i].id != -1; i++)
	{
        void* readAux = readFromBlock(archivo->array[aux[i].id],pathBloques,aux[i].offset,aux[i].size);
        if(readAux == NULL) {
            free(miDato);
            free(aux);
            free(fullPath);
            kill_archivo(archivo);
            *error =-19;
            return NULL;
        }
        memcpy(miDato+cargado,readAux,aux[i].size);
        cargado+=aux[i].size;
        free(readAux);
	}
	free(aux);
    kill_archivo(archivo);
    *error = 1;
	return miDato;
}

int32_t cantidadBloquesLibres(t_bitarray* bitmap){
    int32_t i,cantidad=0;
    if(bitmap==NULL)
        return NULL;
    for(i=0;i<bitarray_get_max_bit(bitmap);i++){
        if(bitarray_test_bit(bitmap,i)==false)
            cantidad++;
    }
    return cantidad;
}

int32_t max(int32_t numA,int32_t numB){
    if(numA >=numB)
        return numA;
    return numB;
}
int32_t guardar_datos(char* path,char* montaje,int32_t offset,int32_t size,void* buffer,t_bitarray* bitmap,int32_t sizeBloque,char* pathBloques,t_log *log){
	if(validar_archivo(path,montaje,log) == false){
        log_info(log,"ARCHIVO %s NO VALIDO",path);
		return FNF;
	}
    if(size < 1)
        return 14;
    if(offset<0)
        return 15;
    char* fullPath = unir_str(montaje,path);
    Archivo_t *archivo = get_data_Archivo(fullPath);
    free(fullPath);
    int32_t i;
    Bloque_t *aux = blocks_to_process(sizeBloque,offset,size);
    for(i=0;aux[i].id != -1;i++);
    int32_t bloquesNecesarios = i - archivo->cantidadElementos;

    if(bloquesNecesarios >0){
        int32_t bloques = cantidadBloquesLibres(bitmap);
        if(bloques < bloquesNecesarios){
            log_info(log,"NO HAY BLOQUES SUFICIENTES BLOQUES libres %d --- Necesarios %d",bloques,bloquesNecesarios);
            return 16;
        }
        for(i=0;i<bloquesNecesarios;i++){
            int ubicacionBit= primer_bloque_libre(bitmap);
            agregarBloque(archivo,ubicacionBit,log);
            bitarray_set_bit(bitmap,ubicacionBit);
        }
    }
    int writed = 0;
    for(i=0;aux[i].id != -1;i++){
        log_info(log,"POR OPERAR CON EL BLOQUE %s, los datos de size %d offset %d ",archivo->array[aux[i].id],aux[i].size,aux[i].offset);
        if(!writeToBlock(archivo->array[aux[i].id],pathBloques,aux[i].offset,aux[i].size,buffer+writed,log)){
            log_error(log,"FALLA AL ABRIR EL BLOQUE -> %s -- de id -> %d",archivo->array[aux[i].id],aux[i].id);
            return false;
        }
        writed+=aux[i].size;
    }
    archivo->tamanio = max(archivo->tamanio,offset + size);
    free(aux);
    log_info(log,"%s == MONTAJE ---- %s == PATH",montaje,path);
    return Archivo_guardar(path,montaje,archivo,log);
}