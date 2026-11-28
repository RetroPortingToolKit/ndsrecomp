"""Focused public tests for shared title tooling (no retail image required)."""
import json
from pathlib import Path
import socket
import tempfile
import threading
import types
import unittest
from unittest.mock import patch
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

    def test_nested_pe_dependencies_require_packaged_files(self):
        files = {"Runtime/player.exe": Path("player.exe"), "Runtime/SDL3.dll": Path("SDL3.dll")}
        with patch("nds_tools.release.subprocess.check_output", return_value="DLL Name: KERNEL32.dll\nDLL Name: SDL3.dll\n"):
            release.check_pe_dependencies(files, "objdump")
        with patch("nds_tools.release.subprocess.check_output", return_value="DLL Name: missing.dll\n"):
            with self.assertRaisesRegex(ValueError, "missing.dll"):
                release.check_pe_dependencies(files, "objdump")
        with patch("nds_tools.release.subprocess.check_output", return_value="DLL Name: python313.dll\n"):
            with self.assertRaisesRegex(ValueError, "python313.dll"):
                release.check_pe_dependencies(files, "objdump")
            release.check_pe_dependencies(files, "objdump", embedded_python=True)

    def test_runtime_dll_staging_is_complete_before_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            runtime, stage = root / "runtime", root / "stage"
            runtime.mkdir(); stage.mkdir()
            names = ("SDL2.dll", "SDL3.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "libiconv-2.dll")
            for name in names[:-1]:
                (runtime / name).write_bytes(name.encode())
            with self.assertRaisesRegex(ValueError, "libiconv"):
                release.stage_runtime_dlls(stage, runtime, ("SDL2", "SDL3"))
            self.assertEqual(list(stage.iterdir()), [])
            (runtime / names[-1]).write_bytes(names[-1].encode())
            self.assertEqual(set(release.stage_runtime_dlls(stage, runtime, ("SDL2", "SDL3"))), set(names))
            for name in names:
                self.assertEqual((stage / name).read_bytes(), (runtime / name).read_bytes())

    def test_archive_checks_payload_and_contents(self):
        for name in ('../bad', '/bad', 'C:/bad', 'bad\\name', 'game.nds', 'generated/a.c', 'Generated/a.c', 'save.sav', 'scene.ndss'):
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
