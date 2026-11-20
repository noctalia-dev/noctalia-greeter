"""Check display metadata without a Wayland/greetd session or publishing anything."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary, version_file, revision_header, validator = map(Path, sys.argv[1:])
version = version_file.read_text().strip()
revision = re.search(r'#define NOCTALIA_GREETER_GIT_REVISION "([^"]*)"', revision_header.read_text())[1]
label = f"noctalia-greeter v{version}"
if revision and revision != "unknown":
    label += f" ({revision})"
env = os.environ.copy()
for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "GREETD_SOCK", "NOCTALIA_GREETER_LOG"):
    env.pop(key, None)
for flag in ("--version", "-v"):
    result = subprocess.run([binary.resolve(), flag], env=env, capture_output=True, text=True, check=True)
    assert result.stdout.strip() == label, result
    assert not result.stderr, result.stderr
env["NOCTALIA_GREETER_LOG"] = "stderr"
result = subprocess.run([binary.resolve()], env=env, capture_output=True, text=True)
assert result.returncode == 1, result
assert f"[INF] [main] {label}" in result.stdout + result.stderr, result
with tempfile.TemporaryDirectory(prefix="noctalia-version-") as directory:
    path = Path(directory) / "VERSION"
    for contents, tag, success in (
        (version + "\n", "v" + version, True),
        (version + "\n", "v0.0.0-mismatch", False),
        ("", "v", False),
        (version + "\n\n", "v" + version, False),
        ("1.0.0\n2.0.0\n", "v1.0.0", False),
        ("1.0.0 bad\n", "v1.0.0 bad", False),
    ):
        path.write_text(contents)
        result = subprocess.run(["bash", validator, path, tag], capture_output=True, text=True)
        assert (result.returncode == 0) == success, result
        if success:
            assert result.stdout.strip() == version
print("version metadata: CLI/startup agreement and release validation passed")
