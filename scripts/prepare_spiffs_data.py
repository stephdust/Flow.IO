from pathlib import Path
import gzip
import shutil

Import("env")


def _gzip_file(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with src.open("rb") as in_file, gzip.GzipFile(filename="", mode="wb", fileobj=dst.open("wb"), mtime=0) as out_file:
        shutil.copyfileobj(in_file, out_file)


project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
src_dir = project_dir / "data"
staging_dir = build_dir / "spiffs_data"

if src_dir.exists():
    if staging_dir.exists():
        shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)

    compressed_sources = {
        Path("webinterface/index.html"): Path("webinterface/index.html.gz"),
        Path("webinterface/sh.html"): Path("webinterface/sh.html.gz"),
        Path("webinterface/app.css"): Path("webinterface/app.css.gz"),
        Path("webinterface/app.js"): Path("webinterface/app.js.gz"),
        Path("webinterface/light.html"): Path("webinterface/light.html.gz"),
        Path("webinterface/light.css"): Path("webinterface/light.css.gz"),
        Path("webinterface/light.js"): Path("webinterface/light.js.gz"),
        Path("webinterface/runtimeui.json"): Path("webinterface/runtimeui.json.gz"),
        Path("webinterface/cfgdocs.fr.json"): Path("webinterface/cfgdocs.jz"),
        Path("webinterface/cfgmods.fr.json"): Path("webinterface/cfgmods.jz"),
    }
    generated_outputs = set(compressed_sources.values())

    for path in src_dir.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(src_dir)
        if rel in compressed_sources or rel in generated_outputs:
            continue
        dst = staging_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dst)

    for src_rel, dst_rel in compressed_sources.items():
        src = src_dir / src_rel
        if src.exists():
            _gzip_file(src, staging_dir / dst_rel)

    env.Replace(PROJECT_DATA_DIR=str(staging_dir), PROJECTDATA_DIR=str(staging_dir))
    print(f"[prepare_spiffs_data] staging {src_dir} -> {staging_dir}")
