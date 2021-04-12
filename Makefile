CC:=gcc -Wall -pthread
obj:=pi eval

pi: thread.o main.c
	$(CC) thread.o main.c -o pi -lm

dthread: dthread.o main.c
	$(CC) dthread.o main.c -o pi -lm

test: thread.o main.c
	$(CC) thread.o main.c -o pi -lm
	valgrind --leak-check=full --show-leak-kinds=all -s ./pi 

eval:
	$(CC) eval.c -o eval -lm

clean:
	rm -f *.o $(obj)
