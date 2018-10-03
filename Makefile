CFLAGS=-Wall
LDFLAGS=-lasound -lpthread

all: nojoebuck

nojoebuck: main.o
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm *.o nojoebuck
