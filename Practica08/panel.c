#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include "gpio_regs.h"

#define BTN1 5
#define BTN2 6

static const unsigned leds[4] = {17, 27, 22, 23};
static volatile sig_atomic_t ejecutando = 1;

static void manejador_sigint(int sig)
{
    (void)sig;
    ejecutando = 0;
}

static void configurar_pin(gpio_regs_t *gpio, unsigned pin, uint32_t funcion)
{
    unsigned reg = pin / 10;
    unsigned desp = (pin % 10) * 3;
    uint32_t valor = gpio->GPFSEL[reg];
    valor &= ~(0x7u << desp);
    valor |= (funcion & 0x7u) << desp;
    gpio->GPFSEL[reg] = valor;
}

/* MCP3008 y BMP280 */
static int pot_leer(int fd_spi, unsigned *valor)
{
    uint8_t tx[3] = {0x01, 0x80, 0x00};
    uint8_t rx[3] = {0, 0, 0};
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = 3;
    tr.speed_hz = 1000000;
    tr.bits_per_word = 8;

    if (ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr) < 0)
        return -1;

    *valor = ((unsigned)(rx[1] & 0x03u) << 8) | rx[2];
    return 0;
}

struct bmp_calib {
    uint16_t T1;
    int16_t  T2;
    int16_t  T3;
};

static uint16_t leer_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

static int bmp_leer_registros(int fd, uint8_t reg, uint8_t *dst, size_t n)
{
    if (write(fd, &reg, 1) != 1) return -1;
    if (read(fd, dst, n) != (ssize_t)n) return -1;
    return 0;
}

static int bmp_calibracion(int fd, struct bmp_calib *c)
{
    uint8_t b[6];
    if (bmp_leer_registros(fd, 0x88, b, sizeof(b)) < 0) return -1;
    c->T1 = leer_u16_le(&b[0]);
    c->T2 = (int16_t)leer_u16_le(&b[2]);
    c->T3 = (int16_t)leer_u16_le(&b[4]);
    return 0;
}

static int32_t bmp_compensar(const struct bmp_calib *c, int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)c->T1 << 1))) * (int32_t)c->T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)c->T1) * ((adc_T >> 4) - (int32_t)c->T1)) >> 12) * (int32_t)c->T3) >> 14;
    return ((var1 + var2) * 5 + 128) >> 8;
}

int main(int argc, char *argv[])
{
    int dir_i2c = (argc > 1) ? (int)strtol(argv[1], NULL, 16) : 0x76;
    signal(SIGINT, manejador_sigint);

    gpio_regs_t *gpio = gpio_abrir();
    if (!gpio) return 1;

    int fd_i2c = open("/dev/i2c-1", O_RDWR);
    int fd_spi = open("/dev/spidev0.0", O_RDWR);
    if (fd_i2c < 0 || fd_spi < 0) return 1;

    ioctl(fd_i2c, I2C_SLAVE, dir_i2c);
    uint8_t modo = SPI_MODE_0;
    ioctl(fd_spi, SPI_IOC_WR_MODE, &modo);

    struct bmp_calib calib;
    bmp_calibracion(fd_i2c, &calib);
    uint8_t cmd[2] = {0xF4, 0x27};
    write(fd_i2c, cmd, 2);

    /* Configurar GPIOs */
    for (int i = 0; i < 4; i++) configurar_pin(gpio, leds[i], 1);
    configurar_pin(gpio, BTN1, 0);
    configurar_pin(gpio, BTN2, 0);

    bool congelado = false;
    bool btn1_prev = false;
    bool btn2_prev = false;

    while (ejecutando) {
        unsigned pot_val = 0;
        pot_leer(fd_spi, &pot_val);

        uint32_t lev = gpio->GPLEV[0];
        bool btn1_act = (lev & BIT(BTN1)) != 0;
        bool btn2_act = (lev & BIT(BTN2)) != 0;

        /* Detección de flanco de subida */
        if (btn1_act && !btn1_prev) {
            congelado = !congelado;
        }
        if (btn2_act && !btn2_prev) {
            uint8_t t[3];
            bmp_leer_registros(fd_i2c, 0xFA, t, 3);
            int32_t cruda = (int32_t)(((uint32_t)t[0] << 12) | ((uint32_t)t[1] << 4) | ((uint32_t)t[2] >> 4));
            int32_t cent = bmp_compensar(&calib, cruda);
            printf("Temp: %d.%02d C | Pot: %u\n", cent / 100, cent % 100, pot_val);
        }

        btn1_prev = btn1_act;
        btn2_prev = btn2_act;

        if (!congelado) {
            uint32_t set_mask = 0;
            uint32_t clr_mask = 0;

            int leds_a_encender = (int)(pot_val / 205);
            if (leds_a_encender > 4) leds_a_encender = 4;

            for (int i = 0; i < 4; i++) {
                if (i < leds_a_encender) set_mask |= BIT(leds[i]);
                else clr_mask |= BIT(leds[i]);
            }

            if (set_mask) gpio->GPSET[0] = set_mask;
            if (clr_mask) gpio->GPCLR[0] = clr_mask;
        }

        usleep(50000); /* Muestreo de 50 ms */
    }

    /* Verificación de pines del bus */
    const unsigned pines_bus[] = {2, 3, 8, 9, 10, 11};
    for (size_t i = 0; i < 6; i++) {
        unsigned pin = pines_bus[i];
        uint32_t campo = (gpio->GPFSEL[pin / 10] >> ((pin % 10) * 3)) & 0x7u;
        if (campo != 4) printf("Aviso: el pin %u cambió de función ALT0 (%u)\n", pin, campo);
    }

    /* Apagar LEDs y restaurar a entrada */
    gpio->GPCLR[0] = BIT(17) | BIT(27) | BIT(22) | BIT(23);
    for (int i = 0; i < 4; i++) configurar_pin(gpio, leds[i], 0);

    close(fd_i2c);
    close(fd_spi);
    return 0;
}
