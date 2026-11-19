"""Build the E5 integration against real lwIP and the deterministic E4 MAC fixture."""
from pathlib import Path
import subprocess

from e4_sources import BENCH, LWIP, LWIP_DHCP, LWIP_TCP, PORT, TRANSPORT


def main():
    root = Path(__file__).resolve().parents[2]
    out = root / 'output/Ethernet/e5_host'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test_e5_network.exe'
    port = PORT + TRANSPORT + BENCH + ['Tests/Ethernet/test_e5_network.c']
    lwip = LWIP + LWIP_DHCP + LWIP_TCP
    flags = ['/nologo', '/std:c11', '/utf-8', '/wd4103', '/wd4324', '/IPlatform/Ethernet',
             '/IThirdParty/lwip/src/include', '/DETH_LWIP_DHCP', '/DETH_LWIP_E5',
             '/Fo' + str(out) + '\\']
    for sources, warning in ((port, '/W4'), (lwip, '/W3')):
        subprocess.run(['cl', *flags, warning, '/WX', '/c', *sources], cwd=root, check=True)
    objects = [str(out / (Path(source).stem + '.obj')) for source in port + lwip]
    subprocess.run(['link', '/nologo', '/OUT:' + str(binary), *objects], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)


if __name__ == '__main__':
    main()
