"""Compile the original palette asset into the same flash-resident data on both targets."""
import pathlib
import sys
source, destination = map(pathlib.Path, sys.argv[1:])
rows = source.read_text().splitlines()
if not rows or len(rows) > 48 or not 0 < len(rows[0]) <= 48 or any(len(r) != len(rows[0]) for r in rows):
    sys.exit("Invalid sprite dimensions")
palette = {".": 0, "x": 0xff633438, "r": 0xffdd533f, "o": 0xffff8653,
           "h": 0xffffc67a, "e": 0xffc2ffd4, "b": 0xff241d28}
pixels = [palette[p] for row in rows for p in row]
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text("#pragma once\n#include <cstdint>\nnamespace daemonos::presentation {\n"
    + f"inline constexpr unsigned sprite_width={len(rows[0])},sprite_height={len(rows)};\n"
    + "alignas(4) inline constexpr std::uint32_t sprite_data[]={\n"
    + ",".join(hex(p)+"U" for p in pixels) + "\n};\n}\n")
