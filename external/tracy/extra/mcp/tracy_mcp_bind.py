"""Validate the Tracy MCP listening address before opening sockets."""

from __future__ import annotations

import ipaddress
import os


_DEFAULT_BIND_HOST = "127.0.0.1"
_REMOTE_OPT_IN = "TRACY_MCP_ALLOW_REMOTE"


def _is_enabled(value: str | None) -> bool:
    return (value or "").strip().lower() in {"1", "true", "yes", "on"}


def resolve_bind_host(
    bind_host: str | None = None,
    allow_remote: str | None = None,
) -> str:
    """Return a safe IPv4 bind address or raise ValueError.

    Remote binding is deliberately opt-in because the MCP server has no
    authentication layer. Hostnames and IPv6 literals are rejected so the
    effective socket address cannot vary with resolver or address-family
    configuration.
    """
    raw_host = os.environ.get("TRACY_MCP_BIND_HOST", _DEFAULT_BIND_HOST) if bind_host is None else bind_host
    host = raw_host.strip()
    if not host:
        raise ValueError("TRACY_MCP_BIND_HOST must be a non-empty IPv4 address")

    try:
        address = ipaddress.ip_address(host)
    except ValueError as exc:
        raise ValueError("TRACY_MCP_BIND_HOST must be an IPv4 address") from exc
    if address.version != 4:
        raise ValueError("TRACY_MCP_BIND_HOST must be an IPv4 address")

    remote_enabled = _is_enabled(
        os.environ.get(_REMOTE_OPT_IN) if allow_remote is None else allow_remote
    )
    if not address.is_loopback and not remote_enabled:
        raise ValueError(
            f"remote MCP binding to {host} requires {_REMOTE_OPT_IN}=1"
        )
    return str(address)
