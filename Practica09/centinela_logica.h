#ifndef CENTINELA_LOGICA_H
#define CENTINELA_LOGICA_H

/* Aqui dejo la logica del Centinela. Las conexiones y los valores de
 * configuracion estan en centinela_cfg.h.
 * Trabajo las temperaturas en centesimas: 2500 equivale a 25 grados. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "centinela_cfg.h"

/* Primero saco la temperatura base. Sumo las lecturas y divido entre
 * la cantidad que recibi. Uso int64_t para tener mas espacio en la suma.
 * Si no hay lecturas, regreso 0 para no dividir entre cero. */
static int32_t temp_base(const int32_t *lecturas, size_t n)
{
	int64_t suma = 0;

	if (n == 0)
		return 0;
	for (size_t i = 0; i < n; i++)
		suma += lecturas[i];
	return (int32_t)(suma / (int64_t)n);
}

/* Con la base calculo el limite de temperatura para la alarma.
 * El potenciometro va de 0 a 1023 y me sirve para ajustar ese limite.
 * A la base le sumo el minimo y la parte que corresponde al potenciometro. */
static int32_t umbral_desde_pot(int32_t base_c100, unsigned pot)
{
	int32_t parte_pot = (int32_t)(((uint64_t)pot * UMBRAL_RANGO_C100) / 1023u);

	return base_c100 + UMBRAL_MIN_C100 + parte_pot;
}

/* Ahora reviso cuanto falta para llegar al limite.
 * El margen es el umbral menos la temperatura. Mientras mas pequeno sea,
 * mas LEDs uso: 4 si ya llego al limite, 3 si falta hasta 1 grado,
 * 2 si faltan hasta 2 grados y 1 si faltan hasta 4. Si falta mas, regreso 0. */
static unsigned nivel_barra(int32_t margen_c100)
{
	if (margen_c100 <= 0)
		return 4;
	if (margen_c100 <= 100)
		return 3;
	if (margen_c100 <= 200)
		return 2;
	if (margen_c100 <= 400)
		return 1;
	return 0;
}

/* Con el nivel anterior preparo los bits de los LEDs que necesito.
 * Recorro el arreglo desde el primer LED y voy juntando sus bits con |=.
 * Aunque reciba un nivel mayor, solo tomo los cuatro LEDs que hay. */
static uint32_t mascara_leds(unsigned nivel)
{
	const uint32_t leds[4] = {BIT(LED_1), BIT(LED_2), BIT(LED_3), BIT(LED_4)};
	uint32_t mascara = 0;

	for (unsigned i = 0; i < nivel && i < 4; i++)
		mascara |= leds[i];
	return mascara;
}

/* Para la alarma uso dos grupos de LEDs.
 * Si la fase es par, selecciono el 1 y el 3. Si es impar, el 2 y el 4.
 * Al cambiar la fase se pueden ir alternando los dos grupos. */
static uint32_t mascara_alarma(unsigned fase)
{
	if (fase % 2u == 0)
		return BIT(LED_1) | BIT(LED_3);
	return BIT(LED_2) | BIT(LED_4);
}

/* Aqui decido en que estado debe seguir el sistema.
 * hay_pausa indica que se acaba de presionar el boton.
 * Si esta pausado, el boton lo pone a vigilar. Si esta vigilando o en
 * alarma, el boton lo pausa. Cuando vigila, comparo la temperatura con
 * el umbral para saber si debe pasar a alarma. */
static estado_t estado_siguiente(estado_t actual, int32_t temp_c100, int32_t umbral_c100, int hay_pausa)
{
	switch (actual) {
	case PAUSADO:
		if (hay_pausa) {
			return VIGILANDO;
		}
		return PAUSADO;
	case VIGILANDO:
		if (hay_pausa)
			return PAUSADO;
		if (temp_c100 >= umbral_c100) {
			return ALARMA;
		}
		return VIGILANDO;
	case ALARMA:
		if (hay_pausa)
			return PAUSADO;
		/* Para salir de la alarma espero a que baje un poco mas del umbral.
		 * Ese margen es la histeresis y evita que cambie de estado a cada rato
		 * cuando la temperatura anda justo en el limite. */
		if (temp_c100 <= umbral_c100 - HISTERESIS_C100) {
			return VIGILANDO;
		}
		return ALARMA;
	}
	return actual;
}

/* Al final preparo una linea para el registro.
 * Primero convierto el estado a texto y despues lo escribo junto con el
 * tiempo, la temperatura y el umbral, separados por comas.
 * snprintf limita lo que escribo al espacio disponible en dst.
 * Regreso la cantidad de caracteres que tendria la linea completa. */
static int formatear_registro(char *dst, size_t n, uint32_t t_ms, int32_t temp_c100, int32_t umbral_c100, estado_t e)
{
	const char *texto = "?";

	switch (e) {
	case PAUSADO:   texto = "PAUSADO";   break;
	case VIGILANDO: texto = "VIGILANDO"; break;
	case ALARMA:    texto = "ALARMA";    break;
	}
	return snprintf(dst, n, "%lu,%ld,%ld,%s\n", (unsigned long)t_ms,
			(long)temp_c100, (long)umbral_c100, texto);
}

#endif
