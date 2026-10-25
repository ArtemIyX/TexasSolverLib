import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).with_name("extract_symbols.py")
SPEC = importlib.util.spec_from_file_location("extract_symbols", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
extract_symbols = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extract_symbols)


class ExtractSymbolsTests(unittest.TestCase):
    def test_template_and_inheritance_declarations(self) -> None:
        match = extract_symbols.parse_symbol_line(
            "template <typename T, typename U = std::pair<T, T>> class Sample : public Base<T>"
        )
        self.assertIsNotNone(match)
        assert match is not None
        self.assertEqual(match.group(1), "class")
        self.assertEqual(match.group(2), "Sample")
        self.assertEqual(match.group(3), "Base<T>")

    def test_malformed_template_is_rejected(self) -> None:
        self.assertIsNone(extract_symbols.parse_symbol_line("template <typename T class Broken"))
        self.assertIsNone(extract_symbols.parse_symbol_line("template class MissingParameters"))

    def test_pathological_input_is_bounded(self) -> None:
        pathological = "template <" + ("<" * 100_000) + ("x" * 100_000)
        self.assertIsNone(extract_symbols.parse_symbol_line(pathological))
        self.assertIsNone(
            extract_symbols.parse_symbol_line("class " + ("x" * (extract_symbols.MAX_SCAN_LINE_LENGTH + 1)))
        )


if __name__ == "__main__":
    unittest.main()
