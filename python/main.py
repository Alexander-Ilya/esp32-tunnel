# MARK: esp32-tunnel relay server
"""
Deploy on Render.com (or any Python host with WebSocket support).
ESP32 connects via WebSocket, visitors access via HTTP.

  WS   /api/tunnel/ws?id=<name>  — ESP32 tunnel
  ANY  /<name>/<path>            — visitor -> ESP32
  GET  /api/status               — health check
"""

import importlib
import os
import pathlib
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request

from middleware import add_middlewares, log
from api.tunnel import tunnel_proxy

PORT = int(os.environ.get("PORT", 8000))


# MARK: Startup/shutdown hook — add init/cleanup here
@asynccontextmanager
async def lifespan(app: FastAPI):
    log.info(f"Server: http://localhost:{PORT}")
    yield


app = FastAPI(title="esp32-tunnel", lifespan=lifespan)
add_middlewares(app)


# MARK: Every api/*.py module with a `router` is mounted under /api — no wiring here.
def _include_all_routers(directory: str, prefix: str):
    api_dir = pathlib.Path(__file__).parent / directory
    for py in sorted(api_dir.rglob("*.py")):
        if py.name.startswith("_"):
            continue
        module = f"{api_dir.name}." + ".".join(py.relative_to(api_dir).with_suffix("").parts)
        router = getattr(importlib.import_module(module), "router", None)
        if router is not None:
            app.include_router(router, prefix=prefix)


_include_all_routers("api", "/api")


# MARK: Catch-all proxy — /{tid} and /{tid}/... (after the /api routes)
@app.api_route("/{tid}{rest:path}", methods=["GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"])
async def proxy(tid: str, request: Request):
    return await tunnel_proxy(tid, request)


# MARK: Dashboard at "/" — path operations always win over frontend files.
# https://fastapi.tiangolo.com/tutorial/frontend/
app.frontend("/", directory=pathlib.Path(__file__).parent / "frontend")


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="0.0.0.0", port=PORT,
                ws_ping_interval=20, ws_ping_timeout=30)
