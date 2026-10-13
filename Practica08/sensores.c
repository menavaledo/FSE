#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>

/* MCP3008 por SPI */
static unsigned mcp3008_valor(const uint8_t rx[3])
{
    return ((unsigned)(rx[1] & 0x03u) << 8) | rx[2];
}

static int pot_leer(int fd_spi, unsigned canal, unsigned *valor)
{
    uint8_t tx[3] = {0x01, (uint8_t)((8u + canal) << 4), 0x00};
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

    *valor = mcp3008_valor(rx);
    return 0;
}

/* BMP280 por I2C */
#define BMP_REG_CALIB 0x88
#define BMP_REG_CTRL  0xF4
#define BMP_REG_TEMP  0xFA

struct bmp_calib {
    uint16_t T1;
    int16_t  T2;
    int16_t  T3;
};

/* El BMP280 guarda los enteros de 16 bits con el byte bajo primero */
static uint16_t leer_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

static int bmp_leer_registros(int fd, uint8_t reg, uint8_t *dst, size_t n)
{
    if (write(fd, &reg, 1) != 1)
        return -1;
    if (read(fd, dst, n) != (ssize_t)n)
        return -1;
    return 0;
}

static int bmp_calibracion(int fd, struct bmp_calib *c)
{
    uint8_t b[6];
    if (bmp_leer_registros(fd, BMP_REG_CALIB, b, sizeof(b)) < 0)
        return -1;
    c->T1 = leer_u16_le(&b[0]);
    c->T2 = (int16_t)leer_u16_le(&b[2]);
    c->T3 = (int16_t)leer_u16_le(&b[4]);
    return 0;
}

static int32_t bmp_temperatura_cruda(const uint8_t r[3])
{
    return (int32_t)(((uint32_t)r[0] << 12) | ((uint32_t)r[1] << 4) | ((uint32_t)r[2] >> 4));
}

/* Compensacion entera del datasheet. Devuelve centesimas de grado Celsius */
static int32_t bmp_compensar(const struct bmp_calib *c, int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)c->T1 << 1))) * (int32_t)c->T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)c->T1) * ((adc_T >> 4) - (int32_t)c->T1)) >> 12) * (int32_t)c->T3) >> 14;
    return ((var1 + var2) * 5 + 128) >> 8;
}

static int bmp_iniciar(int fd)
{
    /* ctrl_meas: sobremuestreo de temperatura x1, presion x1, modo normal */
    uint8_t cmd[2] = {BMP_REG_CTRL, 0x27};
    return write(fd, cmd, 2) == 2 ? 0 : -1;
}

int main(int argc, char *argv[])
{
    int direccion = (argc > 1) ? (int)strtol(argv[1], NULL, 16) : 0x76;
    int fd_i2c = open("/dev/i2c-1", O_RDWR);
    int fd_spi = open("/dev/spidev0.0", O_RDWR);

    struct bmp_calib calib;

    if (fd_i2c < 0 || fd_spi < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd_i2c, I2C_SLAVE, direccion) < 0) {
        perror("ioctl I2C_SLAVE");
        return 1;
    }

    uint8_t modo = SPI_MODE_0;
    ioctl(fd_spi, SPI_IOC_WR_MODE, &modo);

    if (bmp_calibracion(fd_i2c, &calib) < 0 || bmp_iniciar(fd_i2c) < 0) {
        fprintf(stderr, "No responde el BMP280 en 0x%02X\n", direccion);
        return 1;
    }

    printf("dig_T1=%u, dig_T2=%d, dig_T3=%d\n", calib.T1, calib.T2, calib.T3);
    usleep(50000);

    for (int i = 0; i < 5; i++) {
        uint8_t t[3];
        unsigned valor = 0;
        if (pot_leer(fd_spi, 0, &valor) < 0 ||
            bmp_leer_registros(fd_i2c, BMP_REG_TEMP, t, sizeof(t)) < 0) {
            perror("lectura");
            return 1;
        }

        int32_t cruda = bmp_temperatura_cruda(t);
        int32_t cent = bmp_compensar(&calib, cruda);

        printf("pot %4u (%.2f V) bytes %02X %02X %02X cruda=%d T=%d.%02d C\n",
               valor, (double)valor * 3.3 / 1023.0,
               t[0], t[1], t[2], cruda, cent / 100, cent % 100);
        sleep(1);
    }

    close(fd_i2c);
    close(fd_spi);
    return 0;
}
