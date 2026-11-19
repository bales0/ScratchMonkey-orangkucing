"""Build and run host-side firmware regressions; no programmer is opened."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX'))
args = parser.parse_args()
cxx = args.cxx or shutil.which('g++')
if not cxx and Path('C:/msys64/ucrt64/bin/g++.exe').exists():
    cxx = 'C:/msys64/ucrt64/bin/g++.exe'
if not cxx:
    parser.error('g++ not found; pass --cxx or set CXX')
build = ROOT / '.pio' / 'tests-host'
build.mkdir(parents=True, exist_ok=True)
exe = build / ('firmware_tests.exe' if os.name == 'nt' else 'firmware_tests')
env = dict(os.environ)
env['PATH'] = str(Path(cxx).resolve().parent) + os.pathsep + env.get('PATH', '')
sources = [ROOT / 'Tests/host/firmware_tests.cpp'] + [ROOT / f for f in (
    'SMoCommand.cpp', 'SMoXPROG.cpp', 'SMoTPI.cpp', 'SMoPDI.cpp', 'SMoHVPP.cpp',
    'SMoHVSP.cpp', 'SMoISP.cpp', 'SMoGeneral.cpp', 'SMoValidation.cpp', 'SMoSession.cpp', 'ScratchMonkey.ino')]
sources += [ROOT / 'Tests/host/protocol_tests.cpp']
subprocess.run([cxx, '-std=c++17', '-O1', '-g', '-static', '-D__AVR_ATmega32__',
    '-DF_CPU=7372800L', '-I' + str(ROOT / 'Tests/host/mocks'), '-I' + str(ROOT),
    '-x', 'c++', *(str(s) for s in sources), '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, timeout=30, env=env)
