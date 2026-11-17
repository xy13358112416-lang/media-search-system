"""Check imported synthetic users through a real server on an ephemeral port.

All data and databases are created in a temporary directory. No existing user
database is opened. Pass the integration checkout and build directory as args.
"""
import argparse
import collections
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("repo", type=Path)
parser.add_argument("build", type=Path)
parser.add_argument("--users", type=int, default=20)
parser.add_argument("--end-date", default="2026-11-15")
args = parser.parse_args()
repo, build = args.repo, args.build
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
                    "--output", str(fixture_dir), "--users", str(args.users),
                    "--end-date", args.end_date], check=True, capture_output=True)
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
        numbers = sorted({1, args.users} | {n for n in (5, 6, 11, 15, 16, 17, 18, 19, 20) if n <= args.users})
        for number in numbers:
            user = f"sim_user_{number:02d}"
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
                recommendations = call(client, {"type": "recommend_temporal", "req_id": "recommend", "token": token,
                    "payload": {"category": {"mode": "auto", "value": ""},
                                "file_type": {"mode": "auto", "value": ""}, "scene": "neutral", "limit": 6}})
                result = recommendations["payload"]["result"]
                assert result["engine"] == "temporal"
                assert result["items"], (user, result)
                assert len({i["work"]["resource"]["work_id"] for i in result["items"]}) == len(result["items"])
                if args.users == 200:
                    for kind in ("works", "search_terms"):
                        board = call(client, {"type": "get_trending", "req_id": "trending-" + kind, "token": token,
                            "payload": {"kind": kind, "range": {"begin_utc": str(int(fixture["now_utc"]) - 7 * 86400),
                                        "end_utc": fixture["now_utc"]}, "limit": 20, "summary_mode": "rules"}})
                        result = board["payload"]["result"]
                        assert result["items"] and result["contains_synthetic_data"]
                        assert not result["summary_from_model"]
                        assert all(int(i["distinct_users"]) >= 3 and int(i["effective_events"]) >= int(i["distinct_users"])
                                   for i in result["items"])
    finally:
        server.terminate()
        server.wait(timeout=10)
