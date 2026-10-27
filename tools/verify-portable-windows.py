"""Verify, relocate, and ZIP the most recently assembled portable directory.

Does not access the camera. Uses a copied package, system-only PATH, native Qt
startup, and generated synthetic ONNX inputs. Requires the developer toolchain.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
from datetime import datetime
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_tools", ROOT / "tools/package-windows.py")
package_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_tools)

PROBE = r'''
#include <QCoreApplication>
#include "onnx_loader.hpp"
#include <opencv2/dnn.hpp>
#include <iostream>
#include <map>
#include <vector>

void checkModel(const QString& path, int side,
                const std::map<std::string, size_t>& expected) {
    auto model = neko::desktop::loadDesktopOnnx(path);
    int shape[] = {1, side, side, 3};
    cv::Mat input(4, shape, CV_32F, cv::Scalar(0));
    model.setInput(input);
    auto names = model.getUnconnectedOutLayersNames();
    std::vector<cv::Mat> outputs;
    model.forward(outputs, names);
    if (names.size() != expected.size()) throw std::runtime_error("Output count mismatch");
    for (size_t i = 0; i < names.size(); ++i) {
        auto found = expected.find(names[i]);
        if (found == expected.end() || outputs[i].total() != found->second)
            throw std::runtime_error("Unexpected ONNX output");
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto models = QCoreApplication::applicationDirPath() + "/models/";
        checkModel(models + "palm_detection_mediapipe_2023feb.onnx", 192,
                   {{"Identity", 2016 * 18}, {"Identity_1", 2016}});
        checkModel(models + "handpose_estimation_mediapipe_2023feb.onnx", 224,
                   {{"Identity", 63}, {"Identity_1", 1}, {"Identity_2", 1}, {"Identity_3", 63}});
        std::cout << "models_ok palm=2016x18,2016 pose=63,1,1,63\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''


def loaded_modules(pid):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE),
                                          wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        modules = (wintypes.HMODULE * 2048)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
            raise ctypes.WinError(ctypes.get_last_error())
        result = []
        for module in modules[: needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            buffer = ctypes.create_unicode_buffer(32768)
            if psapi.GetModuleFileNameExW(handle, module, buffer, len(buffer)):
                result.append(buffer.value)
        return result
    finally:
        kernel.CloseHandle(handle)


def isolated_environment(folder):
    env = {name: value for name, value in os.environ.items()
           if not name.upper().startswith(("QT_", "QML", "OPENCV_", "GST_", "GIO_"))}
    windows = Path(os.environ["SystemRoot"])
    env["PATH"] = os.pathsep.join([str(folder), str(windows / "System32"), str(windows)])
    env["QT_QPA_PLATFORM"] = "windows"
    env["QT_PLUGIN_PATH"] = str(folder)
    env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(folder / "platforms")
    return env


def smoke(folder, work):
    snapshot = work / "robot.png"
    process = subprocess.Popen([str(folder / "neko_desktop.exe"), "--smoke-test", "--vision-debug",
                                "--snapshot", str(snapshot)], cwd=work,
                               env=isolated_environment(folder), creationflags=subprocess.CREATE_NO_WINDOW)
    modules = set()
    deadline = time.monotonic() + 8
    while process.poll() is None and time.monotonic() < deadline:
        modules.update(loaded_modules(process.pid))
        time.sleep(0.05)
    if process.poll() is None:
        process.terminate()
        raise RuntimeError("Portable smoke test timed out")
    if process.returncode != 0:
        raise RuntimeError(f"Portable application exited with {process.returncode}")
    if not snapshot.is_file() or snapshot.stat().st_size < 1024:
        raise RuntimeError("Native Qt snapshot was not created")
    external = [path for path in modules if "msys64" in path.lower()]
    if external:
        raise RuntimeError(f"Loaded DLLs from developer environment: {external}")
    by_name = {Path(path).name.lower(): path for path in modules}
    for required in ("qt6core.dll", "qt6gui.dll", "qt6widgets.dll", "qwindows.dll", "libopencv_dnn-413.dll"):
        if required not in by_name or not Path(by_name[required]).is_relative_to(folder):
            raise RuntimeError(f"Not loaded from package: {required}")
    log = (folder / "desktop-vision.log").read_text(encoding="utf-8")
    if "camera=0" not in log or "frame t=" in log:
        raise RuntimeError("Unexpected diagnostic startup or camera frames")
    for model in ("palm_detection_mediapipe_2023feb.onnx", "handpose_estimation_mediapipe_2023feb.onnx"):
        if (folder / "models" / model).as_posix() not in log:
            raise RuntimeError(f"Default model path did not resolve inside package: {model}")
    return {"exit_code": process.returncode, "loaded_module_count": len(modules),
            "developer_dlls_loaded": False, "native_qwindows_snapshot": True}


def main():
    package = Path((ROOT / "build/portable-latest.txt").read_text(encoding="utf-8")).resolve()
    for line in (package / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
        expected, name = line.split("  ", 1)
        if package_tools.sha256(package / name) != expected:
            raise RuntimeError(f"Checksum mismatch before verification: {name}")
    work = ROOT / "build/portable-check" / datetime.now().strftime("%Y%m%d-%H%M%S")
    work.mkdir(parents=True, exist_ok=False)
    relocated = work / "Neko test"
    shutil.copytree(package, relocated)
    other_cwd = work / "unrelated-cwd"
    other_cwd.mkdir()
    report = {"camera_accessed": False, "other_computer_tested": False, "isolated_path": True,
              "relocated_directory_with_spaces": smoke(relocated, other_cwd)}
    prefix = Path("D:/msys64/ucrt64")
    source = work / "model-probe.cpp"
    source.write_text(PROBE, encoding="utf-8")
    compiler_env = os.environ.copy()
    compiler_env["PATH"] = str(prefix / "bin") + os.pathsep + compiler_env.get("PATH", "")
    probe = relocated / "model-probe.exe"
    package_tools.run([prefix / "bin/c++.exe", "-std=c++17", "-O2", source,
                       "-I" + str(prefix / "include/opencv4"), "-I" + str(prefix / "include/qt6"),
                       "-I" + str(prefix / "include/qt6/QtCore"), "-L" + str(prefix / "lib"),
                       "-I" + str(ROOT / "src/desktop"),
                       "-lopencv_dnn", "-lopencv_core", "-lQt6Core", "-o", probe], env=compiler_env)
    result = package_tools.run([probe], cwd=other_cwd, env=isolated_environment(relocated),
                               capture_output=True, text=True, timeout=30)
    if not result.stdout.startswith("models_ok"):
        raise RuntimeError("Model probe did not confirm output shapes")
    report["synthetic_model_inference"] = result.stdout.strip()
    # Also verify model filename conversion in a non-ASCII extraction directory.
    unicode_folder = work / "Neko 测试"
    shutil.copytree(package, unicode_folder)
    shutil.copy2(probe, unicode_folder / probe.name)
    unicode_result = subprocess.run([str(unicode_folder / probe.name)], cwd=other_cwd,
                                    env=isolated_environment(unicode_folder), capture_output=True,
                                    timeout=30)
    report["unicode_model_path_passed"] = unicode_result.returncode == 0
    if unicode_result.returncode != 0:
        (work / "unicode-model-error.txt").write_bytes(unicode_result.stderr)
        raise RuntimeError(f"Model loading failed under a Unicode directory; details: {work}")
    report_path = package.parent / (package.name + "-verification.json")
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    shutil.copy2(report_path, package / "verification-report.json")
    checksums = [f"{package_tools.sha256(path)}  {path.relative_to(package).as_posix()}"
                 for path in sorted(package.rglob("*")) if path.is_file() and path.name != "SHA256SUMS.txt"]
    (package / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    archive = package.parent / (package.name + ".zip")
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                output.write(path, Path(package.name) / path.relative_to(package))
    with zipfile.ZipFile(archive) as output:
        bad = output.testzip()
        if bad:
            raise RuntimeError(f"ZIP CRC failure: {bad}")
    checksum = package_tools.sha256(archive)
    (archive.parent / (archive.name + ".sha256")).write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
    print(json.dumps({"archive": str(archive), "size_mib": round(archive.stat().st_size / 1024**2, 1),
                      "sha256": checksum, "verification": report}, indent=2, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
