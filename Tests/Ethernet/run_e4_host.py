"""Compile the real lwIP port with a simulated Ethernet MAC in an x64 VS prompt."""
from pathlib import Path
import subprocess

from e4_sources import LWIP, PORT


def main():
    root = Path(__file__).resolve().parents[2]
    out = root / 'output/Ethernet/e4_host'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test_e4_network.exe'
    # Intentional split pack pragmas in upstream headers and aligned frame arrays.
    flags = ['/nologo', '/std:c11', '/utf-8', '/wd4103', '/wd4324', '/IPlatform/Ethernet',
             '/IThirdParty/lwip/src/include', '/Fo' + str(out) + '\\']
    for file in PORT + ['Tests/Ethernet/test_e4_network.c']:
        subprocess.run(['cl', *flags, '/W4', '/WX', '/c', file], cwd=root, check=True)
    subprocess.run(['cl', *flags, '/W3', '/WX', '/c', *LWIP], cwd=root, check=True)
    objects = [str(out / (Path(file).stem + '.obj')) for file in
               [*PORT, 'Tests/Ethernet/test_e4_network.c', *LWIP]]
    subprocess.run(['link', '/nologo', '/OUT:' + str(binary), *objects], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)


if __name__ == '__main__':
    main()
