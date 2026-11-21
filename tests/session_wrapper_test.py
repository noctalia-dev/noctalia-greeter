#!/usr/bin/env python3
"""Run the real session wrapper with a private /tmp and harmless launch stubs."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


parser = argparse.ArgumentParser()
parser.add_argument("wrapper", type=Path)
parser.add_argument("--shell", default="/bin/sh", type=Path)
parser.add_argument("--inner", action="store_true")
args = parser.parse_args()
wrapper = args.wrapper.resolve()
shell = args.shell.resolve()

if not args.inner:
    # Never operate on the caller's /tmp/noctalia-runtime-<uid>. Namespace and
    # mount failures are an explicit Meson skip, not a pass for missing coverage.
    namespace = ["unshare", "--user", "--map-root-user", "--mount", "sh", "-c"]
    mount_tmp = "mount --make-rprivate / && mount -t tmpfs -o mode=1777 tmpfs /tmp"
    if not shutil.which("unshare") or subprocess.run(
        namespace + [mount_tmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode:
        print("SKIP: session-wrapper tests require user/mount namespaces and a private tmpfs")
        sys.exit(77)
    command = mount_tmp + ' && touch /tmp/.noctalia-wrapper-test && exec "$@"'
    # Feed exact wrapper bytes and the test over stdin: a checkout/source
    # archive under /tmp will be hidden by the private tmpfs mount.
    program = ("from pathlib import Path\n"
               "Path('/tmp/wrapper-under-test').write_bytes(" + repr(wrapper.read_bytes()) + ")\n"
               + Path(__file__).read_text())
    sys.exit(subprocess.run(namespace + [command, "sh", str(Path(sys.executable).resolve()), "-",
                                       "/tmp/wrapper-under-test", "--shell", str(shell), "--inner"],
                            input=program, text=True).returncode)

if os.getuid() != 0 or not Path("/tmp/.noctalia-wrapper-test").is_file():
    raise SystemExit("Refusing to test without the private namespace fixture")


class SessionWrapperTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="session-wrapper-")
        self.root = Path(self.temporary.name)
        self.runtime = Path(f"/tmp/noctalia-runtime-{os.getuid()}")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        # An explicit PATH permits testing the no-dbus branch even on systems
        # where dbus-run-session is installed.
        for name in ["sh", "dirname", "date", "id", "mkdir", "chmod", "touch", "env"]:
            program = shutil.which(name)
            self.assertIsNotNone(program, name)
            (self.bin / name).symlink_to(program)
        self.compositor = self.root / "compositor"
        self.compositor.write_text(f"#!{sys.executable}\n" + """
import json, os, sys
from pathlib import Path
Path(os.environ['TEST_RECORD']).write_text(json.dumps({
    'arguments': sys.argv[1:],
    'runtime': os.environ['XDG_RUNTIME_DIR'],
    'mode': Path(os.environ['XDG_RUNTIME_DIR']).stat().st_mode & 0o777,
    'greeter': os.environ['GREETER_BIN'],
    'wayland': os.environ.get('WAYLAND_DISPLAY'),
    'log': os.environ.get('NOCTALIA_GREETER_LOG'),
    'wlr_log': os.environ['WLR_LOG'],
    'dbus': os.environ.get('TEST_DBUS'),
}))
""")
        self.compositor.chmod(0o755)
        self.greeter = self.root / "greeter"
        self.greeter.write_text("#!/bin/sh\nexit 99\n")
        self.greeter.chmod(0o755)
        self.record = self.root / "launch.json"
        self.env = os.environ.copy()
        for name in ["GREETD_SOCK", "WAYLAND_SOCKET", "DISPLAY", "DBUS_SESSION_BUS_ADDRESS"]:
            self.env.pop(name, None)
        self.env.update(PATH=str(self.bin), TEST_RECORD=str(self.record),
                        XDG_RUNTIME_DIR=str(self.root / "inherited-runtime"),
                        WAYLAND_DISPLAY="inherited-wayland", NOCTALIA_GREETER_LOG="stderr",
                        NOCTALIA_GREETER_BIN=str(self.greeter),
                        NOCTALIA_GREETER_COMPOSITOR_BIN=str(self.compositor))
        self.env.pop("WLR_LOG", None)

    def tearDown(self):
        if self.runtime.is_symlink() or self.runtime.is_file():
            self.runtime.unlink()
        elif self.runtime.exists():
            shutil.rmtree(self.runtime)
        self.temporary.cleanup()

    def run_wrapper(self, *arguments):
        return subprocess.run([str(shell), str(wrapper), *arguments], env=self.env,
                              capture_output=True, text=True, timeout=10)

    def assert_rejected(self):
        result = self.run_wrapper()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("is not a directory owned by uid", result.stdout + result.stderr)
        self.assertFalse(self.record.exists(), "compositor launched before runtime validation")

    def test_create_and_launch_without_dbus(self):
        result = self.run_wrapper("--user", "name with spaces", "--session", "Session (test)")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        record = json.loads(self.record.read_text())
        self.assertEqual(record["arguments"], ["--", "--user", "name with spaces", "--session", "Session (test)"])
        self.assertEqual(record["runtime"], str(self.runtime))
        self.assertEqual(record["mode"], 0o700)
        self.assertEqual(record["greeter"], str(self.greeter))
        self.assertIsNone(record["wayland"])
        self.assertEqual(record["wlr_log"], "error")
        self.assertEqual(record["log"], "stderr")
        self.assertIn("dbus-run-session not found", result.stdout)

    def test_reuse_owned_directory(self):
        self.runtime.mkdir(mode=0o755)
        marker = self.runtime / "existing"
        marker.write_text("preserve me")
        inode = self.runtime.stat().st_ino
        for _ in range(2):
            result = self.run_wrapper()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(self.runtime.stat().st_ino, inode)
            self.assertEqual(self.runtime.stat().st_mode & 0o777, 0o700)
            self.assertEqual(marker.read_text(), "preserve me")

    def test_symlink(self):
        target = self.root / "target"
        target.mkdir(mode=0o755)
        target.chmod(0o755)
        self.runtime.symlink_to(target)
        self.assert_rejected()
        self.assertTrue(self.runtime.is_symlink())
        self.assertEqual(target.stat().st_mode & 0o777, 0o755)

    def test_dangling_symlink(self):
        target = self.root / "missing"
        self.runtime.symlink_to(target)
        self.assert_rejected()
        self.assertTrue(self.runtime.is_symlink())
        self.assertFalse(target.exists())

    def test_regular_file(self):
        self.runtime.write_text("preserve me")
        self.assert_rejected()
        self.assertEqual(self.runtime.read_text(), "preserve me")

    def test_foreign_owned_directory(self):
        # A read-only store directory belongs to an unmapped host UID here.
        # Bind it only inside the private mount namespace, and never modify it.
        source = Path(sys.executable).resolve().parent
        if source.stat().st_uid == os.getuid():
            self.skipTest("no foreign-owned fixture directory available")
        self.runtime.mkdir()
        subprocess.run(["mount", "--bind", str(source), str(self.runtime)], check=True)
        try:
            self.assertNotEqual(self.runtime.stat().st_uid, os.getuid())
            self.assert_rejected()
        finally:
            subprocess.run(["umount", str(self.runtime)], check=True)

    def test_dbus_launch_and_log_defaults(self):
        bus = self.bin / "dbus-run-session"
        bus.write_text('#!/bin/sh\n[ "$1" = -- ] || exit 98\nshift\nexport TEST_DBUS=entered\nexec "$@"\n')
        bus.chmod(0o755)
        self.env.pop("NOCTALIA_GREETER_LOG")
        self.env["WLR_LOG"] = "info"
        result = self.run_wrapper()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout + result.stderr, "")
        record = json.loads(self.record.read_text())
        self.assertEqual(record["dbus"], "entered")
        self.assertEqual(record["wlr_log"], "info")
        self.assertIsNone(record["log"])


unittest.main(argv=[sys.argv[0]], verbosity=2)
