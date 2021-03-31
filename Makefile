CC:=gcc -Wall -pthread
obj:=pi

pi: thread.o main.c
	$(CC) thread.o main.c -o pi -lm

test: 
	valgrind --leak-check=full --show-leak-kinds=all -s ./pi 

clean:
	rm -f *.o $(obj)
