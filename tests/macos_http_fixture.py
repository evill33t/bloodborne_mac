"""Run the Mac transport executable against an isolated loopback HTTP server."""
import http.server
import os
import subprocess
import sys
import threading


class Echo(http.server.BaseHTTPRequestHandler):
    requests = 0

    def do_POST(self):
        payload = self.rfile.read(int(self.headers["Content-Length"]))
        if self.path != "/echo" or self.headers.get("X-BBHost-Ruleset") != "vanilla":
            self.send_error(400)
            return
        type(self).requests += 1
        self.send_response(200)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    try:
        local_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Echo)
    except PermissionError:
        print("Skipped: this sandbox does not permit a loopback test listener.")
        sys.exit(77)
    with local_server as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run(
                [sys.argv[1], f"http://127.0.0.1:{server.server_port}/echo"],
                env={**os.environ, "NO_PROXY": "127.0.0.1", "no_proxy": "127.0.0.1"},
                timeout=20,
            )
            if result.returncode:
                sys.exit(result.returncode)
            assert Echo.requests == 2
        finally:
            server.shutdown()
            thread.join()
