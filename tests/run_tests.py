#!/usr/bin/env python3
"""Run the crequests network tests against a temporary local HTTP server."""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "tests", "mock_server.py")


def main():
    executable = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "build", "test_crequests.exe")
    if not os.path.exists(executable):
        print("test executable not found: %s" % executable, file=sys.stderr)
        return 1

    port_file = tempfile.NamedTemporaryFile(
        prefix="crequests-port-", suffix=".txt", delete=False)
    port_path = port_file.name
    port_file.close()
    try:
        os.remove(port_path)
    except OSError:
        pass

    server = subprocess.Popen(
        [sys.executable, SERVER, port_path],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    base = None
    try:
        for _ in range(100):
            if os.path.exists(port_path):
                with open(port_path, "r") as handle:
                    port = handle.read().strip()
                if port:
                    base = "http://127.0.0.1:%s" % port
                    break
            if server.poll() is not None:
                break
            time.sleep(0.05)
        if base is None:
            if server.poll() is None:
                server.terminate()
            output = server.stdout.read().decode("utf-8", "replace")
            print("mock server did not start:\n" + output, file=sys.stderr)
            return 1

        result = subprocess.run([executable, base], cwd=ROOT)
        return result.returncode
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
        try:
            os.remove(port_path)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
