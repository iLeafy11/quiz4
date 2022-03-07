CC:=gcc -Wall -pthread
obj:=pi eval a.out

pi: thread.o main.c
	$(CC) -g thread.o main.c -o pi -lm -fsanitize=thread
	./pi

dthread: dthread.o main.c
	$(CC) dthread.o main.c -o pi -lm

ring: rthread.o main.c
	$(CC) rthread.o main.c -o pi -lm

test:
	valgrind --leak-check=full --show-leak-kinds=all -s ./pi 

sanitizer: dthread.o main.c
	$(CC) -g dthread.o main.c -o pi -lm -fsanitize=thread 
	./pi

eval:
	$(CC) eval.c -o eval -lm

clean:
	rm -f *.o $(obj)
