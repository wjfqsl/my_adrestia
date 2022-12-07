CC = arm-none-linux-gnueabi-gcc
CFLAGS = -Wall -pthread --static

OBJS = wake.o stats.o

all: adrestia
adrestia: adrestia.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean: 
	$(RM) adrestia adrestia_host $(OBJS)
