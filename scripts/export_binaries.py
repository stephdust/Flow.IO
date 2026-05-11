from pathlib import Path
import shutil

Import("env")


def _binary_dir():
    project_dir = Path(env.subst("$PROJECT_DIR"))
    out_dir = project_dir / "binary"
    out_dir.mkdir(exist_ok=True)
    return out_dir


def _copy_if_exists(src_path, dst_name):
    src = Path(str(src_path))
    if not src.exists():
        return
    dst = _binary_dir() / dst_name
    shutil.copy2(src, dst)
    print(f"[export_binaries] copied {src.name} -> {dst.relative_to(Path(env.subst('$PROJECT_DIR')))}")


def _export_program_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")

    if env_name == "FlowIO":
        _copy_if_exists(build_dir / "firmware.bin", "firmware-flowio.bin")
    elif env_name == "Supervisor":
        _copy_if_exists(build_dir / "firmware.bin", "firmware-supervisor.bin")
    elif env_name == "FlowConnectDisplay":
        _copy_if_exists(build_dir / "firmware.bin", "firmware-flow-connect-display.bin")


def _export_spiffs_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    if env_name != "Supervisor":
        return
    _copy_if_exists(build_dir / "spiffs.bin", "spiffs-supervisor.bin")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _export_program_bin)
env.AddPostAction("$BUILD_DIR/spiffs.bin", _export_spiffs_bin)
