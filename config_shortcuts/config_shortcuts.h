/*
 * config_shortcuts.h
 *
 *  Created on: 7/4/2017
 *      Author: utnso
 */

#ifndef CONFIG_SHORTCUTS_H_
#define CONFIG_SHORTCUTS_H_

	#include <stdio.h>
	#include <stdlib.h>
	#include <commons/config.h>

	typedef struct {
		char* ip_kernel;
		char* puerto_kernel;
		char* ip_memoria;
		char* puerto_memoria;
	} cpu_config;

	typedef struct {
		char *puerto;
		int marcos;
		int marco_size;
		int entradas_cache;
		int cache_x_proceso;
		char *reemplazo_cache;
		int retardo_memoria;
	} mem_config;

	typedef struct {
		char* puerto_prog;
		char* puerto_cpu;
		char* ip_memoria;
		char* puerto_memoria;
		char* ip_fs;
		char* puerto_fs;
		int quantum;
		int quantum_sleep;
		char* algoritmo;
		int grado_multiprog;
		char** sem_ids;
		int* sem_init;
		char** shared_vars;
		int stack_size;
	} kernel_config;

	typedef struct {
		char* puerto;
		char* montaje;
	} fs_config;

	typedef struct {
		char* ip_kernel;
		char* puerto_kernel;
	} consola_config;

	t_config* config_create_from_relative_with_check(char **argv, char *cfgPath);
	void checkArguments(int argc);


#endif /* CONFIG_SHORTCUTS_H_ */
