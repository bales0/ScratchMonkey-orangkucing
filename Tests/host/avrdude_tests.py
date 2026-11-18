"""Exercise the real AVRDUDE against a loopback-only STK500v2 simulator."""
import argparse
import re
from pathlib import Path
import socket
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]

class Programmer:
    def __init__(self, signature, signature_address=0, *, xprog=True, fault=None, page_size=512):
        self.signature = bytes(signature)
        self.memory = dict(enumerate(self.signature, signature_address))
        self.events = []
        self.xprog = xprog
        self.fault = fault
        self.mode = None
        self.page_size = page_size
        self.ee_page_size = 32
        self.page_buffer = {}
        self.commits = []
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
                        if reply is None:
                            return  # simulate an acknowledgement lost after a write
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
            if not self.xprog:
                return bytes([cmd, 0xc9])
            self.mode = body[1]
            return bytes([cmd, 0])
        if cmd == 0x50:
            sub = body[1]
            if sub == 7:
                self.events.append(('param', body[2], int.from_bytes(body[3:], 'big')))
                if body[2] == 5:
                    self.page_size = int.from_bytes(body[3:], 'big')
                if body[2] == 2:
                    self.ee_page_size = int.from_bytes(body[3:], 'big')
            elif sub == 5:
                address = int.from_bytes(body[3:7], 'big')
                count = int.from_bytes(body[7:9], 'big')
                if self.fault == 'short_read':
                    return bytes([cmd, sub, 0])
                if self.fault == 'wrong_subcommand':
                    return bytes([cmd, 4, 0])
                if self.fault == 'wrong_opcode':
                    return bytes([0x51, 0])
                data = bytes(self.memory.get(address + i, 0xff) for i in range(count))
                if self.fault == 'verify_corrupt' and 0x800000 <= address < 0x900000:
                    data = bytes([data[0] ^ 1]) + data[1:]
                return bytes([cmd, sub, 0]) + data
            elif sub == 4:
                address = int.from_bytes(body[4:8], 'big')
                count = int.from_bytes(body[8:10], 'big')
                data = body[10:]
                assert len(data) == count
                self.events.append(('write', body[2], address, count, body[3]))
                if self.fault == 'write_timeout':
                    return bytes([cmd, sub, 3])
                if self.fault == 'short_write':
                    return bytes([cmd, sub])
                if self.fault == 'lost_ack':
                    return None
                if self.mode == 0 and body[2] in (1, 2, 3, 6):
                    size = self.ee_page_size if body[2] == 3 else self.page_size
                    base = address & ~(size - 1)
                    assert address + count <= base + size, 'packet crosses a page boundary'
                    self.page_buffer.update((a % size, v) for a, v in enumerate(data, address))
                    if body[3] & 2:
                        self.commits.append((body[2], base))
                        for i in range(size):
                            old = 0xff if body[3] & 1 else self.memory.get(base + i, 0xff)
                            self.memory[base + i] = old & self.page_buffer.get(i, 0xff)
                        self.page_buffer.clear()
                else:
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


class FirmwareProgrammer(Programmer):
    """Forward every request to the real firmware, with mocked electrical IO."""
    def __init__(self, signature, signature_address=0, *, protocol, **kwargs):
        self.process = subprocess.Popen(
            [str(ROOT / '.pio/tests-host/firmware_tests.exe'), '--bridge', protocol],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        super().__init__(signature, signature_address, **kwargs)

    def exchange(self, body):
        self.process.stdin.write(len(body).to_bytes(2, 'big') + body)
        self.process.stdin.flush()
        header = self.process.stdout.read(2)
        assert len(header) == 2, 'firmware bridge closed'
        count = int.from_bytes(header, 'big')
        reply = self.process.stdout.read(count)
        assert len(reply) == count, 'short firmware bridge reply'
        return reply

    def handle(self, body):
        self.events.append(('command', body[0]))
        return self.exchange(body)

    def finish(self):
        try:
            super().finish()
            self.commit_count = int.from_bytes(self.exchange(b''), 'big')
            self.process.stdin.close()
            self.process.wait(timeout=3)
            assert self.process.returncode == 0, self.process.stderr.read().decode(errors='replace')
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--avrdude', type=Path, default=ROOT / '.pio/tools/avrdude/install/bin/avrdude.exe')
    parser.add_argument('--config', type=Path, default=ROOT / '.pio/tools/avrdude/install/etc/avrdude.conf')
    args = parser.parse_args()
    build = ROOT / '.pio/tests-host'
    build.mkdir(parents=True, exist_ok=True)
    listing = subprocess.run([str(args.avrdude), '-C', str(args.config), '-c', '?'],
                             capture_output=True, timeout=10)
    names = re.findall(r'^\s+(\S+)\s+=', (listing.stdout + listing.stderr).decode(errors='replace'), re.M)
    assert names and names == sorted(names, key=lambda value: (value.casefold(), value))
    print('PASS programmer list sorted by short ID, including aliases')

    def exercise(name, profile, part, signature, signature_address=0, operations=(), extra_config=None,
                 *, expected_success=True, factory=Programmer, **options):
        print(f'RUN {name}', flush=True)
        simulator = factory(signature, signature_address, **options)
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
        assert (result.returncode == 0) == expected_success, f'{name}:\n{log}'
        exercise.simulator = simulator
        return simulator.events

    for profile, part, signature, enter in (
        ('stk500v2', 'm32', (0x1e, 0x95, 0x02), 0x10),
        ('stk500pp', 'm32', (0x1e, 0x95, 0x02), 0x20),
        ('scratchmonkey_pp', 'm32', (0x1e, 0x95, 0x02), 0x20),
        ('stk500hvsp', 't85', (0x1e, 0x93, 0x0b), 0x30),
        ('stk500v2', 't13', (0x1e, 0x90, 0x07), 0x10),
        ('stk500', 't13', (0x1e, 0x90, 0x07), 0x10),
        ('scratchmonkey', 't13', (0x1e, 0x90, 0x07), 0x10),
        ('stk500hvsp', 't13', (0x1e, 0x90, 0x07), 0x30),
        ('scratchmonkey_hvsp', 't13', (0x1e, 0x90, 0x07), 0x30),
    ):
        events = exercise(f'{profile}_{part}', profile, part, signature)
        assert ('command', enter) in events
        assert not any(e[0] == 'mode' for e in events), profile
        if enter == 0x10:
            assert ('command', 0x30) not in events and ('command', 0x20) not in events
        print(f'PASS {profile}/{part}: explicitly selected programming path')

    with tempfile.TemporaryDirectory(dir=build) as directory:
        fixture = Path(directory) / 'data.bin'
        fixture.write_bytes(bytes(range(256)) * 2 + bytes(range(88)))
        events = exercise('pdi', 'stk500v2', 'x128a1', (0x1e, 0x97, 0x4c), 0x1000090,
                          ('-U', f'flash:w:{fixture}:r'))
        assert ('mode', 0) in events
        assert ('param', 5, 512) in events
        assert ('param', 2, 32) in events
        writes = [e for e in events if e[0] == 'write']
        assert len(writes) == 4 and all(0 < e[3] <= 256 for e in writes), writes
        assert [e[4] for e in writes] == [0, 2, 0, 2], writes
        assert len(exercise.simulator.commits) == 2
        print('PASS PDI: real page buffer, exactly two commits for 600 bytes, write/verify')

        # Test 32..512-byte pages through real AVRDUDE using inherited parts.
        overlay = Path(directory) / 'pages.conf'
        for size in (32, 64, 128, 256, 512):
            overlay.write_text(f'part parent "x128a1"\n id = "xpage";\n'
                               f' memory "flash"\n page_size = {size};\n ;\n;\n', encoding='utf-8')
            fixture.write_bytes(bytes(range(256)) * 2 + bytes(range(88)))
            exercise(f'pdi_page_{size}', 'stk500v2', 'xpage', (0x1e, 0x97, 0x4c), 0x1000090,
                     ('-U', f'flash:w:{fixture}:r'), extra_config=overlay)
            assert len(exercise.simulator.commits) == (600 + size - 1) // size
        print('PASS PDI: page sizes 32/64/128/256/512 with final partial page')

        # Sparse input spanning the application/boot boundary, above 64 KiB.
        boundary = Path(directory) / 'boundary.hex'
        records = []
        upper = None
        for address in range(0x1ff00, 0x20158, 16):
            if upper != address >> 16:
                upper = address >> 16
                record = bytes([2, 0, 0, 4]) + upper.to_bytes(2, 'big')
                records.append(':' + (record + bytes([-sum(record) & 255])).hex().upper())
            data = bytes((address + i) & 255 for i in range(min(16, 0x20158-address)))
            record = bytes([len(data), (address >> 8) & 255, address & 255, 0]) + data
            records.append(':' + (record + bytes([-sum(record) & 255])).hex().upper())
        boundary.write_text('\n'.join(records + [':00000001FF']) + '\n', encoding='ascii')
        exercise('pdi_app_boot', 'stk500v2', 'x128a1', (0x1e, 0x97, 0x4c), 0x1000090,
                 ('-U', f'flash:w:{boundary}:i'))
        assert exercise.simulator.commits == [(1, 0x81fe00), (2, 0x820000)]
        print('PASS PDI: application/boot boundary uses separate page types and commits')

        for fault in ('short_read', 'wrong_subcommand', 'wrong_opcode', 'write_timeout',
                      'short_write', 'verify_corrupt', 'lost_ack'):
            exercise(f'error_{fault}', 'stk500v2', 'x128a1', (0x1e, 0x97, 0x4c), 0x1000090,
                     ('-U', f'flash:w:{fixture}:r'), expected_success=False, fault=fault)
            if fault == 'lost_ack':
                assert sum(e[0] == 'write' for e in exercise.simulator.events) == 1
        print('PASS XPROG: truncated/mismatched replies, timeout, failed verify and lost ACK return failure')

        events = exercise('original_stk500_no_xprog', 'stk500v2', 'x128a1', (0x1e, 0x97, 0x4c),
                          expected_success=False, xprog=False)
        assert ('command', 0x50) not in events
        print('PASS original STK500: rejected probe prevents XPROG programming')

        exercise('firmware_pdi', 'stk500v2', 'x128a1', (0x1e, 0x97, 0x4c), 0x1000090,
                 ('-U', f'flash:w:{fixture}:r'), factory=FirmwareProgrammer, protocol='pdi')
        assert exercise.simulator.commit_count == 2
        print('PASS real AVRDUDE -> compiled firmware -> SPI -> NVM buffer: PDI 512B pages and verify')

        fixture.write_bytes(b'\xfe')
        events = exercise('tpi', 'stk500v2', 't10', (0x1e, 0x90, 0x03), 0x3fc0,
                          ('-U', f'fuse:w:{fixture}:r'))
        assert ('mode', 2) in events
        erase = next(i for i, e in enumerate(events) if e[0:2] == ('erase', 9))
        write = next(i for i, e in enumerate(events) if e[0:2] == ('write', 4))
        assert erase < write
        print('PASS TPI: XPROG, configuration erase before byte fuse write and verification')

        exercise('firmware_tpi', 'stk500v2', 't10', (0x1e, 0x90, 0x03), 0x3fc0,
                 ('-U', f'fuse:w:{fixture}:r'), factory=FirmwareProgrammer, protocol='tpi')
        print('PASS real AVRDUDE -> compiled firmware -> TPI: fuse write and verify')

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
