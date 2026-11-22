#!/usr/bin/env python3
"""Small POSIX USB-serial transport; standard library only (macOS/Linux)."""
import os
import select
import termios
import time
import tty


class Link:
    def __init__(self, port, timeout=10):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.timeout = timeout
        self.old = termios.tcgetattr(self.fd)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        attrs[4] = attrs[5] = termios.B115200
        attrs[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buffer = bytearray()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        termios.tcsetattr(self.fd, termios.TCSANOW, self.old)
        os.close(self.fd)

    def send(self, text):
        data = (text + "\n").encode("utf-8")
        deadline = time.monotonic() + self.timeout
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError("No se pudo escribir al dispositivo")
            try:
                data = data[os.write(self.fd, data):]
            except BlockingIOError:
                pass

    def read(self, count):
        deadline = time.monotonic() + self.timeout
        while len(self.buffer) < count:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError("Sin datos USB: comprueba el puerto y sal del selector con Tab")
            try:
                data = os.read(self.fd, max(4096, count - len(self.buffer)))
            except BlockingIOError:
                continue
            if not data:
                raise EOFError("Dispositivo desconectado")
            self.buffer.extend(data)
        result = bytes(self.buffer[:count])
        del self.buffer[:count]
        return result

    def line(self):
        line = bytearray()
        while len(line) < 1024:
            ch = self.read(1)
            if ch == b"\n":
                return bytes(line).rstrip(b"\r")
            line.extend(ch)
        raise ValueError("Linea USB demasiado larga")
