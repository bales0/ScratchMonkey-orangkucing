"""Exercise the real AVRDUDE against a loopback-only STK500v2 simulator."""
import argparse
from pathlib import Path
import socket
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]

class Programmer:
    def __init__(self, signature, signature_address=0):
        self.signature = bytes(signature)
        self.memory = dict(enumerate(self.signature, signature_address))
        self.events = []
        self.error = None
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.listener.settimeout(10)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    @staticmethod
    def read_exact(conn, count):
        data = b''
        while len(data) < count:
            chunk = conn.recv(count - len(data))
            if not chunk:
                raise EOFError
            data += chunk
        return data

    def serve(self):
        try:
            with self.listener:
                conn, _ = self.listener.accept()
                with conn:
                    conn.settimeout(10)
                    while True:
                        header = self.read_exact(conn, 5)
                        assert header[0] == 0x1b and header[4] == 0x0e
                        length = int.from_bytes(header[2:4], 'big')
                        assert 0 < length <= 275
                        packet = self.read_exact(conn, length + 1)
                        checksum = 0
                        for value in header + packet:
                            checksum ^= value
                        assert checksum == 0
                        reply = self.handle(packet[:-1])
                        response = bytes([0x1b, header[1]]) + len(reply).to_bytes(2, 'big') + b'\x0e' + reply
                        checksum = 0
                        for value in response:
                            checksum ^= value
                        conn.sendall(response + bytes([checksum]))
        except EOFError:
            pass
        except Exception as exc:
            self.error = exc

    def handle(self, body):
        cmd = body[0]
        self.events.append(('command', cmd))
        if cmd == 1:
            return bytes([cmd, 0, 8]) + b'STK500_2'
        if cmd == 3:
            return bytes([cmd, 0, {0x90: 2, 0x91: 2, 0x92: 10, 0x94: 50, 0x95: 50, 0x98: 2}.get(body[1], 0)])
        if cmd == 0x51:
            self.events.append(('mode', body[1]))
            return bytes([cmd, 0])
        if cmd == 0x50:
            sub = body[1]
            if sub == 7:
                self.events.append(('param', body[2], int.from_bytes(body[3:], 'big')))
            elif sub == 5:
                address = int.from_bytes(body[3:7], 'big')
                count = int.from_bytes(body[7:9], 'big')
                return bytes([cmd, sub, 0]) + bytes(self.memory.get(address + i, 0xff) for i in range(count))
            elif sub == 4:
                address = int.from_bytes(body[4:8], 'big')
                count = int.from_bytes(body[8:10], 'big')
                data = body[10:]
                assert len(data) == count
                self.events.append(('write', body[2], address, count))
                self.memory.update(enumerate(data, address))
            elif sub == 3:
                self.events.append(('erase', body[2], int.from_bytes(body[3:7], 'big')))
            elif sub not in (1, 2):
                raise AssertionError(f'unexpected XPROG command {sub:#x}')
            return bytes([cmd, sub, 0])
        if cmd == 0x1b:
            return bytes([cmd, 0, self.signature[body[4]], 0])
        if cmd in (0x2b, 0x3b):
            return bytes([cmd, 0, self.signature[body[1]]])
        if cmd in (2, 4, 6, 0x10, 0x11, 0x20, 0x21, 0x2d, 0x30, 0x31):
            return bytes([cmd, 0])
        raise AssertionError(f'unexpected command {body.hex()}')

    def finish(self):
        self.thread.join(3)
        assert not self.thread.is_alive(), 'simulator did not close'
        if self.error:
            raise AssertionError(f'simulator: {self.error!r}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--avrdude', type=Path, default=ROOT / '.pio/tools/avrdude/install/bin/avrdude.exe')
    parser.add_argument('--config', type=Path, default=ROOT / '.pio/tools/avrdude/install/etc/avrdude.conf')
    args = parser.parse_args()
    build = ROOT / '.pio/tests-host'
    build.mkdir(parents=True, exist_ok=True)

    def exercise(name, profile, part, signature, signature_address=0, operations=(), extra_config=None):
        print(f'RUN {name}', flush=True)
        simulator = Programmer(signature, signature_address)
        cmd = [str(args.avrdude), '-C', str(args.config), '-c', profile, '-p', part,
               '-P', f'net:127.0.0.1:{simulator.port}', '-D', *operations]
        if extra_config:
            cmd += ['-C', '+' + str(extra_config)]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=20)
        except subprocess.TimeoutExpired as exc:
            detail = (exc.stdout or b'') + (exc.stderr or b'')
            raise AssertionError(f'{name} timeout; events={simulator.events}; server={simulator.error!r}\n{detail.decode(errors="replace")}') from exc
        simulator.finish()
        log = result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace')
        (build / f'{name}.log').write_text(log, encoding='utf-8')
        assert result.returncode == 0, f'{name}:\n{log}'
        return simulator.events

    for profile, part, signature, enter in (
        ('stk500v2', 'm32', (0x1e, 0x95, 0x02), 0x10),
        ('stk500pp', 'm32', (0x1e, 0x95, 0x02), 0x20),
        ('stk500hvsp', 't85', (0x1e, 0x93, 0x0b), 0x30),
    ):
        events = exercise(profile, profile, part, signature)
        assert ('command', enter) in events
        assert not any(e[0] == 'mode' for e in events), profile
        print(f'PASS {profile}: original programming path')

    with tempfile.TemporaryDirectory(dir=build) as directory:
        fixture = Path(directory) / 'data.bin'
        fixture.write_bytes(bytes(range(256)) * 2 + bytes(range(88)))
        events = exercise('pdi', 'stk500v2', 'x128a1', (0x1e, 0x97, 0x4c), 0x1000090,
                          ('-U', f'flash:w:{fixture}:r'))
        assert ('mode', 0) in events
        assert ('param', 5, 512) in events
        assert ('param', 2, 32) in events
        writes = [e for e in events if e[0] == 'write']
        assert len(writes) >= 4 and all(0 < e[3] <= 256 for e in writes), writes
        print('PASS PDI: XPROG, page parameters, multi-page flash write and verification')

        fixture.write_bytes(b'\xfe')
        events = exercise('tpi', 'stk500v2', 't10', (0x1e, 0x90, 0x03), 0x3fc0,
                          ('-U', f'fuse:w:{fixture}:r'))
        assert ('mode', 2) in events
        erase = next(i for i, e in enumerate(events) if e[0:2] == ('erase', 9))
        write = next(i for i, e in enumerate(events) if e[0:2] == ('write', 4))
        assert erase < write
        print('PASS TPI: XPROG, configuration erase before byte fuse write and verification')

        # A synthetic full-page configuration region exercises the other callback.
        overlay = Path(directory) / 'paged.conf'
        overlay.write_text('part parent "t10"\n id = "t10paged";\n memory "fuse"\n size = 16;\n page_size = 16;\n ;\n;\n', encoding='utf-8')
        fixture.write_bytes(b'\xfe' + b'\xff' * 15)
        events = exercise('tpi_paged', 'stk500v2', 't10paged', (0x1e, 0x90, 0x03), 0x3fc0,
                          ('-U', f'fuse:w:{fixture}:r'), extra_config=overlay)
        erase = next(i for i, e in enumerate(events) if e[0:2] == ('erase', 9))
        write = next(i for i, e in enumerate(events) if e[0:2] == ('write', 4))
        assert erase < write
        print('PASS TPI: configuration erase before paged fuse write and verification')

if __name__ == '__main__':
    main()
