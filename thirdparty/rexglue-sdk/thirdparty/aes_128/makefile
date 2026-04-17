CC=gcc
CFLAGS=-Wall -O2

aes.out: aes.o main.o
	$(CC) -o $@ $^ $(CFLAGS)
	make clean

clean:
	rm -f *.o

cleanall:
	rm -f aes.out
	rm -f *.o