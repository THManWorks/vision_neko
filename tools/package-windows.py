"""Assemble the existing desktop build and its UCRT64 DLL dependency closure.

Developer command: python tools/package-windows.py
Recipients do not need Python. Output stays under the ignored build directory.
"""

from __future__ import annotations

import argparse
from collections import deque
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    return subprocess.run([str(value) for value in command], check=True, **kwargs)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def imports(binary, objdump):
    result = run([objdump, "-p", binary], capture_output=True, text=True)
    return re.findall(r"DLL Name:\s*(\S+)", result.stdout)


def collect_dependencies(folder, prefix):
    objdump = prefix / "bin/objdump.exe"
    installed = {p.name.lower(): p for p in (prefix / "bin").glob("*.dll")}
    system = Path(os.environ["SystemRoot"]) / "System32"
    queue = deque(sorted(folder.rglob("*.dll")) + [folder / "neko_desktop.exe"])
    scanned = set()
    sources = {}
    while queue:
        binary = queue.popleft()
        key = binary.resolve()
        if key in scanned:
            continue
        scanned.add(key)
        for name in imports(binary, objdump):
            lower = name.lower()
            if lower.startswith(("api-ms-", "ext-ms-")):
                continue
            source = installed.get(lower)
            target = folder / name
            if source is not None:
                if not target.exists():
                    shutil.copy2(source, target)
                sources[target.name.lower()] = source
                queue.append(target)
            elif not (system / name).is_file():
                raise RuntimeError(f"Missing dependency {name} imported by {binary.name}")
    # windeployqt supplies these plugins outside bin.
    for plugin in (folder / "platforms").glob("*.dll"):
        sources[plugin.relative_to(folder).as_posix()] = prefix / "share/qt6/plugins/platforms" / plugin.name
    for plugin in (folder / "styles").glob("*.dll"):
        sources[plugin.relative_to(folder).as_posix()] = prefix / "share/qt6/plugins/styles" / plugin.name
    return sources


def package_metadata(folder, prefix, sources):
    msys = prefix.parent
    wanted = {source.relative_to(msys).as_posix().lower(): name for name, source in sources.items()}
    owners = {}
    components = []
    for directory in sorted((msys / "var/lib/pacman/local").iterdir()):
        files = directory / "files"
        if not files.is_file():
            continue
        paths = files.read_text(encoding="utf-8", errors="replace").splitlines()
        matching = set(wanted) & {path.lower() for path in paths}
        if not matching:
            continue
        fields = {}
        for section in (directory / "desc").read_text(encoding="utf-8", errors="replace").split("\n\n"):
            lines = section.splitlines()
            if lines and lines[0].startswith("%"):
                fields[lines[0].strip("%")] = lines[1:]
        name = fields["NAME"][0]
        base = fields.get("BASE", [name])[0]
        component = {
            "name": name, "version": fields["VERSION"][0],
            "homepage": fields.get("URL", []), "licenses": fields.get("LICENSE", []),
            "recipe": f"https://github.com/msys2/MINGW-packages/tree/master/{base}",
            "package_page": f"https://packages.msys2.org/packages/{name}",
            "files": sorted(wanted[path] for path in matching), "license_files": [],
        }
        for relative in paths:
            source = msys / relative
            if not source.is_file():
                continue
            basename = source.name.upper()
            if "/share/licenses/" not in relative and not (
                "/share/doc/" in relative and any(token in basename for token in ("LICENSE", "COPYING", "COPYRIGHT", "NOTICE"))
            ):
                continue
            # Keep paths short enough for Windows installations without long-path support.
            if "/share/licenses/" in relative:
                destination = folder / "licenses/installed" / relative.split("/share/licenses/", 1)[1]
            else:
                destination = folder / "licenses/docs" / relative.split("/share/doc/", 1)[1]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            component["license_files"].append(destination.relative_to(folder).as_posix())
        for path in matching:
            owners[wanted[path]] = name
        components.append(component)
    missing = set(sources) - set(owners)
    if missing:
        raise RuntimeError(f"No package metadata for: {sorted(missing)}")
    (folder / "THIRD-PARTY-COMPONENTS.json").write_text(
        json.dumps(components, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    # Qt's distribution includes the common GPL/LGPL/Apache/BSD license texts,
    # also used by dependencies whose packages do not install a separate copy.
    shutil.copytree(prefix / "share/licenses/qt6-base", folder / "licenses/common-texts")
    lines = ["# Third-party components", "", "Installed component versions and source locations are listed below.",
             "Full package metadata and file ownership: THIRD-PARTY-COMPONENTS.json.",
             "Installed license/notice files: licenses/. Common license texts: licenses/common-texts/.",
             "Recipe links may change; the exact binary package versions are recorded here.", "",
             "Models: OpenCV Zoo palm_detection_mediapipe and handpose_estimation_mediapipe.",
             "Model license text: models/LICENSE.txt.", "",
             "| Component | Version | Package license declarations | Source/build recipe |", "| --- | --- | --- | --- |"]
    for item in components:
        license_text = ", ".join(item["licenses"]).replace("|", "\\|")
        lines.append(f'| {item["name"]} | {item["version"]} | {license_text} | {item["recipe"]} |')
    (folder / "THIRD-PARTY-NOTICES.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return components


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("D:/msys64/ucrt64"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    build = args.build_dir.resolve()
    env = os.environ.copy()
    env["PATH"] = str(prefix / "bin") + os.pathsep + env.get("PATH", "")
    run([prefix / "bin/cmake.exe", "--build", build, "--target", "neko_desktop"], cwd=ROOT, env=env)
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d-%H%M%S")
    output = ROOT / "build/portable" / f"Neko-0.3.0-win64-{stamp}"
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(build / "neko_desktop.exe", output)
    qt_command = [prefix / "bin/windeployqt.exe", "--release", "--no-translations",
                  "--no-system-d3d-compiler", "--no-system-dxc-compiler", "--no-opengl-sw",
                  "--skip-plugin-types", "generic,networkinformation,tls,imageformats",
                  output / "neko_desktop.exe"]
    run(qt_command, env=env)
    sources = collect_dependencies(output, prefix)
    print(f"Collected {len(sources)} DLLs", flush=True)
    (output / "models").mkdir()
    for name in ("palm_detection_mediapipe_2023feb.onnx", "handpose_estimation_mediapipe_2023feb.onnx", "LICENSE.txt"):
        shutil.copy2(ROOT / "models" / name, output / "models" / name)
    (output / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="ascii")
    launch = ('@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
              'set "PATH=%~dp0;%SystemRoot%\\System32;%SystemRoot%"\r\n'
              'set "QT_PLUGIN_PATH=%~dp0"\r\n'
              'set "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0platforms"\r\n'
              'set "QT_QPA_PLATFORM=windows"\r\n'
              'start "" "%~dp0neko_desktop.exe" %*\r\n')
    (output / "StartNeko.cmd").write_bytes(launch.encode("ascii"))
    (output / "StartWithPreview.cmd").write_bytes(
        b'@echo off\r\ncall "%~dp0StartNeko.cmd" --camera --preview %*\r\n')
    (output / "README.txt").write_text((ROOT / "docs/portable.md").read_text(encoding="utf-8"), encoding="utf-8-sig")
    components = package_metadata(output, prefix, sources)
    revision = run(["git", "-c", f"safe.directory={ROOT.as_posix()}", "rev-parse", "HEAD"],
                   cwd=ROOT, capture_output=True, text=True).stdout.strip()
    source_folder = output / "source"
    source_folder.mkdir()
    with zipfile.ZipFile(source_folder / "ProjectNeko-source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for top in ("src", "tools"):
            for path in sorted((ROOT / top).rglob("*")):
                if path.is_file() and "__pycache__" not in path.parts:
                    archive.write(path, path.relative_to(ROOT))
        for path in (ROOT / "CMakeLists.txt", ROOT / "docs/desktop.md", ROOT / "docs/portable.md"):
            archive.write(path, path.relative_to(ROOT))
    info = {"application_version": "0.3.0", "stage": "phase-1", "created_asia_shanghai": stamp,
            "source_base_commit": revision, "platform": "Windows x64 UCRT64",
            "source_snapshot": "Current working-tree sources, including packaging changes since the base commit",
            "exe_sha256": sha256(output / "neko_desktop.exe"), "dll_count": len(sources),
            "component_count": len(components),
            "validation": "Automatic verification removed; manually check the packaged application."}
    (output / "BUILD-INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    checksums = [f"{sha256(path)}  {path.relative_to(output).as_posix()}" for path in sorted(output.rglob("*")) if path.is_file()]
    (output / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    archive_path = output.parent / (output.name + ".zip")
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(output.parent))
    archive_path.with_name(archive_path.name + ".sha256").write_text(
        f"{sha256(archive_path)}  {archive_path.name}\n", encoding="utf-8")
    (build / "portable-latest.txt").write_text(str(output), encoding="utf-8")
    print(f"PACKAGE_DIRECTORY={output}", flush=True)


if __name__ == "__main__":
    main()
