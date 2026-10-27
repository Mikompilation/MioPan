from pathlib import Path
import json
import resource
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build'
entries = json.loads((build / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('/iop_voice.cpp'))
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
sources = sys.argv[2:] or ['silent-voice.cpp']
for source in sources:
    with tempfile.TemporaryDirectory(prefix='miopan-audio-') as folder:
        args = list(entry.get('arguments') or shlex.split(entry['command']))
        obj = Path(folder) / 'check.o'
        binary = obj.with_suffix('')
        args[args.index('-o') + 1] = str(obj)
        args[args.index('-c') + 1] = str(root / 'tests/audio' / source)
        args += ['-O0', '-ffunction-sections', '-fdata-sections']
        subprocess.run(args, cwd=entry['directory'], check=True)
        subprocess.run([args[0], str(obj), '-Wl,--gc-sections', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
