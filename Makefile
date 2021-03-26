CC:=gcc -Wall -pthread
obj:=pi

pi: thread.o main.c
	$(CC) thread.o main.c -o pi -lm
    
clean:
	rm -f *.o $(obj)
