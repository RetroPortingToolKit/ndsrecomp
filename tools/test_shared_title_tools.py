"""Focused public tests for shared title tooling (no retail image required)."""
import json
from pathlib import Path
import socket
import tempfile
import threading
import types
import unittest
from nds_tools import cartridge, debug, release


class SharedToolTests(unittest.TestCase):
    def test_overlay_rejects_size_and_address_wrap_before_write(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            overlay = types.SimpleNamespace(data=b'123', ramSize=4, ramAddress=0x02000000, bssSize=0)
            with self.assertRaises(SystemExit):
                cartridge.extract_overlay(2, overlay, out)
            overlay.ramSize = 3
            overlay.ramAddress = 0xffffffff
            with self.assertRaises(SystemExit):
                cartridge.extract_overlay(2, overlay, out)
            self.assertEqual(list(out.iterdir()), [])

    def test_fragmented_and_buffered_protocol(self):
        server = socket.socket()
        self.addCleanup(server.close)
        server.bind(('127.0.0.1', 0))
        server.listen()
        def serve():
            conn, _ = server.accept()
            with conn:
                conn.recv(4096)
                conn.sendall(b'{"v":')
                conn.sendall(b'1}\n{"v":2}\n')
                conn.recv(4096)
        thread = threading.Thread(target=serve)
        thread.start()
        with debug.CaptureClient(server.getsockname()[1]) as client:
            self.assertEqual(client.command('a'), {'v': 1})
            self.assertEqual(client.cmd('b'), {'v': 2})
        thread.join()

    def test_transport_timeout_preserves_owned_runner(self):
        class Process:
            pid = 12
            def terminate(self):
                raise AssertionError('healthy runner was terminated')
        with self.assertRaisesRegex(RuntimeError, 'remains running'):
            debug.finish_owned(Process(), TimeoutError())

    def test_archive_checks_payload_and_contents(self):
        for name in ('../bad', '/bad', 'C:/bad', 'bad\\name', 'game.nds', 'generated/a.c', 'save.sav'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.validate_name(name)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            stage = out / 'stage'
            stage.mkdir()
            (stage / 'README.txt').write_text('fixture')
            digest = release.archive_stage(stage, out / 'fixture.zip')
            self.assertEqual(digest, release.sha256(out / 'fixture.zip'))


if __name__ == '__main__':
    unittest.main()
