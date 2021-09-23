CFLAGS=-Wall -Werror
LDFLAGS=-lasound -lpthread -lzmq

all: nojoebuck

nojoebuck: nojoebuck.o settings.o audio.o ui-server.o
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f *.o nojoebuck
