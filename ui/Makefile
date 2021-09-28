all:

install:
	install -m 755 curses_ui.py /usr/bin
	install -m 755 hw_ui.py /usr/bin
	install -m 644 hw_ui.service /usr/lib/systemd/system/
	systemctl enable hw_ui
	systemctl start --no-block hw_ui

uninstall:
	-systemctl stop hw_ui
	-systemctl disable hw_ui
	rm -f /usr/bin/curses_ui.py /usr/bin/hw_ui.py /usr/lib/systemd/system/hw_ui.service

clean:
