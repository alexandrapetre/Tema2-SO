CC = gcc
CFLAGS = -Wall -fPIC

LDFLAGS = -L.

build:  libso_stdio.so

libso_stdio.so: so_stdio.o
	$(CC) -shared -o libso_stdio.so so_stdio.o

so_stdio.o: so_stdio.c

clean:
	rm -f *.o  libso_stdio.so
