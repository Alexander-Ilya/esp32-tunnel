import logging

from .middleware import add_middlewares, client_ip

# ponytail: reuse uvicorn's logger — same format as its access log, no setup
log = logging.getLogger("uvicorn.error")
