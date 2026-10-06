#include <gpiod.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#define CHIP "gpiochip0"
#define GPIO_BOTON 5
#define GPIO_LED 17

static volatile sig_atomic_t ejecutando = 1;

static void detener(int senal)
{
    (void)senal;
    ejecutando = 0;
}

int main(void)
{
    struct gpiod_chip *chip;
    struct gpiod_line *boton;
    struct gpiod_line *led;

    signal(SIGINT, detener);
    signal(SIGTERM, detener);

    chip = gpiod_chip_open_by_name(CHIP);
    if (chip == NULL) {
        perror("No se pudo abrir " CHIP);
        return 1;
    }

    boton = gpiod_chip_get_line(chip, GPIO_BOTON);
    led = gpiod_chip_get_line(chip, GPIO_LED);
    if (boton == NULL || led == NULL) {
        perror("No se pudieron obtener las lineas GPIO");
        gpiod_chip_close(chip);
        return 1;
    }

    if (gpiod_line_request_input(boton, "boton_polling") < 0) {
        perror("No se pudo configurar el boton");
        gpiod_chip_close(chip);
        return 1;
    }

    if (gpiod_line_request_output(led, "boton_polling", 0) < 0) {
        perror("No se pudo configurar el LED");
        gpiod_line_release(boton);
        gpiod_chip_close(chip);
        return 1;
    }

    puts("Presiona el boton. Usa Ctrl+C para terminar.");
    while (ejecutando) {
        int estado = gpiod_line_get_value(boton);
        if (estado < 0) {
            perror("Error al leer el boton");
            break;
        }
        gpiod_line_set_value(led, estado);
        /* Polling activo intencional para comparar su consumo en top. */
    }

    gpiod_line_set_value(led, 0);
    gpiod_line_release(led);
    gpiod_line_release(boton);
    gpiod_chip_close(chip);
    return 0;
}
