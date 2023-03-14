CC=gcc
flags=-Wall

server: server.c mysocket.c mysocket.h
	$(CC) $(flags) server.c mysocket.c -o server

client: client.c mysocket.c mysocket.h
	$(CC) $(flags) client.c mysocket.c -o client
