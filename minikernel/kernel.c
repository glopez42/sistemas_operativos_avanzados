/*
 *  kernel/kernel.c
 *
 *  Minikernel. Version 1.0
 *
 *  Fernando Perez Costoya
 *
 */

/*
 *
 * Fichero que contiene la funcionalidad del sistema operativo
 *
 */

#include "kernel.h" /* Contiene defs. usadas por este modulo */

/*
 *
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
 *
 */

/*
 * Funcion que inicia la tabla de procesos
 */
static void iniciar_tabla_proc()
{
	int i;

	for (i = 0; i < MAX_PROC; i++)
		tabla_procs[i].estado = NO_USADA;
}

/*
 * Funci�n que busca una entrada libre en la tabla de procesos
 */
static int buscar_BCP_libre()
{
	int i;

	for (i = 0; i < MAX_PROC; i++)
		if (tabla_procs[i].estado == NO_USADA)
			return i;
	return -1;
}

/*
 *
 * Funciones que facilitan el manejo de las listas de BCPs
 *	insertar_ultimo eliminar_primero eliminar_elem
 *
 * NOTA: PRIMERO SE DEBE LLAMAR A eliminar Y LUEGO A insertar
 */

/*
 * Inserta un BCP al final de la lista.
 */
static void insertar_ultimo(lista_BCPs *lista, BCP *proc)
{
	if (lista->primero == NULL)
		lista->primero = proc;
	else
		lista->ultimo->siguiente = proc;
	lista->ultimo = proc;
	proc->siguiente = NULL;
}

/*
 * Elimina el primer BCP de la lista.
 */
static void eliminar_primero(lista_BCPs *lista)
{

	if (lista->ultimo == lista->primero)
		lista->ultimo = NULL;
	lista->primero = lista->primero->siguiente;
}

/*
 * Elimina un determinado BCP de la lista.
 */
static void eliminar_elem(lista_BCPs *lista, BCP *proc)
{
	BCP *paux = lista->primero;

	if (paux == proc)
		eliminar_primero(lista);
	else
	{
		for (; ((paux) && (paux->siguiente != proc));
			 paux = paux->siguiente)
			;
		if (paux)
		{
			if (lista->ultimo == paux->siguiente)
				lista->ultimo = paux;
			paux->siguiente = paux->siguiente->siguiente;
		}
	}
}

/*
 *
 * Funciones relacionadas con la planificacion
 *	espera_int planificador
 */

/*
 * Espera a que se produzca una interrupcion
 */
static void espera_int()
{
	int nivel;

	printk("-> NO HAY LISTOS. ESPERA INT\n");

	/* Baja al m�nimo el nivel de interrupci�n mientras espera */
	nivel = fijar_nivel_int(NIVEL_1);
	halt();
	fijar_nivel_int(nivel);
}

/*
 * Funci�n de planificacion que implementa un algoritmo FIFO.
 */
static BCP *planificador()
{
	while (lista_listos.primero == NULL)
		espera_int(); /* No hay nada que hacer */
	return lista_listos.primero;
}

/*
 *
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso()
{
	BCP *p_proc_anterior;

	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */

	p_proc_actual->estado = TERMINADO;
	eliminar_primero(&lista_listos); /* proc. fuera de listos */

	/* Realizar cambio de contexto */
	p_proc_anterior = p_proc_actual;
	p_proc_actual = planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
		   p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	return; /* no deber�a llegar aqui */
}

/*
 *
 * Funciones relacionadas con el tratamiento de interrupciones
 *	excepciones: exc_arit exc_mem
 *	interrupciones de reloj: int_reloj
 *	interrupciones del terminal: int_terminal
 *	llamadas al sistemas: llam_sis
 *	interrupciones SW: int_sw
 *
 */

/*
 * Tratamiento de excepciones aritmeticas
 */
static void exc_arit()
{

	if (!viene_de_modo_usuario())
		panico("excepcion aritmetica cuando estaba dentro del kernel");

	printk("-> EXCEPCION ARITMETICA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

	return; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de excepciones en el acceso a memoria
 */
static void exc_mem()
{

	if (!viene_de_modo_usuario())
		panico("excepcion de memoria cuando estaba dentro del kernel");

	printk("-> EXCEPCION DE MEMORIA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

	return; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de interrupciones de terminal
 */
static void int_terminal()
{
	char car;

	car = leer_puerto(DIR_TERMINAL);
	printk("-> TRATANDO INT. DE TERMINAL %c\n", car);

	return;
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj()
{
	printk("-> TRATANDO INT. DE RELOJ\n");

	// recorremos la lista de procesos bloqueados
	BCP *proc_bloqueado = lista_bloq.primero;
	BCP *aux_siguiente;
	while (proc_bloqueado != NULL)
	{
		// quitamos un tick del contador
		proc_bloqueado->ticks_bloq -= 1;
		// si el proceso no tiene que seguir bloqueado más ticks
		if (proc_bloqueado->ticks_bloq == 0)
		{
			proc_bloqueado->estado = LISTO;
			// guardamos la referencia al siguiente antes de eliminar el actual de la lista
			aux_siguiente = proc_bloqueado->siguiente;
			// eliminamos al proceso de la lista de bloqueados
			eliminar_elem(&lista_bloq, proc_bloqueado);
			// lo añadimos a la lista de listos
			insertar_ultimo(&lista_listos, proc_bloqueado);
			// tomamos el siguiente proceso
			proc_bloqueado = aux_siguiente;
		}
		else
		{
			// si no cogemos el siguiente
			proc_bloqueado = proc_bloqueado->siguiente;
		}
	}

	return;
}

/*
 * Tratamiento de llamadas al sistema
 */
static void tratar_llamsis()
{
	int nserv, res;

	nserv = leer_registro(0);
	if (nserv < NSERVICIOS)
		res = (tabla_servicios[nserv].fservicio)();
	else
		res = -1; /* servicio no existente */
	escribir_registro(0, res);
	return;
}

/*
 * Tratamiento de interrupciuones software
 */
static void int_sw()
{

	printk("-> TRATANDO INT. SW\n");

	return;
}

/*
 *
 * Funcion auxiliar que crea un proceso reservando sus recursos.
 * Usada por llamada crear_proceso.
 *
 */
static int crear_tarea(char *prog)
{
	void *imagen, *pc_inicial;
	int error = 0;
	int proc;
	BCP *p_proc;
	int nivel_previo;

	proc = buscar_BCP_libre();
	if (proc == -1)
		return -1; /* no hay entrada libre */

	/* A rellenar el BCP ... */
	p_proc = &(tabla_procs[proc]);

	/* crea la imagen de memoria leyendo ejecutable */
	imagen = crear_imagen(prog, &pc_inicial);
	if (imagen)
	{
		p_proc->info_mem = imagen;
		p_proc->pila = crear_pila(TAM_PILA);
		fijar_contexto_ini(p_proc->info_mem, p_proc->pila, TAM_PILA,
						   pc_inicial,
						   &(p_proc->contexto_regs));
		p_proc->id = proc;
		p_proc->estado = LISTO;

		/* lo inserta al final de cola de listos */
		nivel_previo = fijar_nivel_int(NIVEL_3);
		insertar_ultimo(&lista_listos, p_proc);
		fijar_nivel_int(nivel_previo);
		error = 0;
	}
	else
		error = -1; /* fallo al crear imagen */

	return error;
}

/*
 *
 * Rutinas que llevan a cabo las llamadas al sistema
 *	sis_crear_proceso sis_escribir
 *
 */

/*
 * Tratamiento de llamada al sistema crear_proceso. Llama a la
 * funcion auxiliar crear_tarea sis_terminar_proceso
 */
int sis_crear_proceso()
{
	char *prog;
	int res;

	printk("-> PROC %d: CREAR PROCESO\n", p_proc_actual->id);
	prog = (char *)leer_registro(1);
	res = crear_tarea(prog);
	return res;
}

/*
 * Tratamiento de llamada al sistema escribir. Llama simplemente a la
 * funcion de apoyo escribir_ker
 */
int sis_escribir()
{
	char *texto;
	unsigned int longi;

	texto = (char *)leer_registro(1);
	longi = (unsigned int)leer_registro(2);

	escribir_ker(texto, longi);
	return 0;
}

/*
 * Tratamiento de llamada al sistema terminar_proceso. Llama a la
 * funcion auxiliar liberar_proceso
 */
int sis_terminar_proceso()
{

	printk("-> FIN PROCESO %d\n", p_proc_actual->id);

	liberar_proceso();

	return 0; /* no deber�a llegar aqui */
}

/* Rutina que devuelve el ID del proceso */
int sis_obtener_id_pr()
{
	return p_proc_actual->id;
}

/* Rutina que duerme al proceso actual n segundos */
int sis_dormir()
{
	unsigned int segundos;
	int nivel_previo;
	BCP *proc_a_dormir;
	segundos = (unsigned int)leer_registro(1);

	// indicamos ticks iniciales de bloqueo
	p_proc_actual->ticks_bloq = (segundos * TICK);
	p_proc_actual->estado = BLOQUEADO;
	proc_a_dormir = p_proc_actual;

	// inhabilitamos todas las interrupciones
	nivel_previo = fijar_nivel_int(NIVEL_3);

	// sacamos el proceso actual de la lista de listos
	eliminar_elem(&lista_listos, p_proc_actual);

	// insertamos proceso bloqueado en la lista pertinente
	insertar_ultimo(&lista_bloq, p_proc_actual);

	// volvemos al nivel anterior
	fijar_nivel_int(nivel_previo);

	// siguiente proceso
	p_proc_actual = planificador();
	cambio_contexto(&proc_a_dormir->contexto_regs, &p_proc_actual->contexto_regs);
	return 0;
}

/*
 *
 * Rutina de inicializaci�n invocada en arranque
 *
 */
int main()
{
	/* se llega con las interrupciones prohibidas */

	instal_man_int(EXC_ARITM, exc_arit);
	instal_man_int(EXC_MEM, exc_mem);
	instal_man_int(INT_RELOJ, int_reloj);
	instal_man_int(INT_TERMINAL, int_terminal);
	instal_man_int(LLAM_SIS, tratar_llamsis);
	instal_man_int(INT_SW, int_sw);

	iniciar_cont_int();		  /* inicia cont. interr. */
	iniciar_cont_reloj(TICK); /* fija frecuencia del reloj */
	iniciar_cont_teclado();	  /* inici cont. teclado */

	iniciar_tabla_proc(); /* inicia BCPs de tabla de procesos */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init") < 0)
		panico("no encontrado el proceso inicial");

	/* activa proceso inicial */
	p_proc_actual = planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
