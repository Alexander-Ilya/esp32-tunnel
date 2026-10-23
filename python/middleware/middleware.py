# MARK: Middleware — CORS, security headers, body-size limit, rate limiting.
import os
import time
from collections import defaultdict

from fastapi import FastAPI, Request, Response
from fastapi.middleware.cors import CORSMiddleware

MAX_BODY_BYTES = 1 * 1024 * 1024  # 1 MB
RATE_LIMIT = int(os.environ.get("RATE_LIMIT", 60))   # requests per window per IP
RATE_WINDOW = int(os.environ.get("RATE_WINDOW", 60))  # window in seconds

# MARK: Simple in-memory rate limiter
_hits: dict[str, list[float]] = defaultdict(list)
_last_cleanup = 0.0


def client_ip(request: Request) -> str:
    for hdr in ("cf-connecting-ip", "x-real-ip"):
        ip = request.headers.get(hdr, "")
        if ip:
            return ip
    forwarded = request.headers.get("x-forwarded-for", "")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return request.client.host if request.client else ""


def _rate_check(ip: str) -> bool:
    global _last_cleanup
    now = time.time()
    cutoff = now - RATE_WINDOW
    # drop idle IPs once a minute
    if now - _last_cleanup > 60:
        _last_cleanup = now
        for k in [k for k, v in _hits.items() if not v or v[-1] < cutoff]:
            del _hits[k]
    bucket = _hits[ip]
    while bucket and bucket[0] < cutoff:
        bucket.pop(0)
    if len(bucket) >= RATE_LIMIT:
        return False
    bucket.append(now)
    return True


def add_middlewares(app: FastAPI) -> None:
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],  # open by design — the dashboard/clients are cross-origin
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.middleware("http")
    async def security_middleware(request: Request, call_next) -> Response:
        ip = client_ip(request)
        if ip and not _rate_check(ip):
            return Response("Rate limit exceeded", status_code=429)

        cl = request.headers.get("content-length")
        if cl:
            try:
                if int(cl) > MAX_BODY_BYTES:
                    return Response("Request too large", status_code=413)
            except ValueError:
                return Response("Invalid Content-Length", status_code=400)

        response = await call_next(request)
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Referrer-Policy"] = "strict-origin-when-cross-origin"
        return response
