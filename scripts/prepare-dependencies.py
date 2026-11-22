#!/usr/bin/env python3
"""Fetch pinned official M5 libraries into the ignored project build folder."""
from hashlib import sha256
import io
from pathlib import Path
import urllib.request
from zipfile import ZipFile

root = Path(__file__).resolve().parent.parent / ".build-deps"
root.mkdir(exist_ok=True)
versions = {
    "M5Cardputer": ("1.1.1", "7f77a390f3c30662c3f092dc39139cc7aa201b80125bc6e703223e863f5be379"),
    "M5Unified": ("0.2.28", "38886452024b49f9d32dc10cce9e0a5ba20719e352924da66267e688266b7539"),
    "M5GFX": ("0.2.35", "01a81c67cc3544a2eca75022a8b2bdac01115102c22fb289187b899246e91ced"),
}
for name, (version, expected) in versions.items():
    target = root / name
    marker = target / ".source-sha256"
    if target.exists():
        properties = target / "library.properties"
        if marker.exists() and marker.read_text().startswith(expected + " ") and properties.exists() and f"version={version}" in properties.read_text():
            print(f"{name} {version}: cached", flush=True)
            continue
        raise SystemExit(f"{target}: directorio existente sin la version fijada")
    url = f"https://codeload.github.com/m5stack/{name}/zip/refs/tags/{version}"
    print(f"Downloading official {name} {version}", flush=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    digest = sha256(data).hexdigest()
    if digest != expected:
        raise ValueError(f"{name}: SHA256 distinto al archivo oficial verificado")
    with ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            parts = Path(member.filename).parts
            if len(parts) < 2:
                continue
            relative = Path(*parts[1:])
            if relative.is_absolute() or any(p in (".", "..") for p in relative.parts):
                raise ValueError("Unsafe dependency archive path")
            destination = target / relative
            if member.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.read(member))
    marker.write_text(f"{digest}  {url}\n")
    print(f"{name} SHA256 {digest}", flush=True)
