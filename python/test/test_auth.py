"""End-to-end auth test: spins up the real app + a FakeESP32, checks access
control over real HTTP/WS. No hardware. Run: uv run python test/test_auth.py
"""
import asyncio
import pathlib
import sys
import threading
import time
import urllib.error
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).parent.parent))   # repo: python/

import uvicorn

from emulator import FakeESP32
from main import app

BASE = "http://127.0.0.1:8011"


def get(devid, key=None, where="query"):
    url = f"{BASE}/{devid}/status"
    req = urllib.request.Request(url)
    if key and where == "query":
        req.full_url = f"{url}?key={key}"
    elif key and where == "header":
        req.add_header("X-Tunnel-Key", key)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code


def fetch(path, data=None, headers={}):
    try:
        with urllib.request.urlopen(urllib.request.Request(BASE + path, data=data, headers=headers), timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, ""


async def main():
    loop = asyncio.get_running_loop()
    async def g(*a, **k): return await loop.run_in_executor(None, lambda: get(*a, **k))
    async def f(*a): return await loop.run_in_executor(None, lambda: fetch(*a))

    results = []
    def ok(name, got, want): results.append((name, got, want, got == want))

    # ── secured device ──
    async with FakeESP32(f"ws://127.0.0.1:8011", "secdev", "s3cret"):
        await asyncio.sleep(0.4)
        ok("no key -> 401",            await g("secdev"), 401)
        ok("?key= -> 200",             await g("secdev", "s3cret", "query"), 200)
        ok("header -> 200",            await g("secdev", "s3cret", "header"), 200)
        ok("wrong key -> 401",         await g("secdev", "nope", "query"), 401)
        ok("POST + header -> 200",     (await f("/secdev/echo", b"hi", {"X-Tunnel-Key": "s3cret"}))[0], 200)
        ok("POST + ?key= -> 200",      (await f("/secdev/echo?key=s3cret", b"hi"))[0], 200)
        ok("POST, no key -> 401",      (await f("/secdev/echo", b"hi"))[0], 401)
        two = await asyncio.gather(g("secdev", "s3cret"), g("secdev", "s3cret"))
        ok("2 clients -> 200/200",     tuple(two), (200, 200))
        import websockets
        for name, url, hdrs in (("hijack -> rejected", "", {"X-Tunnel-Key": "WRONG"}),
                                ("old firmware -> rejected", "&token=s3cret", None)):
            try:                        # same id, wrong key
                async with websockets.connect(f"ws://127.0.0.1:8011/api/tunnel/ws?id=secdev{url}",
                                              additional_headers=hdrs) as w:
                    await w.recv(); hij = "not-closed"
            except Exception:
                hij = "rejected"
            ok(name, hij, "rejected")

    # ── public device ──
    async with FakeESP32(f"ws://127.0.0.1:8011", "pubdev", ""):
        await asyncio.sleep(0.4)
        ok("public, no key -> 200", await g("pubdev"), 200)

        # ── proxy: what the device actually receives ──
        ok("query kept, key stripped", await f("/pubdev/set?temp=20&key=x&on=1"), (200, "GET /set?temp=20&on=1"))
        ok("encoded path kept",        await f("/pubdev/a%20b"), (200, "GET /a%20b"))
        ok("dot-dot -> 400",           (await f("/pubdev/a/%2e%2e/b"))[0], 400)
        ok("oversize body -> 413",     (await f("/pubdev/echo", b"x" * 20000))[0], 413)
        ok("small POST -> 200",        await f("/pubdev/echo", b"hi"), (200, "POST /echo"))

    for name, got, want, good in results:
        print(f"  [{'OK' if good else 'FAIL'}] {name:24} got={got}")
    if not all(r[3] for r in results):
        raise SystemExit("auth test FAILED")
    print("auth e2e ok")


if __name__ == "__main__":
    server = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=8011, log_level="error"))
    threading.Thread(target=server.run, daemon=True).start()
    for _ in range(50):                                   # wait until listening
        try:
            urllib.request.urlopen(f"{BASE}/api/status", timeout=1); break
        except Exception:
            time.sleep(0.1)
    asyncio.run(main())
    server.should_exit = True
