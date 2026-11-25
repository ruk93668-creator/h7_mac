"""Run E6 with upstream lwIP and a simulated Ethernet peer on an x64 VS prompt."""
from pathlib import Path
import subprocess
import sys

from e4_sources import HTTP, LWIP, LWIP_DHCP, LWIP_TCP, PORT, TRANSPORT


def main():
    root = Path(__file__).resolve().parents[2]
    dynamic = '--dynamic' in sys.argv
    qvga = '--qvga' in sys.argv
    if qvga and not dynamic:
        raise SystemExit('--qvga requires --dynamic')
    variant = 'e7_qvga_host' if qvga else 'e7_host' if dynamic else 'e6_host'
    out = root / 'output/Ethernet' / variant
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test_e6_network.exe'
    port = PORT + TRANSPORT + HTTP + ['Tests/Ethernet/test_e6_network.c']
    lwip = LWIP + LWIP_DHCP + LWIP_TCP
    flags = ['/nologo', '/std:c11', '/utf-8', '/wd4103', '/wd4324', '/IPlatform/Ethernet',
             '/ICM7/Service/Ethernet', '/IThirdParty/lwip/src/include', '/DETH_LWIP_DHCP', '/DETH_LWIP_E5', '/DETH_MJPEG_FAST_SEND_TEST',
             '/Fo' + str(out) + '\\']
    for sources, warning in ((port, '/W4'), (lwip, '/W3')):
        subprocess.run(['cl', *flags, warning, '/WX', '/c', *sources], cwd=root, check=True)
    objects = [str(out / (Path(source).stem + '.obj')) for source in port + lwip]
    subprocess.run(['link', '/nologo', '/OUT:' + str(binary), *objects], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
    if dynamic:
        subprocess.run([str(binary), 'dynamic'], cwd=root, check=True)
        subprocess.run([str(binary), 'dynamic-fast'], cwd=root, check=True)
        test = out / 'test_e7_camera_provider.exe'
        sources = ['Tests/Ethernet/e7_camera_provider.c', 'Tests/Ethernet/test_e7_camera_provider.c']
        # Existing Camera headers contain legacy-encoded comments.
        camera_flags = ['/DETH_E7_CAMERA_QVGA'] if qvga else []
        subprocess.run(['cl', *flags, *camera_flags, '/wd4828', '/ICM7/Media/Inc', '/ICommon/Inc',
                        '/W4', '/WX', '/c', *sources],
                       cwd=root, check=True)
        subprocess.run(['link', '/nologo', '/OUT:' + str(test),
                        *(str(out / (Path(source).stem + '.obj')) for source in [*sources, HTTP[-1], "CM7/Service/Ethernet/eth_jpeg_copy.c"])],
                       cwd=root, check=True)
        subprocess.run([str(test)], cwd=root, check=True)


if __name__ == '__main__':
    main()
