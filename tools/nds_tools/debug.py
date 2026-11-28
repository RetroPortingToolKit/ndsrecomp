"""Line-delimited JSON transport and owned capture-session support."""
from __future__ import annotations
import json
import socket
import subprocess
import time
from pathlib import Path

class DebugClient:
    def __init__(self, host="127.0.0.1", port=19842, timeout=None):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""

    def cmd(self, name, **args):
        args["cmd"] = name
        self.sock.sendall((json.dumps(args) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("debug server closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        resp = json.loads(line)
        if isinstance(resp, dict) and resp.get("error"):
            raise RuntimeError(f"{name}: {resp['error']}")
        return resp

    command = cmd

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # convenience wrappers
    def ping(self):
        return self.cmd("ping")

    def run_to_event(self, event, count):
        return self.cmd("run_to_event", event=event, count=count)

    def read_region(self, region):
        r = self.cmd("read_region", region=region)
        return bytes.fromhex(r["hex"] if isinstance(r, dict) else r)

    def framebuffer(self, engine, adaptive=False):
        r = self.cmd("framebuffer", engine=engine, adaptive=adaptive)
        return r["w"], r["h"], bytes.fromhex(r["rgb"])

    def event_counts(self):
        return self.cmd("event_counts")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

class CaptureClient(DebugClient):
    """Port-first compatibility for title capture tools."""
    def __init__(self, port, timeout=None):
        super().__init__(port=port, timeout=timeout)


def wait_for_server(port, process):
    # Readiness is observed; elapsed time does not kill healthy startup work.
    while True:
        if process.poll() is not None:
            raise RuntimeError(f"runner exited early with {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), 0.25):
                return
        except OSError:
            time.sleep(0.1)


def finish_owned(process, failure=None):
    """Close an owned completed capture; transport timeouts retain live work."""
    if isinstance(failure, TimeoutError):
        raise RuntimeError(f"Transport request timed out; runner {process.pid} remains running for inspection") from failure
    if process.poll() is None:
        process.terminate()
    process.wait()


def debug_command(port, request, timeout=None):
    with CaptureClient(port, timeout) as client:
        request = dict(request)
        name = request.pop("cmd")
        return client.cmd(name, **request)


def firmware_crc16(data: bytes, start: int = 0xFFFF) -> int:
    polynomial = (
        0xC0C1, 0xC181, 0xC301, 0xC601,
        0xCC01, 0xD801, 0xF001, 0xA001,
    )
    for value in data:
        start ^= value
        for bit in range(8):
            if start & 1:
                start = (start >> 1) ^ (polynomial[bit] << (7 - bit))
            else:
                start >>= 1
    return start & 0xFFFF

def automatic_firmware(source: Path, destination: Path) -> None:
    """Create an ignored private firmware copy with Slot-1 auto-start set."""
    firmware = bytearray(source.read_bytes())
    if len(firmware) < 0x22:
        raise ValueError("firmware image is truncated")
    user = (firmware[0x20] << 3) | (firmware[0x21] << 11)
    if user + 0x200 > len(firmware):
        raise ValueError("firmware user-settings layout is invalid")
    for copy in range(2):
        base = user + copy * 0x100
        flags = int.from_bytes(firmware[base + 0x64:base + 0x66], "little")
        firmware[base + 0x64:base + 0x66] = (
            flags | (1 << 6)
        ).to_bytes(2, "little")
        checksum = firmware_crc16(firmware[base:base + 0x70])
        firmware[base + 0x72:base + 0x74] = checksum.to_bytes(2, "little")
    destination.write_bytes(firmware)
