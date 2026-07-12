"""Merge the ESP32 program and LittleFS music into one 4 MB factory image."""

from pathlib import Path
import os
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".pio" / "build" / "esp32dev"
PIO_HOME = Path.home() / ".platformio"
PYTHON = PIO_HOME / "penv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
ESPTOOL = PIO_HOME / "packages" / "tool-esptoolpy" / "esptool.py"
BOOT_APP = (PIO_HOME / "packages" / "framework-arduinoespressif32" /
            "tools" / "partitions" / "boot_app0.bin")
OUTPUT = BUILD / "pixelboard-full.bin"


def require(path: Path) -> str:
    if not path.is_file():
        raise SystemExit(f"Fehlende Build-Datei: {path}")
    return str(path)


def main() -> None:
    command = [
        require(PYTHON), require(ESPTOOL), "--chip", "esp32", "merge_bin",
        "--output", str(OUTPUT), "--format", "raw",
        "--flash_mode", "dio", "--flash_freq", "40m",
        "--flash_size", "4MB", "--fill-flash-size", "4MB",
        "0x1000", require(BUILD / "bootloader.bin"),
        "0x8000", require(BUILD / "partitions.bin"),
        "0xE000", require(BOOT_APP),
        "0x10000", require(BUILD / "firmware.bin"),
        "0x210000", require(BUILD / "littlefs.bin"),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    size = OUTPUT.stat().st_size
    if size != 4 * 1024 * 1024:
        raise SystemExit(f"Falsche Gesamtgroesse: {size} Byte")
    print(f"Komplettes Flash-Abbild: {OUTPUT} ({size} Byte)")


if __name__ == "__main__":
    main()
