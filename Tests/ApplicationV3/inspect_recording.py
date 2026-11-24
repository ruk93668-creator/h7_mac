"""Read-only structural validation for AVI produced by CM7/Service/mjpeg_avi.c."""

import argparse
import json
import os
import struct


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(stream):
    size = os.fstat(stream.fileno()).st_size
    head = stream.read(224)
    require(len(head) == 224 and head[:4] == b"RIFF" and head[8:12] == b"AVI ", "invalid RIFF/AVI")
    require(u32(head, 4) + 8 == size, "RIFF length differs from file length")
    require(head[20:28] == b"hdrlavih" and head[108:116] == b"vidsMJPG" and head[220:] == b"movi",
            "unexpected recorder header")
    require(u32(head, 44) & 0x10, "AVI header does not declare a completed index")

    frames = u32(head, 48)
    scale, rate = u32(head, 128), u32(head, 132)
    require(0 < frames <= 65536 and frames == u32(head, 140), "frame counts differ or exceed recorder limit")
    require(scale > 0 and rate > 0, "invalid AVI rate")
    movi_end = 220 + u32(head, 216)
    require(224 < movi_end and movi_end + 8 + 16 * frames == size, "invalid movi or idx1 extent")

    index = []
    for _ in range(frames):
        position = stream.tell()
        chunk = stream.read(8)
        require(len(chunk) == 8 and chunk[:4] == b"00dc", f"invalid video chunk at {position}")
        payload = u32(chunk, 4)
        end = position + 8 + payload + (payload & 1)
        require(4 <= payload <= 70000 and end <= movi_end, f"invalid frame size at {position}")
        require(stream.read(2) == b"\xff\xd8", f"missing JPEG SOI at {position}")
        stream.seek(position + 8 + payload - 2)
        require(stream.read(2) == b"\xff\xd9", f"missing JPEG EOI at {position}")
        stream.seek(end)
        index.append((position - 220, payload))
    require(stream.tell() == movi_end and max(payload for _, payload in index) == u32(head, 60),
            "movi length or largest frame differs from the header")

    require(stream.read(8) == b"idx1" + struct.pack("<I", 16 * frames), "invalid idx1 header")
    for offset, payload in index:
        require(stream.read(16) == struct.pack("<4sIII", b"00dc", 0x10, offset, payload),
                f"idx1 does not match the video chunk at {offset}")
    return {
        "structure": "PASS", "file_bytes": size, "movi_bytes": movi_end - 224,
        "frames": frames, "index_entries": len(index), "jpeg_payload_bytes": sum(n for _, n in index),
        "width": u32(head, 64), "height": u32(head, 68), "header_fps": rate / scale,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("avi")
    args = parser.parse_args()
    try:
        with open(args.avi, "rb") as stream:
            print(json.dumps(inspect(stream), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
