CC = clang
CFLAGS = -Wall -Wextra

all: client server

client: src/client.c src/common.h
	$(CC) $(CFLAGS) -o client src/client.c

server: src/server.c src/common.h
	$(CC) $(CFLAGS) -o server src/server.c

clean:
	rm -f client server