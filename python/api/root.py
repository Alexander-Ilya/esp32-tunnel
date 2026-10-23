# MARK: Status endpoint — cgroup-aware, so Render/Docker report the container's limits
import os
import time
from pathlib import Path

import psutil
from fastapi import APIRouter
from pydantic import BaseModel

from api.tunnel import tunnels

router = APIRouter(tags=["Root"])

_start = time.time()
_MB = 1024 * 1024


# MARK: First readable cgroup value (v2 path first, then v1); None = missing/unlimited.
# `field` picks a column — cgroup v2 cpu.max is "quota period".
def _cgroup(*paths: str, field: int = 0) -> int | None:
    for p in paths:
        try:
            parts = Path(p).read_text().split()
            v = int(parts[min(field, len(parts) - 1)])
        except (OSError, ValueError, IndexError):   # missing file, or "max"
            continue
        return v if 0 < v < 2**62 else None          # v1 "unlimited" is ~2**63
    return None


class Memory(BaseModel):
    limit_mb: float
    used_mb: float
    free_mb: float
    process_mb: float
    percent: float


class Status(BaseModel):
    ok: bool
    tunnels: int
    uptime: int
    memory: Memory
    cpu_count: float


@router.get("/status", response_model=Status, summary="System status")
async def status():
    """Container (or host) memory/CPU, active tunnels, uptime."""
    limit = _cgroup("/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory/memory.limit_in_bytes")
    used = _cgroup("/sys/fs/cgroup/memory.current", "/sys/fs/cgroup/memory/memory.usage_in_bytes")
    if not (limit and used):
        vm = psutil.virtual_memory()
        limit, used = vm.total, vm.used

    quota = _cgroup("/sys/fs/cgroup/cpu.max", "/sys/fs/cgroup/cpu/cpu.cfs_quota_us")
    period = _cgroup("/sys/fs/cgroup/cpu.max", "/sys/fs/cgroup/cpu/cpu.cfs_period_us", field=1) or 100000
    cpus = quota / period if quota else (psutil.cpu_count() or 1)

    percent = used / limit * 100
    return Status(
        ok=percent < 95,
        tunnels=len(tunnels),
        uptime=int(time.time() - _start),
        memory=Memory(
            limit_mb=round(limit / _MB, 1),
            used_mb=round(used / _MB, 1),
            free_mb=round((limit - used) / _MB, 1),
            process_mb=round(psutil.Process(os.getpid()).memory_info().rss / _MB, 1),
            percent=round(percent, 1),
        ),
        cpu_count=round(cpus, 2),
    )
