CC = gcc
CFLAGS = -O3 -Wall

LIBS = -lm -lpthread

all: pc

pc: pc_E1.c
	$(CC) $(CFLAGS) -o pc pc_E1.c $(LIBS)

clean:
	rm -f pc