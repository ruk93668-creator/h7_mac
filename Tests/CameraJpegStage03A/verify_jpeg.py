"""Validate an exported frame; synthetic tests are never hardware evidence."""
from pathlib import Path
import argparse
import hashlib
import io
import json
import re
import warnings
from PIL import Image, ImageFile


def read_export(path, address=0xD0000400, length=None):
    path = Path(path)
    if length is not None and not 0 < length <= 262140:
        raise ValueError("length must be 1..262140 bytes")
    if path.suffix.lower() == ".txt":
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        inside = False
        complete = False
        data = bytearray()
        for line in lines:
            line = line.strip()
            if line == "JPEG_DATA_BEGIN":
                if inside or data:
                    raise ValueError("multiple debugger dumps")
                inside = True
            elif line == "JPEG_DATA_END" and inside:
                complete = True
                inside = False
            elif inside:
                match = re.fullmatch(r"JPG([0-9a-fA-F]{8}):([0-9a-fA-F]+)", line)
                if match:
                    if int(match[1], 16) != len(data):
                        raise ValueError("missing or overlapping debugger row")
                    data.extend(bytes.fromhex(match[2]))
                elif line:
                    raise ValueError("unexpected debugger output during dump")
        if not complete or not data or (length is not None and len(data) != length):
            raise ValueError("incomplete debugger dump")
        return bytes(data)
    if path.suffix.lower() != ".hex":
        data = path.read_bytes()
        if length is not None:
            if len(data) != length:
                raise ValueError("binary length differs from exported size_bytes")
        return data
    if length is None:
        raise ValueError("Intel HEX requires --length from size_bytes")
    memory = {}
    base = 0
    eof = False
    for line in path.read_text(encoding="ascii").splitlines():
        if not line.strip():
            continue
        if eof or not line.startswith(":"):
            raise ValueError("invalid record after EOF or missing colon")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError("Intel HEX length/checksum failure")
        size, offset, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        payload = record[4:-1]
        if kind == 0:
            for index, value in enumerate(payload):
                key = base + offset + index
                if key in memory and memory[key] != value:
                    raise ValueError("conflicting overlapping records")
                memory[key] = value
        elif kind in (2, 4) and size == 2:
            base = int.from_bytes(payload, "big") << (4 if kind == 2 else 16)
        elif kind == 1 and size == 0:
            eof = True
        elif kind not in (3, 5):
            raise ValueError("unsupported Intel HEX record")
    if not eof:
        raise ValueError("missing Intel HEX EOF")
    try:
        return bytes(memory[address + index] for index in range(length))
    except KeyError as error:
        raise ValueError("export contains a hole in the requested frame") from error


def validate(data, width=160, height=120):
    if not 4 <= len(data) <= 262140 or data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
        raise ValueError("SOI/EOI or receive-length boundary is invalid")
    ImageFile.LOAD_TRUNCATED_IMAGES = False
    with warnings.catch_warnings(record=True) as observed:
        warnings.simplefilter("always")
        with Image.open(io.BytesIO(data)) as image:
            if image.format != "JPEG" or image.size != (width, height):
                raise ValueError("decoded format or resolution mismatch")
            image.verify()
        with Image.open(io.BytesIO(data)) as image:
            image.load()
            preview = image.convert("RGB")
        if observed:
            raise ValueError("decoder warnings: " + "; ".join(str(item.message) for item in observed))
    return preview


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--address", type=lambda text: int(text, 0), default=0xD0000400)
    parser.add_argument("--length", type=lambda text: int(text, 0))
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=120)
    parser.add_argument("--source", choices=("hardware", "synthetic", "unverified"), default="unverified")
    args = parser.parse_args()
    data = read_export(args.input, args.address, args.length)
    preview = validate(data, args.width, args.height)
    jpg = args.input.with_suffix(".jpg")
    if jpg.resolve() != args.input.resolve():
        jpg.write_bytes(data)
    preview.save(args.input.with_name(args.input.stem + "_preview.png"))
    result = {"source": args.source, "input": str(args.input.resolve()), "size_bytes": len(data),
              "width": args.width, "height": args.height, "sha256": hashlib.sha256(data).hexdigest(),
              "decoded": True, "hardware_origin_is_user_assertion": args.source == "hardware"}
    args.input.with_name(args.input.stem + "_decode.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
