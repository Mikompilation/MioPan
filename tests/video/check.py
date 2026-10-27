from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
renderer = root / 'zero_rom/zero2np/src/miopan/rendering/miopan_renderer.cpp'
source = renderer.read_text()
start = source.index('TextureEntry *GetVideoTexture(')
end = source.index('\nstruct TextureUploadSlice', start)
with tempfile.TemporaryDirectory(prefix='miopan-video-') as folder:
    folder = Path(folder)
    (folder / 'video-texture.inc').write_text(source[start:end])
    for name in ['movie-reset']:
        binary = folder / name
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread', '-I', str(root),
                        '-I', str(folder), str(root / f'tests/video/{name}.cpp'),
                        str(renderer.with_name('miopan_video.cpp')), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Movie texture reset checks passed.')
