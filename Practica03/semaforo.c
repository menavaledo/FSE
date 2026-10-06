#include <gpiod.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define CHIP "gpiochip0"
#define GPIO_ROJO 17
#define GPIO_AMARILLO 27
#define GPIO_VERDE 22
#define GPIO_PEATON 23
#define GPIO_BOTON 5

#define INTERVALO_MS 50
#define VERDE_MS 5000
#define AMARILLO_MS 2000
#define ROJO_MS 5000
#define CRUCE_MS 8000
#define PARPADEO_MS 500

static volatile sig_atomic_t ejecutando = 1;

static void detener(int senal)
{
    (void)senal;
    ejecutando = 0;
}

static long long ahora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000LL + t.tv_nsec / 1000000LL;
}

static int poner_luces(struct gpiod_line *rojo,
                       struct gpiod_line *amarillo,
                       struct gpiod_line *verde,
                       int r, int a, int v)
{
    if (gpiod_line_set_value(rojo, r) < 0 ||
        gpiod_line_set_value(amarillo, a) < 0 ||
        gpiod_line_set_value(verde, v) < 0) {
        perror("Error al cambiar las luces");
        return -1;
    }
    return 0;
}

/*
 * Se usa polling con pausas de 50 ms porque el programa debe controlar los
 * tiempos del semaforo mientras revisa el boton. La pausa evita un bucle que
 * consuma toda la CPU y permite detectar la solicitud con rapidez suficiente.
 */
static int esperar_fase(struct gpiod_line *boton, int duracion_ms,
                        int *solicitud, int *estado_anterior)
{
    long long fin = ahora_ms() + duracion_ms;

    while (ejecutando && ahora_ms() < fin) {
        int estado = gpiod_line_get_value(boton);
        if (estado < 0) {
            perror("Error al leer el boton");
            return -1;
        }

        if (estado == 1 && *estado_anterior == 0)
            *solicitud = 1;
        *estado_anterior = estado;
        usleep(INTERVALO_MS * 1000);
    }
    return 0;
}

static int cruce_peatonal(struct gpiod_line *rojo,
                          struct gpiod_line *amarillo,
                          struct gpiod_line *verde,
                          struct gpiod_line *peaton)
{
    long long inicio;
    int encendido = 1;

    if (poner_luces(rojo, amarillo, verde, 1, 0, 0) < 0)
        return -1;

    puts("Cruce peatonal: vehiculos en rojo durante 8 segundos.");
    inicio = ahora_ms();
    while (ejecutando && ahora_ms() - inicio < CRUCE_MS) {
        long long transcurrido = ahora_ms() - inicio;
        int nuevo_estado = ((transcurrido / PARPADEO_MS) % 2) == 0;
        if (nuevo_estado != encendido || transcurrido == 0) {
            encendido = nuevo_estado;
            if (gpiod_line_set_value(peaton, encendido) < 0) {
                perror("Error al controlar el indicador peatonal");
                return -1;
            }
        }
        usleep(INTERVALO_MS * 1000);
    }
    gpiod_line_set_value(peaton, 0);
    return 0;
}

int main(void)
{
    const unsigned int pines_led[4] = {
        GPIO_ROJO, GPIO_AMARILLO, GPIO_VERDE, GPIO_PEATON
    };
    struct gpiod_chip *chip = NULL;
    struct gpiod_line *leds[4] = {NULL};
    struct gpiod_line *boton = NULL;
    int configurados = 0;
    int solicitud = 0;
    int estado_anterior = 0;
    int error = 0;

    signal(SIGINT, detener);
    signal(SIGTERM, detener);

    chip = gpiod_chip_open_by_name(CHIP);
    if (chip == NULL) {
        perror("No se pudo abrir " CHIP);
        return 1;
    }

    for (int i = 0; i < 4; i++) {
        leds[i] = gpiod_chip_get_line(chip, pines_led[i]);
        if (leds[i] == NULL ||
            gpiod_line_request_output(leds[i], "semaforo", 0) < 0) {
            perror("No se pudo configurar un LED");
            error = 1;
            goto limpiar;
        }
        configurados++;
    }

    boton = gpiod_chip_get_line(chip, GPIO_BOTON);
    if (boton == NULL ||
        gpiod_line_request_input(boton, "semaforo") < 0) {
        perror("No se pudo configurar el boton");
        error = 1;
        boton = NULL;
        goto limpiar;
    }

    puts("Semaforo iniciado. Usa Ctrl+C para terminar.");
    while (ejecutando) {
        puts("Verde");
        if (poner_luces(leds[0], leds[1], leds[2], 0, 0, 1) < 0 ||
            esperar_fase(boton, VERDE_MS, &solicitud, &estado_anterior) < 0) {
            error = 1;
            break;
        }

        puts("Amarillo");
        if (poner_luces(leds[0], leds[1], leds[2], 0, 1, 0) < 0 ||
            esperar_fase(boton, AMARILLO_MS, &solicitud, &estado_anterior) < 0) {
            error = 1;
            break;
        }

        if (solicitud) {
            solicitud = 0;
            if (cruce_peatonal(leds[0], leds[1], leds[2], leds[3]) < 0) {
                error = 1;
                break;
            }
        } else {
            puts("Rojo");
            if (poner_luces(leds[0], leds[1], leds[2], 1, 0, 0) < 0 ||
                esperar_fase(boton, ROJO_MS, &solicitud, &estado_anterior) < 0) {
                error = 1;
                break;
            }

            if (solicitud) {
                solicitud = 0;
                if (cruce_peatonal(leds[0], leds[1], leds[2], leds[3]) < 0) {
                    error = 1;
                    break;
                }
            }
        }
    }

limpiar:
    for (int i = 0; i < configurados; i++) {
        gpiod_line_set_value(leds[i], 0);
        gpiod_line_release(leds[i]);
    }
    if (boton != NULL)
        gpiod_line_release(boton);
    if (chip != NULL)
        gpiod_chip_close(chip);

    puts("Semaforo apagado.");
    return error;
}
