#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>

#define CHIP "gpiochip0"
#define N_LEDS 4

int main(void)
{
    const unsigned int pines[N_LEDS] = {17, 27, 22, 23};
    struct gpiod_chip *chip = gpiod_chip_open_by_name(CHIP);
    struct gpiod_line *lineas[N_LEDS] = {NULL};

    if (chip == NULL) {
        perror("No se pudo abrir " CHIP);
        return 1;
    }

    for (int i = 0; i < N_LEDS; i++) {
        lineas[i] = gpiod_chip_get_line(chip, pines[i]);
        if (lineas[i] == NULL ||
            gpiod_line_request_output(lineas[i], "leds", 0) < 0) {
            perror("No se pudo configurar un LED");
            for (int j = 0; j < i; j++)
                gpiod_line_release(lineas[j]);
            gpiod_chip_close(chip);
            return 1;
        }
    }

    for (int ronda = 0; ronda < 5; ronda++) {
        for (int i = 0; i < N_LEDS; i++) {
            gpiod_line_set_value(lineas[i], 1);
            usleep(150000);
            gpiod_line_set_value(lineas[i], 0);
        }
    }

    for (int i = 0; i < N_LEDS; i++) {
        gpiod_line_set_value(lineas[i], 0);
        gpiod_line_release(lineas[i]);
    }
    gpiod_chip_close(chip);
    return 0;
}
