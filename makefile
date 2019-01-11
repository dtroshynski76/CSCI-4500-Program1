CFLAGS = -Wall -ansi -pedantic
.PHONY: all, clean

objects = prog1.o
program = prog1

%.o : %.c
	   gcc -c $(CFLAGS) $<

all: $(objects)
	   gcc -o $(program) $(objects)


clean:
	   rm *.o $(program)
