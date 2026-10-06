#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_ADDR 0x76  // o 0x77, según lo que muestre i2cdetect
#define REG_ID   0xD0

int main(void)
{
    int fd = open("/dev/i2c-1", O_RDWR);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, I2C_ADDR) < 0)
    {
        perror("ioctl I2C_SLAVE");
        close(fd);
        return 1;
    }

    // Escribir la dirección del registro que queremos leer
    unsigned char reg = REG_ID;

    if (write(fd, &reg, 1) != 1)
    {
        perror("write");
        close(fd);
        return 1;
    }

    // Leer el Chip ID
    unsigned char chip_id;

    if (read(fd, &chip_id, 1) != 1)
    {
        perror("read");
        close(fd);
        return 1;
    }

    printf("Chip ID leído: 0x%02X\n", chip_id);

    // Identifica el sensor
    if (chip_id == 0x60)
    {
        printf("Es un BME280\n");
    }
    else if (chip_id == 0x58)
    {
        printf("Es un BMP280\n");
    }
    else
    {
        printf("Sensor desconocido\n");
    }

    close(fd);

    return 0;
}
