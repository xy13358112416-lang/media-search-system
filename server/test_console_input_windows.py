"""Exercise password hiding in a real, hidden Windows console (temporary DB only)."""
import ctypes
from ctypes import wintypes as W
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

if os.name != "nt":
    raise SystemExit(0)

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.LPVOID, W.DWORD, W.DWORD, W.HANDLE]
kernel.CreateFileW.restype = W.HANDLE
kernel.GetConsoleMode.argtypes = [W.HANDLE, ctypes.POINTER(W.DWORD)]
kernel.ReadConsoleOutputCharacterW.argtypes = [W.HANDLE, W.LPWSTR, W.DWORD, W.DWORD, ctypes.POINTER(W.DWORD)]
kernel.CloseHandle.argtypes = [W.HANDLE]


class Key(ctypes.Structure):
    _fields_ = [("down", W.BOOL), ("repeat", W.WORD), ("virtual", W.WORD),
                ("scan", W.WORD), ("char", W.WCHAR), ("control", W.DWORD)]


class Event(ctypes.Union):
    _fields_ = [("key", Key), ("padding", ctypes.c_byte * 16)]


class Record(ctypes.Structure):
    _fields_ = [("kind", W.WORD), ("event", Event)]


kernel.WriteConsoleInputW.argtypes = [W.HANDLE, ctypes.POINTER(Record), W.DWORD, ctypes.POINTER(W.DWORD)]


with tempfile.TemporaryDirectory() as temporary:
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(Path(sys.argv[1]).resolve()), "--db", str(Path(temporary)/"console.sqlite"),
                                "--setup-admin"], creationflags=subprocess.CREATE_NEW_CONSOLE,
                               startupinfo=startup, close_fds=True)
    handles = []
    attached = False
    try:
        kernel.FreeConsole()  # Detach this helper only; never touches the parent shell.
        for _ in range(100):
            if kernel.AttachConsole(process.pid):
                attached = True
                break
            assert process.poll() is None
            time.sleep(.03)
        assert attached, "Cannot attach hidden test console"
        for name in ("CONIN$", "CONOUT$"):
            handle = kernel.CreateFileW(name, 0xC0000000, 3, None, 3, 0, None)
            assert handle != ctypes.c_void_p(-1).value
            handles.append(handle)
        input_handle, output_handle = handles

        def screen():
            value = ctypes.create_unicode_buffer(16000)
            count = W.DWORD()
            assert kernel.ReadConsoleOutputCharacterW(output_handle, value, 16000, 0, ctypes.byref(count))
            return value.value

        def wait_for(value):
            until = time.monotonic()+10
            while value not in screen():
                assert process.poll() is None, "Server exited before input prompt"
                assert time.monotonic()<until, "Console prompt timeout"
                time.sleep(.03)

        def type_line(value):
            records = (Record * (len(value)+1))()
            for record, character in zip(records, value+"\r"):
                record.kind = 1
                record.event.key = Key(True, 1, 13 if character=="\r" else 0, 0, character, 0)
            count = W.DWORD()
            assert kernel.WriteConsoleInputW(input_handle, records, len(records), ctypes.byref(count))
            assert count.value == len(records)

        wait_for("请输入管理员用户名")
        type_line("console-operator")
        wait_for("请输入密码")
        mode = W.DWORD()
        assert kernel.GetConsoleMode(input_handle, ctypes.byref(mode))
        assert not mode.value & 4, "Password echo was enabled"
        type_line("secret-console-123")
        wait_for("请再次输入密码")
        assert kernel.GetConsoleMode(input_handle, ctypes.byref(mode))
        assert not mode.value & 4, "Confirmation echo was enabled"
        type_line("secret-console-123")
        assert process.wait(timeout=15)==0
        assert kernel.GetConsoleMode(input_handle, ctypes.byref(mode))
        assert mode.value & 4, "Console echo was not restored"
        assert "secret-console-123" not in screen(), "Password appeared in console output"
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        for handle in handles:
            kernel.CloseHandle(handle)
        if attached:
            kernel.FreeConsole()
print("Hidden console password input, confirmation and echo restoration passed")
