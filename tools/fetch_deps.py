# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
"""Cache pinned upstream sources for offline CMake builds (Python 3 stdlib only)."""
import concurrent.futures
import hashlib
import io
import pathlib
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPS = {
    "xatlas": ("jpcy/xatlas", "f700c7790aaa030e794b52ba7791a05c085faf0c"),
    "glfw": ("glfw/glfw", "3.4"),
    "imgui": ("ocornut/imgui", "v1.91.9b"),
    "assimp": ("assimp/assimp", "v6.0.2"),
    "stb": ("nothings/stb", "f0569113c93ad095470c54bf34a17b36646bbbb5"),
    "tinyusdz": ("lighttransport/tinyusdz", "6050eef932f7d2788656d63297aa488fb0961ed1"),
}
HASHES = {
    "xatlas": "57b56dbedab3bf0a487e4c0ed3cc3fe79210d4a008d59a8326af59196a3487eb",
    "glfw": "c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01",
    "imgui": "8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb",
    "assimp": "d1822d9a19c9205d6e8bc533bf897174ddb360ce504680f294170cc1d6319751",
    "stb": "4d05c96640ae3a8cbdafdad8d344b50ce610802f78aee80154acdb8e266282e0",
    "tinyusdz": "5a9aa6702a1d922d9ef12f52ea7a7a4f11425fb03e210dd0fdd2c3a000d56e4b",
}

def fetch(item):
    name, (repo, ref) = item
    target = ROOT / "third_party" / name
    if target.exists():
        return f"{name}: cached"
    url = f"https://codeload.github.com/{repo}/tar.gz/{ref}"
    data = urllib.request.urlopen(url, timeout=120).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != HASHES[name]:
        raise RuntimeError(f"Unexpected SHA-256 for {name}: {digest}")
    target.parent.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        prefix = archive.getmembers()[0].name.split('/')[0]
        for entry in archive.getmembers():
            relative = pathlib.PurePosixPath(entry.name).relative_to(prefix)
            if '..' in relative.parts or entry.issym() or entry.islnk():
                raise RuntimeError("Unsafe archive entry")
            if entry.isdir():
                (target / relative).mkdir(parents=True, exist_ok=True)
            elif entry.isfile():
                destination = target / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.extractfile(entry).read())
    return f"{name}: {ref} SHA256={digest}"

if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        for result in executor.map(fetch, DEPS.items()):
            print(result, flush=True)
