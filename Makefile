CFLAGS=-Wall -Werror
LDFLAGS=-lasound -lpthread -lzmq

all: nojoebuck

nojoebuck: main.o
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm *.o nojoebuck
