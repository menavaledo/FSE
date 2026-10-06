/*
 * sensorcombinado.c
 * Practica 3, Parte F
 *
 * BME280 + MCP3008 + LED
 *
 * BME280:
 *   I2C: /dev/i2c-1
 *   Direccion: 0x76
 *   Chip ID: 0x60
 *
 * MCP3008:
 *   SPI: /dev/spidev0.0
 *   Canal: CH0
 *
 * LED:
 *   GPIO17
 *
 * Compilar:
 * gcc -DUSE_GPIOD_V2 sensorcombinado.c -o sensorcombinado -lgpiod
 *
 * Ejecutar:
 * ./sensorcombinado
 *
 * Salir:
 * Ctrl+C
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>

#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>

#include <gpiod.h>


/* ============================================================
   CONFIGURACION
   ============================================================ */

#define I2C_DEV          "/dev/i2c-1"
#define I2C_ADDR         0x76

#define SPI_DEV          "/dev/spidev0.0"
#define SPI_VEL_HZ       1000000
#define MCP_CANAL        0

#define LED_GPIO         17
#define GPIO_CHIP_NAME   "gpiochip0"

#define TEMP_MAX_UMBRAL  50.0f

#define PERIODO_TEMP_S   2.0
#define PERIODO_POT_US   100000


/* ============================================================
   REGISTROS BME280
   ============================================================ */

#define REG_ID           0xD0
#define REG_CALIB_T      0x88
#define REG_TEMP_MSB     0xFA


/* ============================================================
   CONTROL DE SALIDA
   ============================================================ */

static volatile sig_atomic_t g_salir = 0;


static void manejar_sigint(int sig)
{
    (void)sig;
    g_salir = 1;
}


/* ============================================================
   TIEMPO
   ============================================================ */

static double tiempo_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1e9;
}


/* ============================================================
   I2C
   ============================================================ */

/*
 * Lee varios bytes comenzando desde un registro.
 */
static int i2c_leer(
    int fd,
    uint8_t reg,
    uint8_t *buf,
    size_t n)
{
    if (write(fd, &reg, 1) != 1) {
        perror("i2c write");
        return -1;
    }

    if (read(fd, buf, n) != (ssize_t)n) {
        perror("i2c read");
        return -1;
    }

    return 0;
}


/* ============================================================
   BME280
   ============================================================ */

typedef struct
{
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

} bme280_calib_t;


/*
 * Inicializa el BME280.
 *
 * IMPORTANTE:
 * No escribimos en CTRL_MEAS porque en nuestra Raspberry
 * esa escritura estaba generando Input/output error.
 *
 * La lectura de temperatura utiliza los registros del BME280
 * y sus coeficientes de calibracion.
 */
static int bme280_iniciar(
    int fd,
    bme280_calib_t *cal)
{
    uint8_t id;

    /* Leer Chip ID */

    if (i2c_leer(fd, REG_ID, &id, 1) < 0)
        return -1;


    /* Identificar sensor */

    if (id == 0x60) {

        printf("BME280 detectado (ID 0x60)\n");

    }
    else if (id == 0x58) {

        printf("BMP280 detectado (ID 0x58)\n");

    }
    else {

        fprintf(
            stderr,
            "ID inesperado: 0x%02X\n",
            id
        );

        return -1;
    }


    /* Leer coeficientes de calibracion */

    uint8_t c[6];

    if (i2c_leer(fd, REG_CALIB_T, c, 6) < 0)
        return -1;


    /*
     * Los datos estan almacenados
     * en formato little-endian.
     */

    cal->dig_T1 =
        (uint16_t)(c[0] | (c[1] << 8));

    cal->dig_T2 =
        (int16_t)(c[2] | (c[3] << 8));

    cal->dig_T3 =
        (int16_t)(c[4] | (c[5] << 8));


    printf("Calibracion de temperatura leida correctamente.\n");

    return 0;
}


/*
 * Lee la temperatura del BME280.
 */
static int bme280_leer_temp(
    int fd,
    const bme280_calib_t *cal,
    float *temp_c)
{
    uint8_t d[3];


    /* Leer temperatura */

    if (i2c_leer(
            fd,
            REG_TEMP_MSB,
            d,
            3) < 0)
        return -1;


    /*
     * Convertir los 3 bytes a un valor
     * ADC de 20 bits.
     */

    int32_t adc_T =
        ((int32_t)d[0] << 12) |
        ((int32_t)d[1] << 4) |
        (d[2] >> 4);


    /*
     * Formula de compensacion del datasheet.
     */

    int32_t var1 =
        ((((adc_T >> 3) -
           ((int32_t)cal->dig_T1 << 1))) *
         ((int32_t)cal->dig_T2)) >> 11;


    int32_t var2 =
        (((((adc_T >> 4) -
            (int32_t)cal->dig_T1) *
           ((adc_T >> 4) -
            (int32_t)cal->dig_T1)) >> 12) *
         ((int32_t)cal->dig_T3)) >> 14;


    int32_t t_fine =
        var1 + var2;


    int32_t T =
        (t_fine * 5 + 128) >> 8;


    /*
     * Convertir a grados Celsius.
     */

    *temp_c =
        T / 100.0f;


    return 0;
}


/* ============================================================
   MCP3008
   ============================================================ */

static int mcp3008_iniciar(int fd)
{
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_VEL_HZ;


    if (ioctl(
            fd,
            SPI_IOC_WR_MODE,
            &mode) < 0) {

        perror("Error al configurar modo SPI");
        return -1;
    }


    if (ioctl(
            fd,
            SPI_IOC_WR_BITS_PER_WORD,
            &bits) < 0) {

        perror("Error al configurar bits SPI");
        return -1;
    }


    if (ioctl(
            fd,
            SPI_IOC_WR_MAX_SPEED_HZ,
            &speed) < 0) {

        perror("Error al configurar velocidad SPI");
        return -1;
    }


    return 0;
}


/*
 * Lee un canal del MCP3008.
 *
 * Resultado:
 * 0 - 1023
 */
static int mcp3008_leer(
    int fd,
    uint8_t canal)
{
    uint8_t tx[3] = {

        0x01,

        (uint8_t)((8 + canal) << 4),

        0x00
    };


    uint8_t rx[3] = {0};


    struct spi_ioc_transfer tr;

    memset(
        &tr,
        0,
        sizeof(tr)
    );


    tr.tx_buf =
        (unsigned long)(uintptr_t)tx;


    tr.rx_buf =
        (unsigned long)(uintptr_t)rx;


    tr.len =
        3;


    tr.speed_hz =
        SPI_VEL_HZ;


    tr.bits_per_word =
        8;


    if (ioctl(
            fd,
            SPI_IOC_MESSAGE(1),
            &tr) < 0) {

        perror("Error en comunicacion SPI");
        return -1;
    }


    /*
     * Convertir respuesta del MCP3008
     * a un valor de 10 bits.
     */

    return ((rx[1] & 3) << 8) | rx[2];
}


/* ============================================================
   LED - LIBGPIOD 2.x
   ============================================================ */

static struct gpiod_chip *g_chip = NULL;

static struct gpiod_line_request *g_req = NULL;


/*
 * Inicializar LED.
 */
static int led_iniciar(void)
{
    struct gpiod_line_settings *ls = NULL;

    struct gpiod_line_config *lc = NULL;

    struct gpiod_request_config *rc = NULL;

    unsigned int offset = LED_GPIO;

    int ret = -1;


    /* Abrir GPIO chip */

    g_chip =
        gpiod_chip_open(
            "/dev/" GPIO_CHIP_NAME
        );


    if (!g_chip) {

        perror("Error al abrir GPIO chip");

        return -1;
    }


    /* Crear configuraciones */

    ls =
        gpiod_line_settings_new();


    lc =
        gpiod_line_config_new();


    rc =
        gpiod_request_config_new();


    if (!ls || !lc || !rc)
        goto fin;


    /* GPIO como salida */

    gpiod_line_settings_set_direction(
        ls,
        GPIOD_LINE_DIRECTION_OUTPUT
    );


    /* LED apagado inicialmente */

    gpiod_line_settings_set_output_value(
        ls,
        GPIOD_LINE_VALUE_INACTIVE
    );


    /* Agregar GPIO17 */

    if (gpiod_line_config_add_line_settings(
            lc,
            &offset,
            1,
            ls) < 0) {

        goto fin;
    }


    /* Nombre del consumidor */

    gpiod_request_config_set_consumer(
        rc,
        "sensorcombinado"
    );


    /* Solicitar GPIO */

    g_req =
        gpiod_chip_request_lines(
            g_chip,
            rc,
            lc
        );


    if (!g_req) {

        perror("Error al solicitar GPIO");

        goto fin;
    }


    ret = 0;


fin:

    if (rc)
        gpiod_request_config_free(rc);

    if (lc)
        gpiod_line_config_free(lc);

    if (ls)
        gpiod_line_settings_free(ls);


    return ret;
}


/*
 * Encender o apagar LED.
 */
static void led_set(int encendido)
{
    if (!g_req)
        return;


    gpiod_line_request_set_value(
        g_req,
        LED_GPIO,
        encendido
            ? GPIOD_LINE_VALUE_ACTIVE
            : GPIOD_LINE_VALUE_INACTIVE
    );
}


/*
 * Liberar LED.
 */
static void led_cerrar(void)
{
    if (g_req) {

        led_set(0);

        gpiod_line_request_release(
            g_req
        );
    }


    if (g_chip) {

        gpiod_chip_close(
            g_chip
        );
    }
}


/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    int i2c_fd = -1;

    int spi_fd = -1;

    int ret = 1;


    bme280_calib_t cal;


    /* Ctrl+C */

    signal(
        SIGINT,
        manejar_sigint
    );


    signal(
        SIGTERM,
        manejar_sigint
    );


    /* ========================================================
       I2C / BME280
       ======================================================== */

    i2c_fd =
        open(
            I2C_DEV,
            O_RDWR
        );


    if (i2c_fd < 0) {

        perror("Error al abrir I2C");

        goto fin;
    }


    if (ioctl(
            i2c_fd,
            I2C_SLAVE,
            I2C_ADDR) < 0) {

        perror("Error al seleccionar BME280");

        goto fin;
    }


    if (bme280_iniciar(
            i2c_fd,
            &cal) < 0) {

        goto fin;
    }


    /* ========================================================
       SPI / MCP3008
       ======================================================== */

    spi_fd =
        open(
            SPI_DEV,
            O_RDWR
        );


    if (spi_fd < 0) {

        perror("Error al abrir SPI");

        goto fin;
    }


    if (mcp3008_iniciar(
            spi_fd) < 0) {

        goto fin;
    }


    /* ========================================================
       LED
       ======================================================== */

    if (led_iniciar() < 0)
        goto fin;


    printf(
        "Corriendo. Gira el potenciometro para cambiar "
        "el umbral (Ctrl+C para salir).\n\n"
    );


    double t_ultima_temp =
        -PERIODO_TEMP_S;


    float temp =
        0.0f;


    int temp_valida =
        0;


    int led_encendido =
        0;


    /* ========================================================
       CICLO PRINCIPAL
       ======================================================== */

    while (!g_salir)
    {
        /*
         * 1. Leer potenciometro.
         */

        int valor =
            mcp3008_leer(
                spi_fd,
                MCP_CANAL
            );


        if (valor < 0)
            goto fin;


        /*
         * Convertir 0-1023
         * a 0-50 grados C.
         */

        float umbral =
            valor *
            TEMP_MAX_UMBRAL /
            1023.0f;


        /*
         * 2. Leer temperatura cada 2 segundos.
         */

        int imprimir =
            0;


        double ahora =
            tiempo_s();


        if (ahora - t_ultima_temp >=
            PERIODO_TEMP_S)
        {
            if (bme280_leer_temp(
                    i2c_fd,
                    &cal,
                    &temp) < 0) {

                goto fin;
            }


            temp_valida =
                1;


            t_ultima_temp =
                ahora;


            imprimir =
                1;
        }


        /*
         * 3. Control del LED.
         */

        if (temp_valida)
        {
            int debe_encender =
                (temp > umbral);


            if (debe_encender !=
                led_encendido)
            {
                led_set(
                    debe_encender
                );


                led_encendido =
                    debe_encender;
            }
        }


        /*
         * 4. Mostrar informacion.
         */

        if (imprimir)
        {
            printf(
                "Temp: %5.2f C | "
                "Umbral: %5.2f C | "
                "pot=%4d | "
                "LED: %s\n",

                temp,

                umbral,

                valor,

                led_encendido
                    ? "ON"
                    : "OFF"
            );


            fflush(stdout);
        }


        /*
         * Leer potenciometro
         * cada 100 ms.
         */

        usleep(
            PERIODO_POT_US
        );
    }


    ret = 0;


    printf(
        "\nSaliendo...\n"
    );


fin:

    led_cerrar();


    if (spi_fd >= 0)
        close(spi_fd);


    if (i2c_fd >= 0)
        close(i2c_fd);


    return ret;
}

