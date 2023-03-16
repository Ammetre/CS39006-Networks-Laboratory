CC=gcc
flags=-Wall -L.
library: mysocket.c mysocket.h
	gcc -c mysocket.c
	ar rcs libmsocket.a mysocket.o 
server: server.c
	$(CC) $(flags) server.c -lmsocket -o server

client: client.c
	$(CC) $(flags) client.c -lmsocket -o client
