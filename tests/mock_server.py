#!/usr/bin/env python3
"""Local HTTP server used by the crequests test-suite.

Usage: python tests/mock_server.py [port-file]

The server binds to 127.0.0.1 on an ephemeral port and writes that port to
port-file so the test runner can discover it.
"""
import base64
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qsl, urlsplit


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "crequests-test"

    def log_message(self, fmt, *args):
        pass

    def _read_body(self):
        try:
            length = int(self.headers.get("Content-Length", "0") or "0")
        except ValueError:
            length = 0
        return self.rfile.read(length) if length > 0 else b""

    def _send(self, status, body=b"", headers=None, reason=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status, reason)
        headers = headers or []
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)
        self.close_connection = True

    def _echo(self):
        body = self._read_body()
        parts = urlsplit(self.path)
        lines = [
            "method=" + self.command,
            "path=" + parts.path,
            "query=" + parts.query,
        ]
        for name in ("x-test", "x-session", "authorization", "cookie",
                     "content-type", "content-length", "host", "user-agent",
                     "accept", "accept-encoding", "connection"):
            values = self.headers.get_all(name) or []
            for value in values:
                lines.append("header:%s=%s" % (name, value))
        text = body.decode("utf-8", "replace")
        lines.append("body=" + text)
        self._send(200, "\n".join(lines) + "\n",
                   [("Content-Type", "text/plain; charset=utf-8")])

    def _dispatch(self):
        parts = urlsplit(self.path)
        path = parts.path
        query = dict(parse_qsl(parts.query, keep_blank_values=True))

        if path in ("/", "/echo", "/get"):
            self._echo()
            return
        if path.startswith("/status/"):
            try:
                status = int(path.rsplit("/", 1)[1])
            except ValueError:
                status = 400
            self._send(status, "status %d" % status)
            return
        if path == "/reason":
            self._send(418, "teapot", reason="I am a teapot")
            return
        if path.startswith("/redirect/"):
            try:
                count = int(path.rsplit("/", 1)[1])
            except ValueError:
                count = 0
            target = "/echo" if count <= 0 else "/redirect/%d" % (count - 1)
            self._send(302, "redirect", [("Location", target)])
            return
        if path == "/redirect-post":
            self._send(302, "redirect", [("Location", "/echo")])
            return
        if path == "/redirect301":
            self._send(301, "redirect", [("Location", "/echo")])
            return
        if path == "/redirect303":
            self._send(303, "redirect", [("Location", "/echo")])
            return
        if path == "/redirect307":
            self._send(307, "redirect", [("Location", "/echo")])
            return
        if path == "/redirect308":
            self._send(308, "redirect", [("Location", "/echo")])
            return
        if path == "/redirect-loop":
            self._send(302, "loop", [("Location", "/redirect-loop")])
            return
        if path == "/redirect-relative":
            self._send(302, "relative", [("Location", "echo?from=relative")])
            return
        if path == "/redirect-localhost":
            port = self.server.server_port
            target = "http://localhost:%d/echo" % port
            self._send(302, "cross-origin", [("Location", target)])
            return
        if path == "/redirect-to":
            target = query.get("url", "/echo")
            self._send(302, "redirect", [("Location", target)])
            return
        if path == "/cookies/set":
            headers = []
            for name, value in parse_qsl(parts.query, keep_blank_values=True):
                headers.append(("Set-Cookie", "%s=%s; Path=/" % (name, value)))
            self._send(302, "cookies", headers + [("Location", "/cookies")])
            return
        if path == "/cookies/attributes":
            headers = [
                ("Set-Cookie", "a=1; Path=/; HttpOnly"),
                ("Set-Cookie", "b=2; Path=/deep"),
                ("Set-Cookie", "secure=1; Path=/; Secure"),
                ("Set-Cookie", "gone=1; Path=/; Max-Age=0"),
            ]
            self._send(302, "cookies", headers + [("Location", "/cookies")])
            return
        if path == "/cookies/case":
            headers = [
                ("Set-Cookie", "Case=1; Path=/"),
                ("Set-Cookie", "case=2; Path=/"),
            ]
            self._send(302, "cookies", headers + [("Location", "/cookies")])
            return
        if path == "/cookies":
            cookie = self.headers.get("Cookie", "")
            self._send(200, "cookie=" + cookie)
            return
        if path.startswith("/basic-auth/"):
            bits = path.split("/")
            if len(bits) >= 4:
                expected = base64.b64encode(
                    (bits[2] + ":" + bits[3]).encode("utf-8")).decode("ascii")
                actual = self.headers.get("Authorization", "")
                if actual == "Basic " + expected:
                    self._send(200, "authenticated")
                    return
            self._send(401, "unauthorized",
                       [("WWW-Authenticate", 'Basic realm="test"')])
            return
        if path == "/bearer":
            if self.headers.get("Authorization") == "Bearer secret-token":
                self._send(200, "authenticated")
            else:
                self._send(401, "unauthorized",
                           [("WWW-Authenticate", "Bearer")])
            return
        if path == "/chunked":
            chunks = [b"Wiki", b"pedia", b"!"]
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("X-Chunked", "yes")
            self.send_header("Connection", "close")
            self.end_headers()
            for chunk in chunks:
                self.wfile.write(("%X\r\n" % len(chunk)).encode("ascii"))
                self.wfile.write(chunk + b"\r\n")
            self.wfile.write(b"0\r\nX-Trailer: done\r\n\r\n")
            self.close_connection = True
            return
        if path == "/no-content":
            self._send(204, b"")
            return
        if path == "/empty":
            self._send(200, b"")
            return
        if path == "/head":
            self._send(200, b"head-body")
            return
        if path == "/slow":
            try:
                delay = int(query.get("ms", "500"))
            except ValueError:
                delay = 500
            time.sleep(max(0, delay) / 1000.0)
            self._send(200, "slow")
            return
        if path == "/large":
            try:
                count = int(query.get("n", "1000"))
            except ValueError:
                count = 1000
            self._send(200, b"x" * max(0, count))
            return
        if path == "/multi-header":
            self._send(200, "multi", [
                ("X-Multi", "one"),
                ("X-Multi", "two"),
                ("Set-Cookie", "first=1; Path=/"),
                ("Set-Cookie", "second=2; Path=/"),
            ])
            return
        if path == "/utf8":
            self._send(200, "你好, crequests",
                       [("Content-Type", "text/plain; charset=utf-8")])
            return
        if path == "/bad-length":
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"short")
            self.close_connection = True
            return
        if path == "/continue":
            self.wfile.write(b"HTTP/1.1 100 Continue\r\n\r\n")
            self.wfile.flush()
            self._send(200, "after-continue")
            return
        if path == "/upload":
            body = self._read_body()
            content_type = self.headers.get("Content-Type", "")
            self._send(200, "type=" + content_type + "\nbody=" +
                       body.decode("utf-8", "replace"))
            return
        self._send(404, "not found")

    do_GET = _dispatch
    do_POST = _dispatch
    do_PUT = _dispatch
    do_PATCH = _dispatch
    do_DELETE = _dispatch
    do_HEAD = _dispatch
    do_OPTIONS = _dispatch


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w") as handle:
            handle.write(str(server.server_port))
    print("listening on 127.0.0.1:%d" % server.server_port, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
