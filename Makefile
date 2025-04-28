CC=gcc
CFLAGS=-Wall -pthread

all: server

server: web_server.c
	$(CC) $(CFLAGS) web_server.c -o server

clean:
	rm -f server
