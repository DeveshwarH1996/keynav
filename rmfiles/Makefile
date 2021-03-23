CC=gcc
CFLAGS=-I.
DEPS = hellomake.h

%.o: %.cpp $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

hellomake: hellomake.o hellofunc.o 
	$(CC) -o hellomake hellomake.o hellofunc.o 

clean:
	rm -f *.o hellomake
