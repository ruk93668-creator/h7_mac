"""Reattach archived qualification code to the current Camera in historical tests."""
from pathlib import Path
import re


def build_camera_fixture(root, output):
    fixture = Path(__file__).with_name('camera_qualification_fixture.inc')
    segments = re.split(r'^// @fixture (\w+)\n', fixture.read_text(encoding='utf-8'), flags=re.M)[1:]
    sections = dict(zip(segments[::2], segments[1::2]))
    camera = (root / 'CM7/Application/app_camera.c').read_bytes().decode('gb18030').replace('\r\n', '\n')

    # Fail the test build if a required Camera boundary has moved or become ambiguous.
    def insert(anchor, section, after=False):
        nonlocal camera
        addition = sections.pop(section)
        if camera.count(anchor) != 1:
            raise ValueError(f'Historical Camera fixture anchor changed: {section}')
        camera = camera.replace(anchor, anchor + addition if after else addition + anchor, 1)

    insert('#include "video_config.h"', 'include')
    insert('    bool round_open;', 'fields')
    insert('static SysResult_t App_CameraAdvanceStop(void);\n', 'declaration', after=True)
    stop_confirmation = sections['stop_confirmed']
    cleared_stop = '            s_camera.stopping = false;\n            s_camera.running = false;\n'
    insert(cleared_stop, 'stop_confirmed', after=True)
    insert(cleared_stop + stop_confirmation + '        }\n', 'stop_result', after=True)
    insert('static SysResult_t App_CameraStartResource(void)\n{\n    SysResult_t result;\n', 'start_begin', after=True)
    insert('    else if (s_camera.prepared)\n        s_camera.stopping = true;\n', 'start_end', after=True)
    camera += sections.pop('adapter')
    if sections:
        raise ValueError(f'Unused historical Camera fixture sections: {list(sections)}')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(camera.encode('utf-8'))
    return output
