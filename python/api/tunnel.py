# MARK: ESP32 Tunnel — WebSocket relay + HTTP proxy
"""
ESP32 connects via WebSocket, visitors access via HTTP proxy.

  WS   /api/tunnel/ws?id=<device>   — ESP32 connects here
  ANY  /<device>/<path>?<query>     — Visitors reach ESP32 here
"""

import asyncio
import hmac
import json
import re
import time
import uuid

from fastapi import APIRouter, WebSocket, Request, HTTPException
from fastapi.responses import Response
from urllib.parse import unquote

from middleware import log, client_ip

router = APIRouter(tags=["Tunnel"])

# MARK: Config
TIMEOUT = 30
GRACE_PERIOD = 15
MAX_PATH_LEN = 2048
DEFAULT_MAX = 16384            # device's TUN_MAX_BODY when it doesn't announce one (&max=)
MAX_REPLY_LEN = 4 * 1024 * 1024   # largest device reply we parse (body is JSON-escaped)
_VALID_ID = re.compile(r"^[a-zA-Z0-9_-]{1,64}$")
_FORWARD_HEADERS = ("authorization", "cookie", "user-agent", "accept", "accept-language")
_KEY_PARAMS = ("key", "x-tunnel-key")


# MARK: Tunnel — isolates per-connection state
class Tunnel:
    __slots__ = ("ws", "routes", "pending", "token", "max_msg")

    def __init__(self, ws: WebSocket, token: str = "", max_msg: int = DEFAULT_MAX):
        self.ws = ws
        self.max_msg = max_msg   # largest WS frame the device accepts
        self.routes: list[dict] | None = None   # set once by the device's config msg
        self.pending: dict[str, asyncio.Future] = {}
        self.token = token   # device access key; "" = open (no auth)

    def check_route(self, path: str, key: str) -> bool:
        for r in self.routes or ():
            if path.startswith(r.get("path", "/")):
                pw = r.get("password")
                return not pw or hmac.compare_digest(key, pw)
        return True

    def cancel_pending(self):
        for fut in self.pending.values():
            if not fut.done():
                fut.set_result({"status": 502, "body": "Device disconnected", "type": "text/plain"})
        self.pending.clear()


# MARK: State (root.py reads `tunnels` for /api/status)
tunnels: dict[str, Tunnel] = {}
_grace: dict[str, float] = {}


# MARK: Helpers

# Path + query for the device, still %-encoded as the visitor sent it, minus the access key.
def _device_target(request: Request) -> str:
    raw = request.scope.get("raw_path", b"").decode("latin-1").split("/", 2)   # ['', tid, rest]
    path = "/" + (raw[2] if len(raw) > 2 else "")
    if ".." in unquote(path) or "\x00" in unquote(path):
        raise HTTPException(400, "Bad path")
    query = "&".join(p for p in request.url.query.split("&")
                     if p and p.split("=", 1)[0] not in _KEY_PARAMS)
    target = f"{path}?{query}" if query else path
    if len(target) > MAX_PATH_LEN:
        raise HTTPException(414, "URI too long")
    return target


def _pick_headers(request: Request) -> dict:
    return {h: request.headers[h] for h in _FORWARD_HEADERS if h in request.headers}


def _clean_grace():
    global _grace
    cutoff = time.time() - GRACE_PERIOD * 2
    _grace = {k: v for k, v in _grace.items() if v > cutoff}


# MARK: Auth — device key via ?key= or X-Tunnel-Key. No key on device = no check.
def _req_key(request: Request) -> str:
    return (request.query_params.get("key")
            or request.query_params.get("x-tunnel-key")
            or request.headers.get("x-tunnel-key", ""))


def _authorize(tun: Tunnel, request: Request):
    if tun.token and not hmac.compare_digest(_req_key(request), tun.token):
        raise HTTPException(401, "Unauthorized")


# MARK: WebSocket endpoint — ESP32 connects here
@router.websocket("/tunnel/ws")
async def tunnel_ws(ws: WebSocket, id: str, token: str = "", max: int = DEFAULT_MAX):
    if not _VALID_ID.match(id):
        await ws.close(4000, "invalid id")
        return

    await ws.accept()

    # Don't let a wrong key hijack a live device that claimed this id with a key.
    old = tunnels.get(id)
    if old and old.token and not hmac.compare_digest(token, old.token):
        await ws.close(4003, "id in use")
        return

    old = tunnels.pop(id, None)
    if old:
        try:
            await old.ws.close(4001, "replaced")
        except Exception:
            pass
        old.cancel_pending()

    tun = Tunnel(ws, token, max)
    tunnels[id] = tun
    _grace.pop(id, None)
    log.info(f"[tunnel] + {id} ({len(tunnels)} active)")

    try:
        while True:
            raw = await ws.receive_text()
            if len(raw) > MAX_REPLY_LEN:
                continue
            msg = json.loads(raw)

            # Route config: accepted once, then locked
            if msg.get("type") == "config":
                if tun.routes is None:
                    routes = msg.get("routes")
                    tun.routes = routes if isinstance(routes, list) else []
                continue

            rid = msg.get("id")
            if rid and rid in tun.pending:
                tun.pending[rid].set_result(msg)
    except Exception:   # WebSocketDisconnect, bad JSON, invalid UTF-8 — drop the device
        pass
    finally:
        if tunnels.get(id) is tun:
            del tunnels[id]
        _grace[id] = time.time()
        _clean_grace()
        tun.cancel_pending()
        log.info(f"[tunnel] - {id} ({len(tunnels)} active)")


# MARK: Request -> device -> reply (matched by id). 502 if the device is gone.
async def _rpc(tid: str, tun: Tunnel, msg: dict) -> dict:
    rid = msg["id"] = str(uuid.uuid4())
    data = json.dumps(msg)
    if len(data) > tun.max_msg:          # the device would drop it and desync its WS stream
        raise HTTPException(413, "Request too large for device")
    fut = asyncio.get_running_loop().create_future()
    tun.pending[rid] = fut
    try:
        try:
            await tun.ws.send_text(data)
        except Exception:
            if tunnels.get(tid) is tun:
                del tunnels[tid]
            _grace[tid] = time.time()
            raise HTTPException(502, "Device disconnected")
        return await asyncio.wait_for(fut, timeout=TIMEOUT)
    finally:
        tun.pending.pop(rid, None)


# MARK: HTTP proxy — visitors access ESP32 through this
async def tunnel_proxy(tid: str, request: Request):
    if not _VALID_ID.match(tid):
        raise HTTPException(400, "Invalid tunnel ID")

    tun = tunnels.get(tid)
    if not tun:
        if tid in _grace and time.time() - _grace[tid] < GRACE_PERIOD:
            raise HTTPException(503, "Tunnel unavailable")
        raise HTTPException(404, "Tunnel not found")

    target = _device_target(request)

    _authorize(tun, request)
    if not tun.check_route(target, _req_key(request)):
        raise HTTPException(403, "Access denied")

    msg = {"method": request.method, "path": target, "ip": client_ip(request)}
    if request.method in ("POST", "PUT", "PATCH"):
        body = await request.body()
        if body:
            msg["body"] = body.decode("utf-8", errors="replace")
        ct = request.headers.get("content-type", "")
        if ct:
            msg["ct"] = ct
    hdrs = _pick_headers(request)
    if hdrs:
        msg["hdrs"] = hdrs

    try:
        r = await _rpc(tid, tun, msg)
    except asyncio.TimeoutError:
        raise HTTPException(504, "Device timeout")
    return Response(
        r.get("body", ""),
        status_code=r.get("status", 200),
        media_type=r.get("type", "text/html; charset=utf-8"),
    )
