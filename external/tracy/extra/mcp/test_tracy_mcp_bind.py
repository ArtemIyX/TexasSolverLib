import os
import unittest
from unittest import mock

from tracy_mcp_bind import resolve_bind_host


class TracyMcpBindTests(unittest.TestCase):
    def test_defaults_to_loopback(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(resolve_bind_host(), "127.0.0.1")

    def test_remote_requires_explicit_opt_in(self):
        with self.assertRaisesRegex(ValueError, "requires TRACY_MCP_ALLOW_REMOTE"):
            resolve_bind_host("0.0.0.0", "0")

    def test_remote_opt_in_is_explicit(self):
        self.assertEqual(resolve_bind_host("192.0.2.10", "yes"), "192.0.2.10")

    def test_invalid_hosts_fail_closed(self):
        for host in ("", "localhost", "::1", "not-an-address"):
            with self.subTest(host=host), self.assertRaises(ValueError):
                resolve_bind_host(host, "1")


if __name__ == "__main__":
    unittest.main()
