/* Centinela: alarma termica con el circuito de las practicas 2 y 3.
 *
 *   BMP280       temperatura
 *   Potenciometro umbral de alarma (base + 0.50 C ... base + 3.50 C)
 *   LEDs         barra de margen, o parpadeo en ALARMA
 *   Boton GPIO5  pausa y reanuda
 *   Boton GPIO6  escribe una linea de estado en pantalla
 *   centinela.csv  una linea por segundo y una en cada cambio de estado
 *
 * Compilar:  make centinela      (en la Pi)
 *            make sim            (simulado, en cualquier computadora)
 * Ejecutar:  ./centinela [direccion_BMP280_en_hexadecimal]    Ctrl+C para salir
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "centinela_cfg.h"
#include "centinela_logica.h"
#include "hw.h"

/* Periodos del programa. Se pueden cambiar entre 1 y el maximo de centinela_cfg.h. */
#define PERIODO_BOTONES_MS 50
#define PERIODO_SENSOR_MS  500
#define PERIODO_SENSOR_PAUSA_MS 1000
#define PERIODO_POT_MS     1000

_Static_assert(PERIODO_BOTONES_MS >= 1 && PERIODO_BOTONES_MS <= MAX_BOTONES_MS,
	       "PERIODO_BOTONES_MS fuera de la regla");
_Static_assert(PERIODO_SENSOR_MS >= 1 && PERIODO_SENSOR_MS <= MAX_SENSOR_MS,
	       "PERIODO_SENSOR_MS fuera de la regla");

// Esta bandera indica cuando hay que terminar el programa.
static volatile sig_atomic_t salir = 0;

// Cuando llega Ctrl+C o una señal de cierre, aviso al ciclo principal que termine.
// Aqui solo cambio la bandera; los archivos y el hardware se cierran al final.
static void al_salir(int senal)
{
	(void)senal;
	salir = 1;
}

// Obtengo el tiempo en milisegundos para controlar cuando toca hacer cada lectura.
// Uso un reloj que no depende de los ajustes de fecha y hora del sistema.
static uint32_t ahora_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

// Convierto el estado a texto para poder mostrarlo en la terminal.
static const char *texto_estado(estado_t e)
{
	switch (e) {
	case PAUSADO:   return "PAUSADO";
	case VIGILANDO: return "VIGILANDO";
	case ALARMA:    return "ALARMA";
	}
	return "?";
}

// La temperatura viene en centesimas de grado: 2534 representa 25.34 C.
// La muestro con dos decimales y con signo si es negativa.
static void imprimir_temp(int32_t c)
{
	uint32_t a = (uint32_t)(c < 0 ? -c : c);

	printf("%s%u.%02u", c < 0 ? "-" : "", a / 100u, a % 100u);
}

// Leo la temperatura del BMP280. Si falla la lectura, muestro un aviso.
static void leer_temp(hw_t *hw, int32_t *temp)
{
	if (hw_temp(hw, temp) < 0)
		fprintf(stderr, "Aviso: no se pudo leer el BMP280\n");
}

// Leo el potenciometro y calculo el umbral usando la temperatura base.
// Si la lectura falla, aviso y calculo el umbral con el valor que quede en pot.
static void leer_pot(hw_t *hw, unsigned *pot, int32_t base, int32_t *umbral)
{
	if (hw_pot(hw, pot) < 0)
		fprintf(stderr, "Aviso: no se pudo leer el potenciometro\n");
	*umbral = umbral_desde_pot(base, *pot);
}

// Abro el CSV para agregar registros sin borrar los anteriores.
// Si esta vacio, primero escribo los nombres de las columnas.
static FILE *abrir_registro(void)
{
	FILE *f = fopen(ARCHIVO_REGISTRO, "a");

	if (f == NULL) {
		perror("fopen " ARCHIVO_REGISTRO);
		return NULL;
	}
	if (ftell(f) == 0)
		fputs(ENCABEZADO_CSV, f);
	return f;
}

// Formo una linea del registro y la escribo en el CSV.
// Con sincronizar en 1 tambien pido guardar los datos pendientes en disco.
static void registrar(FILE *f, uint32_t t, int32_t temp, int32_t umbral, estado_t e, int sincronizar)
{
	char linea[96];

	formatear_registro(linea, sizeof(linea), t, temp, umbral, e);
	fputs(linea, f);
	fflush(f);
	if (sincronizar)
		fsync(fileno(f));
}

// Aqui preparo el sistema y repito las lecturas hasta que se pida salir.
int main(int argc, char *argv[])
{
	// Si no paso una direccion al ejecutar, uso 0x76 para el BMP280.
	int direccion = (argc > 1) ? (int)strtol(argv[1], NULL, 16) : 0x76;
	hw_t hw;
	FILE *reg;
	int32_t lecturas[N_BASE], base, temp = 0, umbral;
	unsigned pot = 0, b_antes;
	estado_t estado = VIGILANDO;
	uint32_t t0, t_sensor = 0, t_pot = 0, t_registro = 0, mascara_actual = 0xFFFFFFFFu;

	// Inicio el hardware y abro el registro. Si algo falla, termino.
	if (hw_abrir(&hw, direccion) < 0)
		return 1;
	reg = abrir_registro();
	if (reg == NULL) {
		hw_cerrar(&hw);
		return 1;
	}
	// Conecto las señales de cierre con la funcion que activa la bandera salir.
	signal(SIGINT, al_salir);
	signal(SIGTERM, al_salir);

	// Tomo varias muestras para calcular la temperatura base al arrancar.
	// En este ciclo, una lectura fallida se guarda como cero.
	for (int i = 0; i < N_BASE; i++) {
		if (hw_temp(&hw, &lecturas[i]) < 0)
			lecturas[i] = 0;
		usleep(100000);
	}
	// Calculo la base y el primer umbral con la posicion del potenciometro.
	base = temp_base(lecturas, N_BASE);
	hw_pot(&hw, &pot);
	umbral = umbral_desde_pot(base, pot);
	temp = base;
	printf("Base: ");
	imprimir_temp(base);
	printf(" C. Boton GPIO%u: pausa. Boton GPIO%u: estado. Ctrl+C: salir.\n", BOTON_PAUSA, BOTON_ESTADO);

	// Guardo el tiempo inicial y como estaban los botones antes de empezar.
	t0 = ahora_ms();
	b_antes = hw_botones(&hw);
	registrar(reg, 0, temp, umbral, estado, 1);
	// Este ciclo revisa botones, actualiza lecturas y controla los LEDs.
	while (!salir) {
		uint32_t t = ahora_ms() - t0;
		unsigned b = hw_botones(&hw);
		// Detecto una pulsacion nueva comparando con la lectura anterior.
		int hay_pausa = flanco_subida(b_antes & 1u, b & 1u);
		int pide_estado = flanco_subida((b_antes >> 1) & 1u, (b >> 1) & 1u);
		estado_t nuevo;
		int reanuda;
		uint32_t mascara;

		b_antes = b;
		/* Temperatura: cada 500 ms (cada 1000 ms en PAUSADO); al reanudar se lee
		 * de inmediato. Potenciometro: cada 1000 ms y al pedir el estado con GPIO6. */
		reanuda = (estado == PAUSADO && hay_pausa);
		if (reanuda || t - t_sensor >= (estado == PAUSADO ? PERIODO_SENSOR_PAUSA_MS : PERIODO_SENSOR_MS)) {
			t_sensor = t;
			leer_temp(&hw, &temp);
		}
		if (reanuda || pide_estado || t - t_pot >= PERIODO_POT_MS) {
			t_pot = t;
			leer_pot(&hw, &pot, base, &umbral);
		}

		// La logica decide si sigo vigilando, entro en alarma o quedo en pausa.
		nuevo = estado_siguiente(estado, temp, umbral, hay_pausa);
		// Registro cada cambio de estado y tambien cuando se cumple el periodo del CSV.
		if (nuevo != estado) {
			estado = nuevo;
			registrar(reg, t, temp, umbral, estado, 1);
			t_registro = t;
		} else if (t - t_registro >= PERIODO_REGISTRO_MS) {
			registrar(reg, t, temp, umbral, estado, 0);
			t_registro = t;
		}

		// Al pulsar el boton de estado, muestro los valores actuales en pantalla.
		if (pide_estado) {
			printf("%s  T = ", texto_estado(estado));
			imprimir_temp(temp);
			printf(" C  umbral = ");
			imprimir_temp(umbral);
			printf(" C  pot = %u\n", pot);
		}

		// En pausa apago los LEDs; vigilando muestro el margen y en alarma parpadean.
		switch (estado) {
		case PAUSADO:
			mascara = 0;
			break;
		case VIGILANDO:
			mascara = mascara_leds(nivel_barra(umbral - temp));
			break;
		default:
			mascara = mascara_alarma((unsigned)(t / PARPADEO_MS));
			break;
		}
		// Solo actualizo la salida cuando cambia la combinacion de LEDs.
		if (mascara != mascara_actual) {
			hw_leds(&hw, mascara);
			mascara_actual = mascara;
		}
		// Espero antes de volver a revisar los botones. usleep recibe microsegundos.
		usleep(PERIODO_BOTONES_MS * 1000u);
	}

	// Antes de salir guardo un ultimo registro y cierro el archivo y el hardware.
	registrar(reg, ahora_ms() - t0, temp, umbral, estado, 1);
	fclose(reg);
	hw_cerrar(&hw);
	printf("Centinela detenido\n");
	return 0;
}
