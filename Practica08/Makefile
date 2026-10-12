CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -Wconversion -O0 -g
PROGS = tipos bits desborde regs_leer leds_regs sensores panel b_con_O0 b_sin_O0 b_con_O2 b_sin_O2

all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

regs_leer leds_regs: gpio_regs.h

clean:
	rm -f $(PROGS) b_* *.o
