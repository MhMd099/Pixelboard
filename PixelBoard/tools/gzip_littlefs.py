import gzip
import os
import shutil
from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SOURCE_DATA = PROJECT_DIR / "data"
GZIP_DATA = PROJECT_DIR / ".pio" / "littlefs_gzip_data"

COMPRESS_EXTENSIONS = {
    ".html",
    ".htm",
    ".css",
    ".js",
    ".json",
    ".svg",
    ".txt",
}


def prepare_gzip_data():
    if GZIP_DATA.exists():
        shutil.rmtree(GZIP_DATA)
    GZIP_DATA.mkdir(parents=True, exist_ok=True)
    env.Replace(PROJECT_DATA_DIR=str(GZIP_DATA), PROJECTDATA_DIR=str(GZIP_DATA))

    if not SOURCE_DATA.exists():
        return

    for src in SOURCE_DATA.rglob("*"):
        rel = src.relative_to(SOURCE_DATA)
        dst = GZIP_DATA / rel

        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue

        dst.parent.mkdir(parents=True, exist_ok=True)
        suffix = src.suffix.lower()

        if suffix in COMPRESS_EXTENSIONS:
            gz_dst = Path(str(dst) + ".gz")
            with src.open("rb") as src_file, gzip.open(gz_dst, "wb", compresslevel=9) as gz_file:
                shutil.copyfileobj(src_file, gz_file)
        else:
            shutil.copy2(src, dst)


prepare_gzip_data()
