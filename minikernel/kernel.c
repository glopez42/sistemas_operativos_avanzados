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

/****************************************************************************************
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
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

/****************************************************************************************
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

/****************************************************************************************
 * Funciones relacionadas con la tabla de mutex:
 * iniciar_tabla_mutex, buscar_mutex_libre, buscar_nombre_mutex
 * find_mutex_descrp, get_free_mutex_descrp, get_open_mutex
 * desbloquear_proc_esperando, liberar_mutex
 */

/*
 * Funcion que inicia la tabla de mutex
 */
static void iniciar_tabla_mutex()
{
	int i;

	for (i = 0; i < NUM_MUT; i++)
	{
		tabla_mutex[i].estado = SIN_USAR;
	}

	n_mutex_open = 0;
}

/*
 * Funci�n que busca una entrada libre en la tabla de mutex
 */
static int buscar_mutex_libre()
{
	int i;

	for (i = 0; i < NUM_MUT; i++)
		if (tabla_mutex[i].estado == SIN_USAR)
			return i;
	return -1;
}

/*
 * Funci�n que dado un nombre busca si ya existe
 * Si existe se devuelve un numero positivo que indica su posicion.
 * Si no, devuelve -1
 */
static int buscar_nombre_mutex(char *nombre)
{
	int i;

	for (i = 0; i < NUM_MUT; i++)
	{
		if (tabla_mutex[i].estado != SIN_USAR && strcmp(tabla_mutex[i].nombre, nombre) == 0)
			return i;
	}
	return -1;
}

// Rutina que busca si el proceso actual tiene abierto el mutex con identificador id
int find_mutex_descrp(int id)
{
	int i;
	for (i = 0; i < NUM_MUT_PROC; i++)
	{
		if (p_proc_actual->desc_mutex[i] == id)
			return i;
	}
	return -1;
}

// Rutina que dado un proceso, devuelve el primer descriptor de mutex libre que tenga, -1 si no hay
int get_free_mutex_descrp()
{
	return find_mutex_descrp(-1);
}

// Rutina que dado el id de un mutex busca si el proceso actual tiene abierto el mutex
int get_open_mutex(int mutexid)
{
	int i;
	for (i = 0; i < NUM_MUT_PROC; i++)
	{
		if (p_proc_actual->desc_mutex[i] == mutexid)
			return i;
	}
	return -1;
}

// dada una lista desbloquea al primer proceso esperando y lo mete en la lista de listos
void desbloquear_proc_esperando(lista_BCPs *lista_bloqueos)
{
	int nivel_previo;

	BCP *proceso_desbloqueado = lista_bloqueos->primero;
	if (proceso_desbloqueado != NULL)
	{
		nivel_previo = fijar_nivel_int(3);
		proceso_desbloqueado->estado = LISTO;
		// eliminamos al primer proceso esperando
		eliminar_elem(lista_bloqueos, proceso_desbloqueado);
		// insertamos proceso bloqueado en la lista de procesos esperando al mutex
		insertar_ultimo(&lista_listos, proceso_desbloqueado);
		fijar_nivel_int(nivel_previo);
	}
}

// Funcion que libera todos los mutex del proceso actual.
// Se llama al liberar un proceso
void liberar_mutex()
{
	int i, descriptor;
	mutex *mut;

	for (i = 0; i < NUM_MUT_PROC; i++)
	{
		descriptor = p_proc_actual->desc_mutex[i];
		// aquellos mutex que tenga abiertos se cierran
		if (descriptor != -1)
		{
			mut = &tabla_mutex[descriptor];

			p_proc_actual->desc_mutex[i] = -1;
			// si el proceso actual tiene bloqueado el mutex
			if (mut->owner == p_proc_actual->id && mut->estado == LOCKED)
			{
				mut->estado = UNLOCKED;
				mut->n_blocks = 0; // cerramos todas las veces que se habia bloqueado por el proceso actual
				// desbloqueamos procesos esperando por el mutex
				desbloquear_proc_esperando(&mut->procesos_esperando);
			}

			mut->n_opens--;

			// si no hay nadie con el mutex abierto se elimina definitivamente
			if (mut->n_opens <= 0)
			{
				tabla_mutex[descriptor].estado = SIN_USAR;
				n_mutex_open--;

				// desbloqueamos procesos esperando a crear un mutex si los habia
				desbloquear_proc_esperando(&lista_bloq_mutex);
			}
		}
	}
}

/****************************************************************************************
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

	// le asignamos los ticks que tiene por rodaja
	lista_listos.primero->ticks_rodaja_restantes = TICKS_POR_RODAJA;
	return lista_listos.primero;
}

/****************************************************************************************
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso()
{
	BCP *p_proc_anterior;
	int nivel_previo;

	liberar_mutex();						 // liberamos mutex
	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */

	p_proc_actual->estado = TERMINADO;
	nivel_previo = fijar_nivel_int(NIVEL_3);
	eliminar_primero(&lista_listos); /* proc. fuera de listos */
	fijar_nivel_int(nivel_previo);

	/* Realizar cambio de contexto */
	p_proc_anterior = p_proc_actual;
	p_proc_actual = planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
		   p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	return; /* no deber�a llegar aqui */
}

/****************************************************************************************
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
	// si hay una excepcion en modo sistema y no se estaba accediendo a un parametro de usuario
	if (!viene_de_modo_usuario() && acceso_parametro == 0)
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

	// si el buffer esta completo se ignora el caracter nuevo
	if (contCaracteres < TAM_BUF_TERM)
	{
		bufferTerminal[contCaracteres] = car;
		contCaracteres++;

		// desbloqueamos a un proceso si estaba bloqueado esperando caracteres que leer
		desbloquear_proc_esperando(&lista_bloq_lectura);
	}
	return;
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj()
{
	num_ints += 1;
	printk("-> TRATANDO INT. DE RELOJ\n");

	// si hay al menos un proceso listo
	if (lista_listos.primero != NULL)
	{
		// contabilizamos si ha ocurrido en modo usuario o modo sistema
		if (viene_de_modo_usuario())
		{
			p_proc_actual->int_usuario++;
		}
		else
		{
			p_proc_actual->int_sistema++;
		}

		// contabilizamos gasto de rodaja
		p_proc_actual->ticks_rodaja_restantes--;
		// si ha llegado al final de su rodaja se activa una interrupcion software
		if (p_proc_actual->ticks_rodaja_restantes <= 0)
		{
			// guardamos referencia al proceso que queremos expulsar
			proc_a_expulsar = p_proc_actual->id;
			activar_int_SW();
		}
	}

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
	BCP *proc_expulsado;
	int nivel_previo;

	printk("-> TRATANDO INT. SW\n");

	// comprobamos que el proceso a expulsar no ha terminado
	if (p_proc_actual->id == proc_a_expulsar)
	{
		proc_expulsado = p_proc_actual;
		nivel_previo = fijar_nivel_int(NIVEL_3);
		// eliminamos al proceso de la lista de listos
		eliminar_elem(&lista_listos, proc_expulsado);
		// lo añadimos a la lista de listos
		insertar_ultimo(&lista_listos, proc_expulsado);
		fijar_nivel_int(nivel_previo);

		p_proc_actual = planificador();
		cambio_contexto(&proc_expulsado->contexto_regs, &p_proc_actual->contexto_regs);
	}

	return;
}

/****************************************************************************************
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
	int i;

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
		p_proc->int_sistema = 0;
		p_proc->int_usuario = 0;

		// iniciamos tabla de descriptores de mutex a -1
		for (i = 0; i < NUM_MUT_PROC; i++)
			p_proc->desc_mutex[i] = -1;

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

/****************************************************************************************
 * Rutinas auxiliares para el tratamiento de caracteres en el buffer de terminal
 */

int sacar_primer_caracter()
{
	int i;
	char c;

	c = bufferTerminal[0];
	// lo eliminamos del buffer
	for (i = 0; i < contCaracteres - 1; i++)
	{
		bufferTerminal[i] = bufferTerminal[i + 1];
	}

	// restamos 1 al contadores de caracteres en bufffer
	contCaracteres--;

	// devolvemos caracter
	return c;
}

/****************************************************************************************
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

/* Rutina que  contabiliza el uso del procesador por parte de un proceso */
int sis_tiempos_proceso()
{
	struct tiempos_ejec_t *tiempos;
	int nivel_previo;
	tiempos = (struct tiempos_ejec_t *)leer_registro(1);

	if (tiempos != NULL)
	{
		// inhabilitamos todas las interrupciones
		nivel_previo = fijar_nivel_int(NIVEL_3);
		// controlamos acceso por si hay excepción
		acceso_parametro = 1;

		tiempos->sistema = p_proc_actual->int_sistema;
		tiempos->usuario = p_proc_actual->int_usuario;

		acceso_parametro = 0;
		// volvemos al nivel anterior
		fijar_nivel_int(nivel_previo);
	}

	return num_ints;
}

/* Rutinas mutex */

int sis_crear_mutex()
{
	char *nombre;
	int tipo, pos, descriptor, nivel_previo, se_ha_bloqueado = 0;
	BCP *proc_a_bloquear;

	nombre = (char *)leer_registro(1);
	tipo = (int)leer_registro(2);

	// si se pasa del tamaño maximo se devuelve un error
	if (strlen(nombre) > MAX_NOM_MUT)
	{
		printk("ERROR: nombre de mutex demaisado largo.\n");
		return -1;
	}

	// miramos si al proceso actual le quedan descriptores de mutex libres
	descriptor = get_free_mutex_descrp();
	if (descriptor == -1)
	{
		printk("ERROR: proceso actual no tiene descriptores de mutex libres.\n");
		return -1;
	}

	// si ya existe ese nombre en la tabla de mutex devuelve error
	pos = buscar_nombre_mutex(nombre);
	if (pos != -1)
	{
		printk("ERROR: nombre de Mutex %s en uso.\n", nombre);
		return -1;
	}

	// si se ha alcanzado el numero maximo de mutex se bloquea hasta que se puedan crear mas
	while (n_mutex_open >= NUM_MUT)
	{
		se_ha_bloqueado = 1;
		printk("WARNING: proceso actual bloqueado, no se pueden hacer mas mutex.\n");
		nivel_previo = fijar_nivel_int(3);

		p_proc_actual->estado = BLOQUEADO;
		proc_a_bloquear = p_proc_actual;
		// sacamos el proceso actual de la lista de listos
		eliminar_elem(&lista_listos, p_proc_actual);

		// insertamos proceso bloqueado en la lista pertinente
		insertar_ultimo(&lista_bloq_mutex, p_proc_actual);

		fijar_nivel_int(nivel_previo);

		// siguiente proceso
		p_proc_actual = planificador();
		cambio_contexto(&proc_a_bloquear->contexto_regs, &p_proc_actual->contexto_regs);
	}

	// si se ha bloqueado, hay que volver a comprobar si durante ese tiempo alguien ha creado un mutex con el mismo nombre
	if (se_ha_bloqueado)
	{
		pos = buscar_nombre_mutex(nombre);
		if (pos != -1)
		{
			printk("ERROR: nombre de Mutex en uso.\n");
			return -1;
		}
	}

	// se crea por fin el mutex en una posicion libre
	pos = buscar_mutex_libre();
	strcpy(tabla_mutex[pos].nombre, nombre);
	tabla_mutex[pos].id = pos;
	tabla_mutex[pos].tipo = tipo;
	tabla_mutex[pos].estado = UNLOCKED;
	tabla_mutex[pos].n_blocks = 0;
	tabla_mutex[pos].procesos_esperando.primero = NULL;
	tabla_mutex[pos].procesos_esperando.ultimo = NULL;
	tabla_mutex[pos].n_opens = 1;
	n_mutex_open++;

	// le asignamos la posicion de la tabla al descriptor libre del proceso actual
	p_proc_actual->desc_mutex[descriptor] = pos;

	return descriptor;
}

int sis_abrir_mutex()
{
	int descr, mutexid;
	char *nombre;

	// miramos si al proceso actual le quedan descriptores de mutex libres
	descr = get_free_mutex_descrp();
	if (descr == -1)
	{
		printk("ERROR: proceso actual no tiene descriptores de mutex libres.\n");
		return -1;
	}

	nombre = (char *)leer_registro(1);
	mutexid = buscar_nombre_mutex(nombre);
	if (mutexid == -1)
	{
		printk("ERROR: no existe mutex con ese nombre.\n");
		return -1;
	}

	// se asocia el descriptor del proceso al mutex correspondiente
	p_proc_actual->desc_mutex[descr] = mutexid;
	tabla_mutex[mutexid].n_opens++;

	return mutexid;
}

int sis_lock()
{
	unsigned int mutexid, found;
	int nivel_previo;
	mutex *mut;
	BCP *proc_a_bloquear;
	mutexid = (unsigned int)leer_registro(1);

	// primero mira si el proceso ha abierto el mutex anteriormente
	found = find_mutex_descrp(mutexid);
	if (found == -1)
	{
		printk("ERROR: el proceso no ha abierto el mutex %d.\n", mutexid);
		return -1;
	}

	// obtenemos la información del mutex
	mut = &tabla_mutex[mutexid];

	// miramos si esta libre el mutex
	while (mut->estado == LOCKED)
	{

		// miramos si el proceso actual ya es propietario del mutex
		if (mut->owner == p_proc_actual->id)
		{
			// comprobamos qua tipo de mutex es
			if (mut->tipo == RECURSIVO)
			{
				mut->n_blocks++; // proceso vuelve a bloquear el mutex
				return 0;
			}
			else
			{
				printk("ERROR: el proceso ya es propietario del mutex no recursivo %d.\n", mutexid);
				return -1;
			}
		}
		else // si no es propietario y no lo puede coger se bloquea el proceso
		{
			nivel_previo = fijar_nivel_int(3);

			p_proc_actual->estado = BLOQUEADO;
			proc_a_bloquear = p_proc_actual;
			// sacamos el proceso actual de la lista de listos
			eliminar_elem(&lista_listos, p_proc_actual);

			// insertamos proceso bloqueado en la lista de procesos esperando al mutex
			insertar_ultimo(&mut->procesos_esperando, p_proc_actual);

			fijar_nivel_int(nivel_previo);

			// siguiente proceso
			p_proc_actual = planificador();
			cambio_contexto(&proc_a_bloquear->contexto_regs, &p_proc_actual->contexto_regs);
		}
	}

	// cuando este libre lo bloquea
	mut->estado = LOCKED;
	mut->owner = p_proc_actual->id;
	mut->n_blocks++;
	return 0;
}

int sis_unlock()
{
	unsigned int mutexid, found;
	mutex *mut;
	mutexid = (unsigned int)leer_registro(1);

	// primero mira si el proceso ha abierto el mutex anteriormente
	found = find_mutex_descrp(mutexid);
	if (found == -1)
	{
		printk("ERROR: el proceso no ha abierto el mutex %d.\n", mutexid);
		return -1;
	}

	// obtenemos la información del mutex
	mut = &tabla_mutex[mutexid];

	// comprueba que el mutex esta bloqueado
	if (mut->estado != LOCKED)
	{
		printk("ERROR: el mutex %d no esta bloqueado.\n", mutexid);
		return -1;
	}

	// comprueba que el proceso actual tiene bloqueado el mutex
	if (mut->owner != p_proc_actual->id)
	{
		printk("ERROR: el mutex %d no esta bloqueado por el proceso actual.\n", mutexid);
		return -1;
	}

	// restamos 1 al contador de bloqueos
	mut->n_blocks--;

	// si el proceso actual ha hecho el mismo nº de locks que unlocks
	if (mut->n_blocks == 0)
	{
		mut->estado = UNLOCKED;
		mut->owner = -1;

		// desbloqueamos al primer proceso esperando por el mutex si lo hay
		desbloquear_proc_esperando(&mut->procesos_esperando);
	}

	return 0;
}

int sis_cerrar_mutex()
{
	unsigned int mutexid;
	int descpr;
	mutex *mut;
	mutexid = (unsigned int)leer_registro(1);

	// primero mira si el proceso ha abierto el mutex anteriormente
	descpr = find_mutex_descrp(mutexid);
	if (descpr == -1)
	{
		printk("ERROR: el proceso no ha abierto el mutex %d.\n", mutexid);
		return -1;
	}

	// obtenemos la información del mutex
	mut = &tabla_mutex[mutexid];

	// eliminamos y cerramos mutex de la lista de descriptores del proceso actual
	while (descpr != -1)
	{
		p_proc_actual->desc_mutex[descpr] = -1;
		mut->n_opens--;
		descpr = find_mutex_descrp(mutexid);
	}

	// si ademas el proceso actual tiene bloqueado el mutex
	if (mut->owner == p_proc_actual->id && mut->estado == LOCKED)
	{
		mut->estado = UNLOCKED;
		mut->n_blocks = 0; // cerramos todas las veces que se habia bloqueado por el proceso actual
		// desbloqueamos procesos esperando por el mutex
		desbloquear_proc_esperando(&mut->procesos_esperando);
	}

	// si no hay nadie con el mutex abierto se elimina definitivamente
	if (mut->n_opens <= 0)
	{
		mut->estado = SIN_USAR;
		n_mutex_open--;

		// desbloqueamos procesos esperando a crear un mutex si los habia
		desbloquear_proc_esperando(&lista_bloq_mutex);
	}

	return 0;
}

/* entrada por teclado */

int sis_leer_caracter()
{
	int nivel_previo, caracter;
	BCP *proc_bloqueado;

	// inhibilitamos interrupciones de nivel 2 para que no lleguen caracteres entre medias
	nivel_previo = fijar_nivel_int(NIVEL_2);

	// mientras no haya caracteres por leer se bloquea
	while (contCaracteres == 0)
	{
		p_proc_actual->estado = BLOQUEADO;
		// quitamos todas las interrupciones
		fijar_nivel_int(NIVEL_3);
		eliminar_elem(&lista_listos, p_proc_actual);
		insertar_ultimo(&lista_bloq_lectura, p_proc_actual);
		// activamos interrupciones anteriores
		fijar_nivel_int(NIVEL_2);

		proc_bloqueado = p_proc_actual;
		// siguiente proceso
		p_proc_actual = planificador();
		cambio_contexto(&proc_bloqueado->contexto_regs, &p_proc_actual->contexto_regs);
	}

	caracter = sacar_primer_caracter();

	// volvemos a activar interrupciones
	fijar_nivel_int(nivel_previo);

	return caracter;
}

/****************************************************************************************
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

	iniciar_tabla_proc();  /* inicia BCPs de tabla de procesos */
	iniciar_tabla_mutex(); /* inicia tabla de mutex del sistema */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init") < 0)
		panico("no encontrado el proceso inicial");

	/* activa proceso inicial */
	p_proc_actual = planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
