CC = mpicc
CFLAGS = -fopenmp 

all: ass2

ass2: ass2.c
	$(CC) $(CFLAGS) -o  $@ $<

clean:
	rm -f ass2