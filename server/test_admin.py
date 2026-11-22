"""Real TCP and SQLite regression checks for online administration."""
import json
from contextlib import closing
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time


class Connection:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.stream = self.socket.makefile("rb")
        self.token = None
        self.sequence = 0

    def call(self, operation, payload=None):
        self.sequence += 1
        request = {"type": operation, "req_id": str(self.sequence), "payload": payload or {}}
        if self.token:
            request["token"] = self.token
        data = json.dumps(request).encode()
        self.socket.sendall(struct.pack(">I", len(data)) + data)
        header = self.stream.read(4)
        assert len(header) == 4
        result = json.loads(self.stream.read(struct.unpack(">I", header)[0]))
        assert result["req_id"] == str(self.sequence)
        return result

    def login(self, name, password="test-pass-123"):
        result = self.call("login", {"username": name, "password": password})
        if "payload" in result:
            self.token = result["payload"]["token"]
        return result

    def close(self):
        self.stream.close()
        self.socket.close()


def run(executable):
    with tempfile.TemporaryDirectory() as directory:
        database = Path(directory) / "accounts.sqlite"
        args = [str(executable), "--db", str(database)]
        def provision(option, name):
            subprocess.run(args + [option, name], input="test-pass-123\n", text=True,
                           capture_output=True, check=True, timeout=15)

        provision("--add-user", "legacy")
        # Reproduce the schema of a database from before the admin extension.
        with closing(sqlite3.connect(database)) as db, db:
            db.execute("DROP TABLE network_user_access")
            db.execute("DROP TABLE admin_audit")
        provision("--add-admin", "operator")

        def start():
            with socket.socket() as pick:
                pick.bind(("127.0.0.1", 0))
                port = pick.getsockname()[1]
            process = subprocess.Popen(args + ["--listen", "127.0.0.1", "--port", str(port)],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(100):
                try:
                    connection = Connection(port)
                    connection.close()
                    return process, port
                except OSError:
                    assert process.poll() is None
                    time.sleep(.05)
            process.terminate()
            raise AssertionError("Server did not start")

        process, port = start()
        clients = []
        def connect(name, password="test-pass-123"):
            c = Connection(port)
            clients.append(c)
            return c, c.login(name, password)
        try:
            admin, response = connect("operator")
            assert response["payload"]["user"]["is_admin"] is True
            user, response = connect("legacy")
            assert response["payload"]["user"]["is_admin"] is False
            assert user.call("admin_list_users", {"offset": "0"})["code"] == "PermissionDenied"
            assert user.call("admin_create_user", {"username": "attack", "password": "test-pass-123", "is_admin": True})["code"] == "PermissionDenied"
            assert "payload" in admin.call("admin_create_user", {"username": "reader", "password": "test-pass-123", "is_admin": True})
            reader, response = connect("reader")
            assert response["payload"]["user"]["is_admin"] is False
            assert admin.call("admin_create_user", {"username": "READER", "password": "test-pass-123"})["code"] == "InvalidArgument"
            assert admin.call("admin_list_users", {"offset": "-1"})["code"] == "InvalidArgument"
            rows = admin.call("admin_list_users", {"offset": "0"})["payload"]["users"]
            reader_id = next(r["user_id"] for r in rows if r["username"] == "reader")
            admin_id = next(r["user_id"] for r in rows if r["is_admin"])
            assert admin.call("admin_set_disabled", {"user_id": admin_id, "disabled": True})["code"] == "InvalidArgument"
            second_reader, _ = connect("reader")
            assert "payload" in admin.call("admin_set_disabled", {"user_id": reader_id, "disabled": True})
            second_reader.socket.settimeout(2)
            assert second_reader.stream.read(1) == b""
            reader.socket.settimeout(2)
            assert reader.stream.read(1) == b""
            denied, response = connect("reader")
            assert response["code"] == "AuthenticationFailed"
            assert "payload" in admin.call("admin_set_disabled", {"user_id": reader_id, "disabled": False})
            reader, _ = connect("reader")
            assert "payload" in admin.call("admin_reset_password", {"user_id": reader_id, "password": "new-pass-123"})
            reader.socket.settimeout(2)
            assert reader.stream.read(1) == b""
            _, response = connect("reader")
            assert response["code"] == "AuthenticationFailed"
            reader, response = connect("reader", "new-pass-123")
            assert "payload" in response
            with closing(sqlite3.connect(database)) as db, db:
                db.execute("CREATE TRIGGER reject_audit BEFORE INSERT ON admin_audit BEGIN SELECT RAISE(ABORT,'test rollback'); END")
            assert admin.call("admin_reset_password", {"user_id": reader_id, "password": "failed-pass-123"})["code"] == "ServerError"
            assert admin.call("admin_create_user", {"username": "rolled-back", "password": "test-pass-123"})["code"] == "InvalidArgument"
            with closing(sqlite3.connect(database)) as db:
                assert db.execute("SELECT COUNT(*) FROM network_users WHERE username_key='rolled-back'").fetchone() == (0,)
            _, response = connect("reader", "new-pass-123")
            assert "payload" in response
            with closing(sqlite3.connect(database)) as db, db:
                db.execute("DROP TRIGGER reject_audit")
                # Seed old-format users to exercise pagination without creating passwords repeatedly.
                for i in range(101):
                    db.execute("INSERT INTO network_users(username,username_key,password_hash) SELECT ?,?,password_hash FROM network_users LIMIT 1", (f"page{i}", f"page{i}"))
            page = admin.call("admin_list_users", {"offset": "0"})["payload"]
            assert len(page["users"]) == 100 and page["has_more"]
            assert len(admin.call("admin_list_users", {"offset": "100"})["payload"]["users"]) == 4
            private = user.call("create_warehouse", {"name": "private", "visibility": "private"})["payload"]["warehouse"]["warehouse_id"]
            public = user.call("create_warehouse", {"name": "public", "visibility": "public"})["payload"]["warehouse"]["warehouse_id"]
            assert admin.call("get_warehouse", {"warehouse_id": private})["code"] == "PermissionDenied"
            media = {"id": "test", "file_name": "Song", "file_type": "MP3", "file_size": "0", "director": "Queen", "actors": [], "view_count": "0", "release_date": "2020-01-01", "awards": [], "category": "音乐"}
            assert reader.call("add_item", {"warehouse_id": public, "media": media})["code"] == "PermissionDenied"
            item = admin.call("add_item", {"warehouse_id": public, "media": media})["payload"]["item"]["item_id"]
            assert "payload" in admin.call("remove_item", {"warehouse_id": public, "item_id": item})
            assert "payload" in user.call("add_item", {"warehouse_id": private, "media": media})
            with closing(sqlite3.connect(database)) as db, db:
                assert db.execute("SELECT COUNT(*) FROM admin_audit").fetchone()[0] >= 4
                assert db.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        finally:
            for c in clients:
                c.close()
            process.terminate()
            process.wait(timeout=10)
        process, port = start()
        try:
            c = Connection(port)
            try:
                assert c.login("reader", "new-pass-123")["payload"]["user"]["is_admin"] is False
            finally:
                c.close()
        finally:
            process.terminate()
            process.wait(timeout=10)
    print("Admin migration, authorization, revocation, rollback, pagination and restart checks passed")


if __name__ == "__main__":
    run(Path(sys.argv[1]).resolve())
