CFLAGS=-Wall -Werror
LDFLAGS=-lasound -lpthread -lzmq

all: nojoebuck info n2

nojoebuck: main.o
	$(CC) $(LDFLAGS) $^ -o $@

n2: n2.o settings.o audio.o ui-server.o
	$(CC) $(LDFLAGS) $^ -o $@

info: info.o
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f *.o nojoebuck info n2
