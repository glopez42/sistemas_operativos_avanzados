/*
 *  minikernel/include/kernel.h
 *
 *  Minikernel. Versi�n 1.0
 *
 *  Fernando P�rez Costoya
 *
 */

/*
 *
 * Fichero de cabecera que contiene definiciones usadas por kernel.c
 *
 *      SE DEBE MODIFICAR PARA INCLUIR NUEVA FUNCIONALIDAD
 *
 */

#ifndef _KERNEL_H
#define _KERNEL_H

#include "const.h"
#include "HAL.h"
#include "llamsis.h"

/*
 *
 * Definicion del tipo que corresponde con el BCP.
 * Se va a modificar al incluir la funcionalidad pedida.
 *
 */
typedef struct BCP_t *BCPptr;

typedef struct BCP_t
{
	int id;					  /* ident. del proceso */
	int estado;				  /* TERMINADO|LISTO|EJECUCION|BLOQUEADO*/
	contexto_t contexto_regs; /* copia de regs. de UCP */
	void *pila;				  /* dir. inicial de la pila */
	BCPptr siguiente;		  /* puntero a otro BCP */
	void *info_mem;			  /* descriptor del mapa de memoria */
	int ticks_bloq;		  	  /* ticks que le quedan para desbloquearse en el caso de que lo este*/
	int int_usuario;		  /* veces que ha habido interrupcion de reloj en modo usuario*/
	int int_sistema;          /* veces que ha habido interrupcion de reloj en modo sistema*/
} BCP;

/*
 *
 * Definicion del tipo que corresponde con la cabecera de una lista
 * de BCPs. Este tipo se puede usar para diversas listas (procesos listos,
 * procesos bloqueados en sem�foro, etc.).
 *
 */

typedef struct
{
	BCP *primero;
	BCP *ultimo;
} lista_BCPs;

/*
 * Variable global que identifica el proceso actual
 */

BCP *p_proc_actual = NULL;

/*
 * Variable global que representa la tabla de procesos
 */

BCP tabla_procs[MAX_PROC];

/*
 * Variable global que representa la cola de procesos listos
 */
lista_BCPs lista_listos = {NULL, NULL};

/*
 * Variable global que representa la cola de procesos bloqueados
 */
lista_BCPs lista_bloq = {NULL, NULL};

/*
 * Variable global que guarda el numero de interrupciones de reloj totales
*/

long num_ints = 0; 

/**
 *  Variable global que registra si se esta accediendo a la zona de usuario
*/

int acceso_parametro = 0;

/*
 *
 * Definici�n del tipo que corresponde con una entrada en la tabla de
 * llamadas al sistema.
 *
 */
typedef struct
{
	int (*fservicio)();
} servicio;


/**
 *  Struct para guardar el numero de veces que se ha interrumpido un proceso en modo sistema o usuario
*/
typedef struct tiempos_ejec_t {
    int usuario;
    int sistema;
} tiempos_ejec;

/*
 * Prototipos de las rutinas que realizan cada llamada al sistema
 */
int sis_crear_proceso();
int sis_terminar_proceso();
int sis_escribir();
int sis_obtener_id_pr();
int sis_dormir();
int sis_tiempos_proceso();

/*
 * Variable global que contiene las rutinas que realizan cada llamada
 */
servicio tabla_servicios[NSERVICIOS] = {
	{sis_crear_proceso},
	{sis_terminar_proceso},
	{sis_escribir},
	{sis_obtener_id_pr},
	{sis_dormir},
	{sis_tiempos_proceso}};

#endif /* _KERNEL_H */
