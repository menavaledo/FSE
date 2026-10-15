#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>

#define CHIP "/dev/gpiochip0"
#define N_LEDS 4

int main(void)
{
    const unsigned int pines[N_LEDS] = {17, 27, 22, 23};
    
    /* 1. Abrir el chip */
    struct gpiod_chip *chip = gpiod_chip_open(CHIP);
    if (!chip) {
        perror("No se pudo abrir " CHIP);
        return 1;
    }

    /* 2. Preparar la configuración de las líneas (como salidas) */
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, pines, N_LEDS, settings);

    /* 3. Preparar la configuración de la solicitud */
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "leds");

    /* 4. Solicitar todas las líneas al mismo tiempo */
    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!request) {
        perror("No se pudieron configurar los LEDs");
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return 1;
    }

    /* 5. Secuencia de parpadeo */
    for (int ronda = 0; ronda < 5; ronda++) {
        for (int i = 0; i < N_LEDS; i++) {
            gpiod_line_request_set_value(request, pines[i], GPIOD_LINE_VALUE_ACTIVE);
            usleep(150000);
            gpiod_line_request_set_value(request, pines[i], GPIOD_LINE_VALUE_INACTIVE);
        }
    }

    /* 6. Liberar recursos */
    gpiod_line_request_release(request);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_close(chip);
    
    return 0;
}
