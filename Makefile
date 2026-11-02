CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
LDFLAGS = -pthread

ALL = server client

all: $(ALL)

server: server.o rede_capenga.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server.o: server.c rede_capenga.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

rede_capenga.o: rede_capenga.c rede_capenga.h
	$(CC) $(CFLAGS) -c rede_capenga.c

clean:
	rm -f *.o $(ALL)

.PHONY: all clean
