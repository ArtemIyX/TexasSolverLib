#include "solver/multiway/workflow/multiway_diagnostic_journal.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{
	class TestDirectory
	{
	public:
		TestDirectory()
		{
			static std::atomic<unsigned> sequence{ 0U };
			for (;;)
			{
				path = std::filesystem::temp_directory_path() / ("texassolver_diagnostic_journal_" + std::to_string(sequence.fetch_add(1U)));
				std::error_code error;
				if (std::filesystem::create_directory(path, error))
					break;
				if (error)
					throw std::runtime_error("cannot create diagnostic test directory");
			}
		}
		~TestDirectory()
		{
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}
		std::filesystem::path path;
	};
} // namespace

TEST_CASE(multiway_diagnostic_journal_appends_complete_records_and_preserves_existing_file)
{
	using texas::solver::multiway::MultiwayDiagnosticJournal;
	TestDirectory directory;
	const auto path = directory.path / "events.jsonl";
	MultiwayDiagnosticJournal journal(path);
	journal.append("{\"batch\":1}");
	journal.append("{\"batch\":2}");
	EXPECT_THROW(MultiwayDiagnosticJournal(path), std::runtime_error);
	EXPECT_THROW(journal.append("invalid\nrecord"), std::invalid_argument);
	EXPECT_THROW(journal.append(std::string(65537U, 'x')), std::invalid_argument);
	std::ifstream input(path, std::ios::binary);
	std::string first, second;
	std::getline(input, first);
	std::getline(input, second);
	EXPECT_EQ(first, "{\"batch\":1}");
	EXPECT_EQ(second, "{\"batch\":2}");
	EXPECT_TRUE(!std::getline(input, first));
}
