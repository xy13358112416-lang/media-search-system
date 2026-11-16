"""Check imported synthetic users through a real server on an ephemeral port.

All data and databases are created in a temporary directory. No existing user
database is opened. Pass the integration checkout and build directory as args.
"""
import collections
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

repo, build = map(Path, sys.argv[1:3])
server_exe = build / "server" / "media_server.exe"
import_exe = build / "server" / "media_demo_import.exe"


def exact(sock, size):
    data = b""
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise RuntimeError("server closed the response early")
        data += part
    return data


def call(sock, request):
    data = json.dumps(request).encode("utf-8")
    sock.sendall(struct.pack(">I", len(data)) + data)
    size = struct.unpack(">I", exact(sock, 4))[0]
    response = json.loads(exact(sock, size).decode("utf-8"))
    if response.get("error"):
        raise RuntimeError(str(response))
    return response


with tempfile.TemporaryDirectory(prefix="media-e2e-") as temp:
    root = Path(temp)
    fixture_dir = root / "simulation"
    subprocess.run([sys.executable, str(repo / "sample-data/simulation/generate.py"),
                    "--output", str(fixture_dir)], check=True, capture_output=True)
    fixture = json.loads((fixture_dir / "algorithm_fixture.json").read_text(encoding="utf-8"))
    db = root / "demo.sqlite"
    subprocess.run([str(import_exe), "--demo-db", str(db), "--fixture",
                    str(fixture_dir / "algorithm_fixture.json"), "--demo-password-stdin"],
                   input=b"synthetic-test-password\n", check=True, capture_output=True)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    server = subprocess.Popen([str(server_exe), "--db", str(db), "--port", str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        while True:
            try:
                first = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                if server.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("server failed to start")
                time.sleep(0.05)
        first.close()
        for user in ("sim_user_01", "sim_user_20"):
            expected = collections.Counter(e["action"] for e in fixture["events"] if e["user_ref"] == user)
            with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
                login = call(client, {"type": "login", "req_id": "login", "payload": {
                    "username": user, "password": "synthetic-test-password"}})
                token = login["payload"]["token"]
                result = call(client, {"type": "get_footprint", "req_id": "footprint", "token": token,
                    "payload": {"range": {"begin_utc": fixture["start_utc"], "end_utc": fixture["now_utc"]}}})
                report = result["payload"]["report"]
                actual = report["current"]
                assert int(actual["views"]) == expected["view"], (user, actual, expected)
                assert int(actual["searches"]) == expected["search"], (user, actual, expected)
                if expected:
                    assert report["contains_synthetic_data"] is True
                print(f"{user}: views={expected['view']}, searches={expected['search']}, synthetic={report['contains_synthetic_data']}")
    finally:
        server.terminate()
        server.wait(timeout=10)
