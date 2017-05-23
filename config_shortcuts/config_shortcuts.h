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
		uint32_t marcos;
		uint32_t marco_size;
		uint32_t entradas_cache;
		uint32_t cache_x_proceso;
		char *reemplazo_cache;
		uint32_t retardo_memoria;
	} mem_config;

	typedef struct {
		char* puerto_prog;
		char* puerto_cpu;
		char* ip_memoria;
		char* puerto_memoria;
		char* ip_fs;
		char* puerto_fs;
		uint32_t quantum;
		uint32_t quantum_sleep;
		char* algoritmo;
		uint32_t grado_multiprog;
		char** sem_ids;
		uint32_t* sem_init;
		char** shared_vars;
		uint32_t stack_size;
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
