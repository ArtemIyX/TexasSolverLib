from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


MODULE_PATH = Path(__file__).with_name("scan_ctest_errors.py")
SPEC = spec_from_file_location("scan_ctest_errors", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
scan_ctest_errors = module_from_spec(SPEC)
SPEC.loader.exec_module(scan_ctest_errors)


class ScanCtestErrorsTests(unittest.TestCase):
    def test_harness_failure_without_source_location(self) -> None:
        records = scan_ctest_errors.parse_failures(
            "[FAIL] multiway_external_row_index: external ordered iteration requires bounded run fan-in"
        )

        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["test"], "multiway_external_row_index")
        self.assertIsNone(records[0]["line"])
        self.assertEqual(
            records[0]["message"],
            "external ordered iteration requires bounded run fan-in",
        )

    def test_harness_failure_with_source_location(self) -> None:
        records = scan_ctest_errors.parse_failures(
            r"[FAIL] sample_test: C:\repo\tests\sample.cpp:42 expected value"
        )

        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["line"], "42")
        self.assertEqual(records[0]["message"], "expected value")

    def test_failed_ctest_summary_is_a_fallback(self) -> None:
        records = scan_ctest_errors.parse_failures(
            "The following tests FAILED:\n"
            "  63 - test_multiway_external_storage (Failed)\n"
            "Errors while running CTest\n"
        )

        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["test"], "test_multiway_external_storage")
        self.assertEqual(records[0]["message"], "CTest reported Failed")

    def test_failure_records_are_deduplicated_and_counted(self) -> None:
        records = scan_ctest_errors.parse_failures(
            "[FAIL] sample_test: expected value\n[FAIL] sample_test: expected value\n"
        )

        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["count"], 2)

    def test_missing_log_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            missing_log = root / "missing.log"
            output = root / "errors.txt"
            with patch.object(
                sys,
                "argv",
                ["scan_ctest_errors.py", str(missing_log), "--output", str(output)],
            ):
                result = scan_ctest_errors.main()

            self.assertEqual(result, 2)
            self.assertIn("result: SCAN FAILED", output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
