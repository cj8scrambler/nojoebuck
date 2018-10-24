import curses
import zmq
import logging
import time

BUF_Y = 5
UI_CMD = "ipc:///tmp/nojobuck_cmd"
UI_STATUS = "ipc:///tmp/nojobuck_status"
MAX_DELAY = 120000

delay = 2500
buf = 75
last_redraw = 0.0

def buf_progress(stdscr):
    global buf

    logging.debug('redraw buf at: %d/100' % (buf))
    # Use an odd width so there's a middle:
    w = curses.COLS - 1
    if ((w % 2) == 0):
        w -= 1
    # find the middle
    m = curses.COLS / 2

    left = int((buf / 100.0) * m)
    if (left > m):
        left = m
    else:
        left -= 2

    right = int(((buf - 100) / 100.0) * m) 
    if (right < 0):
        right = 0
    else:
        right -= 2

    logging.debug('  width: %d  middle: %d  left: %d  right: %d' % (w, m, left, right))

    stdscr.addstr(BUF_Y - 1, m - 7, "Buffer Status")

    stdscr.move(BUF_Y, 2)
    stdscr.addch(curses.ACS_ULCORNER)
    stdscr.hline(curses.ACS_HLINE, w-4)
    stdscr.move(BUF_Y, w-1)
    stdscr.addch(curses.ACS_URCORNER)


    stdscr.move(BUF_Y + 1, 2)
    stdscr.addch(curses.ACS_VLINE)
    stdscr.clrtoeol()
    if (left):
        stdscr.addstr("#" * left)
    stdscr.move(BUF_Y + 1, m)
    stdscr.addch(curses.ACS_VLINE)
    if (right):
        stdscr.addstr("#" * right)
    stdscr.move(BUF_Y + 1, w-1)
    stdscr.addch(curses.ACS_VLINE)


    stdscr.move(BUF_Y + 2, 2)
    stdscr.addch(curses.ACS_VLINE)
    stdscr.clrtoeol()
    if (left):
        stdscr.addstr("#" * left)
    stdscr.move(BUF_Y + 2, m)
    stdscr.addch(curses.ACS_VLINE)
    if (right):
        stdscr.addstr("#" * right)
    stdscr.move(BUF_Y + 2, w-1)
    stdscr.addch(curses.ACS_VLINE)


    stdscr.move(BUF_Y + 3, 2)
    stdscr.addch(curses.ACS_LLCORNER)
    stdscr.hline(curses.ACS_HLINE, (w - m -2))
    stdscr.move(BUF_Y + 3, m)
    stdscr.addch(curses.ACS_BTEE)
    stdscr.hline(curses.ACS_HLINE, (w - m -2))
    stdscr.move(BUF_Y + 3, w-1)
    stdscr.addch(curses.ACS_LRCORNER)

    stdscr.addstr(BUF_Y + 5, 3, "Empty")
    stdscr.addstr(BUF_Y + 5, m - 2, "Full")
    stdscr.addstr(BUF_Y + 5, w - 5, "Over")


def redraw(stdscr):
    global delay
    global last_redraw

    # Don't run more than once every 100 ms
    now = time.time()
    if ((buf != 100) and (now - last_redraw) < 0.100):
        logging.debug('%.2f skip redraw' % (now))
        return

    logging.debug('%.2f do redraw' % (now))
    last_redraw = now

    #stdscr.clear()
    height, width = stdscr.getmaxyx()
    stdscr.addstr(2, 2, "Delay: %.2f" % (delay/1000.0), curses.A_REVERSE)
    stdscr.clrtoeol()
    buf_progress(stdscr);
    stdscr.refresh()

def main(stdscr):
    global delay
    global buf

    logging.basicConfig(filename='log',level=logging.ERROR)

    socket_cmd = zmq.Context().socket(zmq.PUSH)
    socket_cmd.connect (UI_CMD)

    socket_status = zmq.Context().socket(zmq.PULL)
    socket_status.connect (UI_STATUS)

    # query the current delay
    socket_cmd.send("D:");
    cmd = socket_status.recv().split(':');
    delay = int(cmd[1]);
    logging.debug('got delay query response: %d' % (delay))
    new_delay = delay

    # query the current buffer
    socket_cmd.send("B:");
    cmd = socket_status.recv().split(':');
    buf = int(cmd[1]);
    logging.debug('got buffer query response: %d' % (buf))

    curses.curs_set(False)
    stdscr.nodelay(True)
    redraw(stdscr)

    while True:
        c = stdscr.getch()
        if (c == ord('k')) or (c == curses.KEY_UP):
            new_delay = delay + 10
        if (c == ord('j')) or (c == curses.KEY_DOWN):
            new_delay = delay - 10
        if (c == curses.KEY_PPAGE) or (c == curses.KEY_RIGHT):
            new_delay = delay + 500
        if (c == curses.KEY_NPAGE) or (c == curses.KEY_LEFT):
            new_delay = delay - 500
        if c == ord('q'):
            break  # Exit the while loop

        if (new_delay < 0):
            new_delay = 0
        if (new_delay > MAX_DELAY):
            new_delay = MAX_DELAY
        if (new_delay != delay):
            # send delay update message
            delay = new_delay
            logging.debug('Updated delay: %d' % (delay))
            socket_cmd.send("D:%d" % (delay))
            redraw(stdscr)

        message = ""
        try:
            message = socket_status.recv(flags=zmq.NOBLOCK)
        except zmq.ZMQError: 
            pass

        if (message != ""):
            stdscr.addstr(12, 2, "Received message: %s" % (message))
            stdscr.clrtoeol()
            cmd = message.split(':');
            if (cmd[0] == "B"):
                new_buf = int(cmd[1])
                if (new_buf != buf):
                    buf = new_buf
                    redraw(stdscr)

curses.wrapper(main)
