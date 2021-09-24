#!/usr/bin/env python3

import curses
import zmq
import logging
import time

BUF_Y = 5
UI_CMD = "ipc:///tmp/nojobuck_cmd"
UI_STATUS = "ipc:///tmp/nojobuck_status"

current_delay = 0
delay_setting = 0
buf = 0
last_redraw = 0.0

def buf_progress(stdscr):
    global buf

    logging.debug('redraw buf at: %d/100' % (buf))
    # Use an odd width so there's a middle:
    w = curses.COLS - 1
    if ((w % 2) == 0):
        w -= 1
    # find the middle
    m = int(curses.COLS / 2)

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
    global current_delay
    global delay_setting
    global last_redraw

    now = time.time()
    logging.debug('%.2f do redraw' % (now))
    last_redraw = now

    #stdscr.clear()
    height, width = stdscr.getmaxyx()
    stdscr.addstr(2, 2, "Delay Setting: %.2f" % (delay_setting/1000.0), curses.A_REVERSE)
    stdscr.clrtoeol()
    stdscr.addstr(2, curses.COLS - 21,
                  "Current Delay: %.2f" % (current_delay/1000.0), curses.A_REVERSE)
    stdscr.clrtoeol()
    buf_progress(stdscr);
    stdscr.refresh()

def main(stdscr):
    global current_delay
    global delay_setting
    global buf

    logging.basicConfig(filename='log',level=logging.INFO)

    socket_cmd = zmq.Context().socket(zmq.PUSH)
    socket_cmd.connect (UI_CMD)

    socket_status = zmq.Context().socket(zmq.SUB)
    socket_status.connect (UI_STATUS)
    socket_status.setsockopt_string(zmq.SUBSCRIBE, "") # subscribe to everything

    curses.curs_set(False)
    stdscr.nodelay(True)
    redraw(stdscr)

    new_current_delay = current_delay
    new_delay_setting = delay_setting
    new_buf = buf

    while True:
        if (current_delay == 0):
            logging.debug('Current delay is 0; send CURRENT DELAY query');
            socket_cmd.send(b"C:");
        if (delay_setting == 0):
            logging.debug('Delay setting is 0; send DELAY SETTING query');
            socket_cmd.send(b"D:");
        if (buf == 0):
            logging.debug('Buffer is 0; send BUFFER query');
            socket_cmd.send(b"B:");

        message = ""
        try:
            message = socket_status.recv(flags=zmq.NOBLOCK).decode()
        except zmq.ZMQError: 
            pass
        if (message != ""):
            logging.debug('Received message: "%s"' % (message))
            stdscr.addstr(12, 2, "Received message: %s" % (message))
            stdscr.clrtoeol()
            cmd = message.split(':');
            if (cmd[0] == "B"):
                new_buf = int(cmd[1])
                if (new_buf != buf):
                    logging.debug('Parsed as new buff: %d' % (new_buf))
                    buf = new_buf
                    redraw(stdscr)
            if (cmd[0] == "D"):
                new_delay_setting = int(cmd[1])
                if (new_delay_setting != delay_setting):
                    logging.debug('Parsed as new delay setting: %d' % (new_delay_setting))
                    delay_setting = new_delay_setting
                    redraw(stdscr)
            if (cmd[0] == "C"):
                new_current_delay = int(cmd[1])
                if (new_current_delay != current_delay):
                    logging.debug('Parsed as new current delay: %d' % (new_current_delay))
                    current_delay = new_current_delay
                    redraw(stdscr)

        ui_delay = delay_setting;

        c = stdscr.getch()
        if (c == ord('k')) or (c == curses.KEY_UP):
            ui_delay = delay_setting + 10
            logging.debug('key up; ui_delay: %d' % (ui_delay))
        if (c == ord('j')) or (c == curses.KEY_DOWN):
            ui_delay = delay_setting - 10
            logging.debug('key down; ui_delay: %d' % (ui_delay))
        if (c == curses.KEY_PPAGE) or (c == curses.KEY_RIGHT):
            ui_delay = delay_setting + 500
            logging.debug('key page up; ui_delay: %d' % (ui_delay))
        if (c == curses.KEY_NPAGE) or (c == curses.KEY_LEFT):
            ui_delay = delay_setting - 500
            logging.debug('key page down; ui_delay: %d' % (ui_delay))
        if c == ord('q'):
            break  # Exit the while loop

        if (ui_delay < 0):
            ui_delay = 0
        if (ui_delay != delay_setting):
            logging.debug('Set new delay: %d' % (ui_delay))
            socket_cmd.send(b"D:%d" % (ui_delay))
        else:
           time.sleep(0.05)

curses.wrapper(main)
