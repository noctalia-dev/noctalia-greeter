#!/usr/bin/env python3
"""Exercise the real greeter/compositor with an isolated fake greetd daemon.

This checks IPC, UI, and process handoff; it does not emulate PAM, DRM, or a
distribution's desktop-session packages.
"""

import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read_exact(peer, length):
    data = bytearray()
    while len(data) < length:
        chunk = peer.recv(length - len(data))
        require(chunk, "greeter disconnected before completing the conversation")
        data.extend(chunk)
    return data


def request(peer, expected_type):
    length = struct.unpack("=I", read_exact(peer, 4))[0]
    require(length <= 65536, "oversized IPC request")
    message = json.loads(read_exact(peer, length))
    require(message["type"] == expected_type, f"expected {expected_type}, got {message}")
    return message


def reply(peer, message):
    payload = json.dumps(message).encode()
    peer.sendall(struct.pack("=I", len(payload)) + payload)


def run_login(compositor, client, root, outcome, outputs):
    runtime = root / "runtime"
    runtime.mkdir(exist_ok=True, mode=0o700)
    state = root / "state"
    state.mkdir(exist_ok=True)
    data = root / "data" / "wayland-sessions"
    data.mkdir(parents=True, exist_ok=True)
    session_script = root / "session.sh"
    session_script.write_text('printf "%s" "$1" > "$2"\n')
    marker = root / "session-started"
    marker.unlink(missing_ok=True)
    command = ["/bin/sh", str(session_script), "synthetic-session", str(marker)]
    (data / "noctalia-test.desktop").write_text(
        "[Desktop Entry]\nName=Noctalia Login Test\n"
        f"Exec={' '.join(command)}\nDesktopNames=NoctaliaTest;\n"
    )
    config_path = state / "greeter.toml"
    # The commenter's appearance, cursor, keyboard, and output settings from #86.
    original = b'''# Keep the administrator's configuration intact.
[appearance]
hide_logo = true
password_style = "random"
[cursor]
size = 24
theme = "Afterglow Joris"
[keyboard]
numlock = true
[output]
scale = 1.25
[user]
default = "noctalia-login-test"
[session]
last = "Noctalia Login Test"
'''
    config_path.write_bytes(original)
    env = os.environ.copy()
    for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(key, None)
    env.update(
        XDG_RUNTIME_DIR=str(runtime),
        XDG_DATA_DIRS=str(data.parent),
        NOCTALIA_GREETER_STATE_DIR=str(state),
        GREETD_SOCK=str(root / "greetd.sock"),
        GREETER_BIN=client,
        WLR_BACKENDS="headless",
        WLR_HEADLESS_OUTPUTS=str(outputs),
        WLR_RENDERER="pixman",
        LIBGL_ALWAYS_SOFTWARE="1",
        NOCTALIA_GREETER_LOG="stderr",
    )
    socket_path = Path(env["GREETD_SOCK"])
    socket_path.unlink(missing_ok=True)
    log_path = root / f"{outcome}-{outputs}.log"
    with socket.socket(socket.AF_UNIX) as server, log_path.open("w+") as log:
        server.bind(str(socket_path))
        server.listen(1)
        server.settimeout(10)
        process = subprocess.Popen(
            [compositor], env=env, stdout=log, stderr=log, start_new_session=True
        )
        try:
            with server.accept()[0] as peer:
                peer.settimeout(10)
                create = request(peer, "create_session")
                require(create["username"] == "noctalia-login-test", "wrong login user")
                reply(peer, {
                    "type": "auth_message",
                    "auth_message_type": "secret",
                    "auth_message": "Password:",
                })
                credential = request(peer, "post_auth_message_response")
                require(credential.get("response") == "synthetic-test-token", "password input was changed")
                if outcome == "auth-error":
                    reply(peer, {
                        "type": "error",
                        "error_type": "auth_error",
                        "description": "Synthetic bad password",
                    })
                else:
                    reply(peer, {"type": "success"})
                    start = request(peer, "start_session")
                    require(start["cmd"] == command, f"wrong session argv: {start}")
                    require(set(start["env"]) == {
                        "XDG_SESSION_TYPE=wayland",
                        "XDG_CURRENT_DESKTOP=NoctaliaTest",
                        "XDG_SESSION_DESKTOP=NoctaliaTest",
                    }, f"wrong session environment: {start}")
                    if outcome == "start-error":
                        reply(peer, {
                            "type": "error",
                            "error_type": "error",
                            "description": "Synthetic session failure",
                        })
                    else:
                        # Do not acknowledge the handoff immediately: the greeter
                        # must stay alive until the daemon confirms start_session.
                        require(
                            not select.select([peer], [], [], 0.2)[0],
                            "greeter exited or repeated a request before start acknowledgement",
                        )
                        require(
                            process.poll() is None,
                            "compositor exited before start acknowledgement",
                        )
                        reply(peer, {"type": "success"})
                if outcome != "success":
                    request(peer, "cancel_session")
                    reply(peer, {"type": "success"})
                    require(
                        not select.select([peer], [], [], 0.3)[0],
                        "failed login disconnected or automatically repeated",
                    )
                    require(process.poll() is None, "failed login terminated the compositor")
                    require(not marker.exists(), "failed login started a session")
                    os.killpg(process.pid, signal.SIGTERM)
                else:
                    require(process.wait(timeout=10) == 0, "compositor failed during handoff")
                    log.flush()
                    require(
                        "login flow: session handoff completed" in log_path.read_text(),
                        "greeter did not exit cleanly after start acknowledgement",
                    )
                    # Model greetd launching the accepted command once its greeter
                    # process has released the display. Execute only our fixture.
                    subprocess.run(command, check=True, timeout=5)
                    require(marker.read_text() == "synthetic-session", "session command did not run")
                require(config_path.read_bytes() == original, "login rewrote greeter.toml")
                sync = (state / "sync.toml").read_text()
                require("Noctalia Login Test" in sync, "last session was not migrated or saved")
        except Exception:
            log.flush()
            print(log_path.read_text(), file=sys.stderr)
            raise
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
    print(f"login flow: {outcome}, {outputs} output(s) passed")


def main():
    compositor, client = map(os.path.abspath, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="noctalia-login-") as temporary:
        root = Path(temporary)
        for outputs in (1, 2):
            directory = root / str(outputs)
            directory.mkdir()
            # Repeat success with the same state to exercise the post-migration
            # startup path, as when greetd restarts the greeter after logout.
            for outcome in ("success", "success", "auth-error", "start-error"):
                run_login(compositor, client, directory, outcome, outputs)


if __name__ == "__main__":
    main()
