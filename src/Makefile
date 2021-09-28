CFLAGS=-Wall -Werror
LDFLAGS=-lasound -lpthread -lzmq -lsystemd

all: nojoebuck

nojoebuck: nojoebuck.o settings.o audio.o ui-server.o
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^

install: nojoebuck
	install -m 755 nojoebuck /usr/bin
	install -m 644 nojoebuck.service /usr/lib/systemd/system/
	install -m 644 nojoebuck.default /etc/default/nojoebuck
	systemctl enable nojoebuck
	systemctl start --no-block nojoebuck

uninstall:
	-systemctl stop nojoebuck
	-systemctl disable nojoebuck
	rm -f /usr/bin/nojoebuck
	rm -f /usr/lib/systemd/system/nojoebuck.service
	rm -f /etc/default/nojoebuck

clean:
	rm -f *.o nojoebuck
