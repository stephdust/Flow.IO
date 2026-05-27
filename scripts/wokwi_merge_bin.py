from pathlib import Path
import subprocess

Import("env")


def _parse_int(value):
    text = str(value or "").strip()
    if not text:
        raise ValueError("empty numeric value")
    return int(text, 0)


def _partition_offsets(partitions_csv):
    app_offset = None
    spiffs_offset = None

    for raw_line in Path(partitions_csv).read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        cols = [c.strip() for c in raw_line.split(",")]
        if len(cols) < 5:
            continue

        name, ptype, subtype, offset = cols[0], cols[1], cols[2], cols[3]
        if not offset:
            continue

        if name == "spiffs" or subtype == "spiffs":
            spiffs_offset = _parse_int(offset)
        elif ptype == "app" and subtype in ("factory", "ota_0"):
            app_offset = _parse_int(offset)

    if app_offset is None:
        app_offset = 0x10000
    if spiffs_offset is None:
        raise RuntimeError("unable to find SPIFFS offset in partition table")

    return app_offset, spiffs_offset


def _merge_flash_image(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    fw = build_dir / "firmware.bin"
    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    spiffs = build_dir / "spiffs.bin"
    merged = build_dir / "wokwi-merged.bin"

    required = (fw, bootloader, partitions, spiffs)
    missing = [p.name for p in required if not p.exists()]
    if missing:
        print(
            "[wokwi-merge] skip merge (missing: {})".format(", ".join(missing))
        )
        return

    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV")
    if not partitions_csv:
        print("[wokwi-merge] skip merge (PARTITIONS_TABLE_CSV not defined)")
        return

    try:
        app_offset, spiffs_offset = _partition_offsets(partitions_csv)
    except Exception as exc:
        print(f"[wokwi-merge] skip merge ({exc})")
        return

    python_exe = env.subst("$PYTHONEXE")
    uploader = env.subst("$UPLOADER")
    cmd = [
        python_exe,
        uploader,
        "--chip",
        env.subst("$BOARD_MCU"),
        "merge_bin",
        "-o",
        str(merged),
        "--flash_mode",
        "keep",
        "--flash_freq",
        "keep",
        "--flash_size",
        "keep",
        "0x0",
        str(bootloader),
        "0x8000",
        str(partitions),
        hex(app_offset),
        str(fw),
        hex(spiffs_offset),
        str(spiffs),
    ]
    print("[wokwi-merge] run: {}".format(" ".join(cmd)))
    result = subprocess.run(cmd, check=False)
    if result.returncode != 0:
        raise RuntimeError("[wokwi-merge] esptool merge_bin failed")

    print(f"[wokwi-merge] merged image: {merged.relative_to(Path(env.subst('$PROJECT_DIR')))}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _merge_flash_image)
env.AddPostAction("$BUILD_DIR/spiffs.bin", _merge_flash_image)
