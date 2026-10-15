#!/usr/bin/env python3
"""Download the compile-only Xposed API and verify its pinned digest."""
import hashlib
from pathlib import Path
from urllib.request import urlopen

URL = "https://api.xposed.info/de/robv/android/xposed/api/82/api-82.jar"
SHA256 = "f48c635f1c7469fdec0e00ad2ea0b7a6b2f5b55065784a35b7ca3a84615e8e25"
target = Path(__file__).resolve().parent / "deps/xposed-api-82.jar"
if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == SHA256:
    print("Xposed API already verified.")
else:
    with urlopen(URL, timeout=30) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("Xposed API checksum mismatch; dependency was not saved.")
    target.parent.mkdir(exist_ok=True)
    target.write_bytes(data)
    print("Xposed API downloaded and verified.")
