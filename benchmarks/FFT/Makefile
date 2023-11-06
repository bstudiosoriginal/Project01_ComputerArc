OBJ = main.o fftmisc.o  fourierf.o
FILE = main.c fftmisc.c  fourierf.c
CFLAGS = -static -O3
CC = arm-linux-gnueabi-gcc
LIB = -lm

fft: ${OBJ} Makefile
	$(CC)  ${CFLAGS} ${OBJ} -o fft $(LIB)
fftmisc.o: fftmisc.c
	$(CC) ${CFLAGS} -c fftmisc.c $(LIB)
fourierf.o: fourierf.c 
	$(CC) ${CFLAGS} -c fourierf.c $(LIB)
main.o: main.c
	$(CC) ${CFLAGS} -c main.c $(LIB)

clean:
	rm -rf *.o fft output*
