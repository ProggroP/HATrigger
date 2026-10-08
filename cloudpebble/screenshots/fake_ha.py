# Nur fuer Store-Screenshots und Tests: antwortet auf 127.0.0.1:8123 wie
# eine Home-Assistant-REST-API. Der Zustand haengt an der Entity.
from http.server import BaseHTTPRequestHandler, HTTPServer
import json

STATES = {
    "lock.front_door":   "locked",
    "cover.garage":      "closed",
    "light.living_room": "on",
    "switch.coffee_maker": "off",
    "cover.gate":        "open",
}

class H(BaseHTTPRequestHandler):
    def _send(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        entity = self.path.rsplit("/", 1)[-1]
        print("GET", entity, flush=True)
        self._send({"entity_id": entity, "state": STATES.get(entity, "unknown")})
    def do_POST(self):
        print("POST", self.path, self.rfile.read(int(self.headers.get("Content-Length", 0))), flush=True)
        self._send([])
    def log_message(self, *a):
        pass

HTTPServer(("127.0.0.1", 8123), H).serve_forever()
