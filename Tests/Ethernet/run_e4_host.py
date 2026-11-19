"""Compile the real lwIP port with a simulated Ethernet MAC in an x64 VS prompt."""
from pathlib import Path
import subprocess
import sys

from e4_sources import LWIP, LWIP_DHCP, PORT


def main():
    root = Path(__file__).resolve().parents[2]
    dhcp = '--dhcp' in sys.argv
    sources = LWIP + (LWIP_DHCP if dhcp else [])
    out = root / 'output/Ethernet' / ('e42_host' if dhcp else 'e4_host')
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test_e4_network.exe'
    # Intentional split pack pragmas in upstream headers and aligned frame arrays.
    flags = ['/nologo', '/std:c11', '/utf-8', '/wd4103', '/wd4324', '/IPlatform/Ethernet',
             '/IThirdParty/lwip/src/include', '/Fo' + str(out) + '\\']
    if dhcp:
        flags += ['/DETH_LWIP_DHCP']
    for file in PORT + ['Tests/Ethernet/test_e4_network.c']:
        subprocess.run(['cl', *flags, '/W4', '/WX', '/c', file], cwd=root, check=True)
    subprocess.run(['cl', *flags, '/W3', '/WX', '/c', *sources], cwd=root, check=True)
    objects = [str(out / (Path(file).stem + '.obj')) for file in
               [*PORT, 'Tests/Ethernet/test_e4_network.c', *sources]]
    subprocess.run(['link', '/nologo', '/OUT:' + str(binary), *objects], cwd=root, check=True)
    scenarios = ('static', 'lease', 'no_server', 'fallback', 'resources') if dhcp else ('static',)
    for scenario in scenarios:
        subprocess.run([str(binary), scenario], cwd=root, check=True)


if __name__ == '__main__':
    main()
