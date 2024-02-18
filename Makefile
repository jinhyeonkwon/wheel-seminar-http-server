server.out: http.o main.o util.o
	gcc -o server.out http.o main.o util.o -lpthread

http.o: http.c http.h
	gcc -c http.c

main.o: main.c
	gcc -c main.c

util.o: util.c util.h
	gcc -c util.c

run: server.out
	./server.out

clean:
	rm -f *.o *.out