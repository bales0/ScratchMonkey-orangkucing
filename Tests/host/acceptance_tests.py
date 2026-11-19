"""Public/fork AVRDUDE against actual firmware with simulated SPI/GPIO targets.

No physical serial port is accepted or opened by this harness.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from avrdude_tests import FirmwareProgrammer, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--public-avrdude', type=Path,
                        default=ROOT / '.pio/tools/avrdude-public-8.3/release/avrdude.exe')
    parser.add_argument('--public-config', type=Path,
                        default=ROOT / '.pio/tools/avrdude-public-8.3/release/avrdude.conf')
    parser.add_argument('--fork-avrdude', type=Path,
                        default=ROOT / '.pio/tools/avrdude/install/bin/avrdude.exe')
    parser.add_argument('--fork-config', type=Path,
                        default=ROOT / '.pio/tools/avrdude/install/etc/avrdude.conf')
    args = parser.parse_args()
    output = ROOT / '.pio/stabilization'
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for host, executable, config in (
        ('public', args.public_avrdude, args.public_config),
        ('fork', args.fork_avrdude, args.fork_config),
    ):
        for profile, protocol, part, signature in (
            ('stk500v2', 'isp', 't13', (0x1e, 0x90, 7)),
            ('scratchmonkey', 'isp', 't13', (0x1e, 0x90, 7)),
            ('stk500hvsp', 'hvsp', 't13', (0x1e, 0x90, 7)),
            ('scratchmonkey_hvsp', 'hvsp', 't13', (0x1e, 0x90, 7)),
            ('stk500pp', 'hvpp', 'm32', (0x1e, 0x95, 2)),
            ('scratchmonkey_pp', 'hvpp', 'm32', (0x1e, 0x95, 2)),
        ):
            name = f'{host}_{profile}'
            print('RUN', name, flush=True)
            simulator = FirmwareProgrammer(signature, protocol=protocol, trace_path=output / (name + '.trace'))
            with tempfile.TemporaryDirectory(dir=output) as directory:
                files = {memory: Path(directory) / (memory + '.bin') for memory in
                         ('signature', 'lfuse', 'hfuse', 'lock', 'calibration')}
                command = [str(executable), '-C', str(config), '-c', profile, '-p', part,
                           '-P', f'net:127.0.0.1:{simulator.port}', '-n']
                for memory, path in files.items():
                    command += ['-U', f'{memory}:r:{path}:r']
                error = None
                try:
                    with (output / (name + '.log')).open('wb') as log_file:
                        run = subprocess.Popen(command, stdout=log_file, stderr=subprocess.STDOUT)
                        try:
                            run.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            run.kill()
                            run.wait(timeout=3)
                            raise
                    simulator.finish()
                    log = (output / (name + '.log')).read_text(errors='replace')
                    assert run.returncode == 0, log
                    assert files['signature'].read_bytes() == bytes(signature)
                    assert files['lfuse'].read_bytes() == b'\x6a'
                    assert files['hfuse'].read_bytes() == b'\xff'
                    assert files['lock'].read_bytes() == b'\xff'
                    calibration = files['calibration'].read_bytes()
                    assert calibration and calibration[0] == 0x57
                    if part == 't13':
                        assert calibration == b'\x57\x54'
                    enter = dict(isp=0x10, hvsp=0x30, hvpp=0x20)[protocol]
                    assert ('command', enter) in simulator.events
                    assert not any(event in simulator.events for event in
                                   [('command', 0x50), ('command', 0x51)])
                    assert ('command', enter + 1) in simulator.events
                    assert 'RX 01000853544b3530305f32' in (output / (name + '.trace')).read_text()
                except Exception as exc:
                    error = f'{exc}; events={simulator.events}; server={simulator.error!r}'
                    print(error, flush=True)
                finally:
                    if simulator.process.poll() is None:
                        simulator.process.kill()
                        simulator.process.wait()
                results.append(dict(host=host, profile=profile, mode=protocol,
                                    evidence='SIMULATED', command=command,
                                    expected='signature/fuse/lock/calibration; standard ENTER/LEAVE; no XPROG',
                                    status='FAIL' if error else 'PASS', detail=error))
                print(results[-1]['status'], name, flush=True)
    (output / 'compatibility-matrix.json').write_text(json.dumps(results, indent=2) + '\n')
    failed = [result for result in results if result['status'] != 'PASS']
    for result in failed:
        print(result['host'], result['profile'], result['detail'])
    if failed:
        raise SystemExit(1)
    for profile,commands in (('stk500v2',(0x10,0x11)),('stk500hvsp',(0x2D,0x30,0x31)),
                             ('stk500pp',(0x2D,0x20,0x21))):
        packets=[]
        for host in ('public','fork'):
            trace=(output/f'{host}_{profile}.trace').read_text().splitlines()
            tx=[bytes.fromhex(line[3:]) for line in trace if line.startswith('TX ')]
            packets.append([packet for packet in tx if packet[0] in commands])
        assert packets[0]==packets[1], f'{profile}: public/fork entry/leave/control-stack differ'
    print('PASS public/fork standard ENTER/LEAVE and 32-byte control stacks are identical')
    results.append(dict(host='public+fork',profile='stk500v2/stk500hvsp/stk500pp',
                        mode='standard-protocol-invariants',evidence='SIMULATED',
                        command=['compare captured ENTER/LEAVE/control-stack packets'],
                        expected='identical bytes',status='PASS',detail=None))
    (output / 'compatibility-matrix.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
