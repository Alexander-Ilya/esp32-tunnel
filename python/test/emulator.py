"""ESP32 emulator — speaks the esp32-tunnel WS protocol, no hardware needed.

As a library (tests):
    async with FakeESP32(id="dev", key="s3cret") as dev:
        ...                      # device is connected and answering

Standalone (drive the dashboard against a virtual device):
    uv run python test/emulator.py --id demo --key s3cret
    # then open the server's dashboard and talk to "demo"
"""
import argparse
import asyncio
import json

import websockets


class FakeESP32:
    def __init__(self, server="ws://localhost:8000", id="emu", key="",
                 handler=None):
        self.server, self.id, self.key = server, id, key
        self.handler = handler or self._default      # (method, path, body) -> (status, body, ctype)
        self._ws = self._task = None

    def _default(self, method, path, body):
        if path.rstrip("/") in ("", "/status"):
            return 200, json.dumps({"device": self.id, "heap": 128000}), "application/json"
        return 200, f"{method} {path}", "text/plain"

    async def _serve(self, ws):
        while True:
            try:
                m = json.loads(await asyncio.wait_for(ws.recv(), 20))
            except TimeoutError:                     # like the firmware: the relay drops silent devices
                await ws.send('{"type":"ping"}')
                continue
            if m.get("method"):
                status, body, ctype = self.handler(m["method"], m.get("path", "/"), m.get("body", ""))
                await ws.send(json.dumps({"id": m["id"], "status": status, "body": body, "type": ctype}))

    async def __aenter__(self):
        self._ws = await websockets.connect(f"{self.server}/api/tunnel/ws?id={self.id}",
                                            additional_headers={"X-Tunnel-Key": self.key} if self.key else None)
        self._task = asyncio.create_task(self._serve(self._ws))
        return self

    async def __aexit__(self, *_):
        self._task.cancel()
        await self._ws.close()

    async def run_forever(self):
        async with self:
            print(f"[emulator] {self.id} connected to {self.server} "
                  f"({'public' if not self.key else 'key=' + self.key})")
            await asyncio.Future()   # until Ctrl+C


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="ws://localhost:8000")
    ap.add_argument("--id", default="emu")
    ap.add_argument("--key", default="")
    a = ap.parse_args()
    try:
        asyncio.run(FakeESP32(a.server, a.id, a.key).run_forever())
    except KeyboardInterrupt:
        pass
