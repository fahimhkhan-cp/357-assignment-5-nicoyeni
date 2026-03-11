CC = gcc
CFLAGS = -Wall -std=c99 -pedantic
SERVER = httpd
SERVER_OBJS = httpd.o net.o

all : $(SERVER)

$(SERVER) : $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

httpd.o : httpd.c net.h
	$(CC) $(CFLAGS) -c httpd.c

net.o : net.c net.h
	$(CC) $(CFLAGS) -c net.c

clean :
	rm -f *.o $(SERVER) core
