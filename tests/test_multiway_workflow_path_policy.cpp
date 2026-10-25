#include "solver/multiway/workflow/multiway_workflow_path_policy.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
	struct TemporaryWorkflowRoot
	{
		std::filesystem::path path;

		TemporaryWorkflowRoot()
		{
			path = std::filesystem::temp_directory_path() /
				("texas_solver_path_policy_" +
					std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(path);
		}

		~TemporaryWorkflowRoot()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	};

	texas::solver::multiway::MultiwayWorkflowPathPolicy policy_for(
		const std::filesystem::path& root)
	{
		texas::solver::multiway::MultiwayWorkflowPathPolicy policy;
		policy.root = std::filesystem::weakly_canonical(root);
		return policy;
	}
}

TEST_CASE(multiway_workflow_path_policy_authorizes_in_root_paths)
{
	using namespace texas::solver::multiway;
	TemporaryWorkflowRoot temporary;
	std::ofstream(temporary.path / "input.bin") << "input";
	const auto policy = policy_for(temporary.path);

	const auto input = authorize_multiway_workflow_path(
		policy, "input.bin", MultiwayWorkflowPathKind::InputFile);
	const auto output = authorize_multiway_workflow_path(
		policy, "nested/output.bin", MultiwayWorkflowPathKind::OutputFile);
	const auto output_directory = authorize_multiway_workflow_path(
		policy, "nested", MultiwayWorkflowPathKind::OutputDirectory);

	EXPECT_EQ(input, std::filesystem::weakly_canonical(temporary.path / "input.bin"));
	EXPECT_EQ(output, std::filesystem::weakly_canonical(temporary.path / "nested/output.bin"));
	EXPECT_EQ(output_directory, std::filesystem::weakly_canonical(temporary.path / "nested"));
}

TEST_CASE(multiway_workflow_path_policy_rejects_traversal_and_outside_absolute_paths)
{
	using namespace texas::solver::multiway;
	TemporaryWorkflowRoot temporary;
	const auto outside = temporary.path.parent_path() / "texas_solver_path_policy_outside.bin";
	std::ofstream(outside) << "outside";
	const auto policy = policy_for(temporary.path);

	EXPECT_THROW(authorize_multiway_workflow_path(
		policy, "../texas_solver_path_policy_outside.bin", MultiwayWorkflowPathKind::InputFile),
		std::invalid_argument);
	EXPECT_THROW(authorize_multiway_workflow_path(
		policy, outside, MultiwayWorkflowPathKind::InputFile), std::invalid_argument);
	EXPECT_THROW(authorize_multiway_workflow_path(
		policy, "invalid:stream", MultiwayWorkflowPathKind::OutputFile), std::invalid_argument);

	std::error_code error;
	std::filesystem::remove(outside, error);
}

TEST_CASE(multiway_workflow_path_policy_rejects_symlink_escape)
{
	using namespace texas::solver::multiway;
	TemporaryWorkflowRoot temporary;
	const auto outside = temporary.path.parent_path() / "texas_solver_path_policy_symlink_target";
	const auto target_file = outside / "input.bin";
	std::filesystem::create_directories(outside);
	std::ofstream(target_file) << "outside";
	const auto link = temporary.path / "linked";
	std::error_code error;
	std::filesystem::create_directory_symlink(outside, link, error);
	if (!error)
	{
		const auto policy = policy_for(temporary.path);
		EXPECT_THROW(authorize_multiway_workflow_path(
			policy, link / "input.bin", MultiwayWorkflowPathKind::InputFile),
			std::invalid_argument);
	}
	std::filesystem::remove_all(outside, error);
}

TEST_CASE(multiway_workflow_path_policy_enforces_overwrite_and_alias_boundaries)
{
	using namespace texas::solver::multiway;
	TemporaryWorkflowRoot temporary;
	const auto existing = temporary.path / "existing.bin";
	std::ofstream(existing) << "existing";
	const auto policy = policy_for(temporary.path);

	EXPECT_THROW(authorize_multiway_workflow_path(
		policy, existing, MultiwayWorkflowPathKind::OutputFile), std::invalid_argument);
	auto overwrite_policy = policy;
	overwrite_policy.allow_output_overwrite = true;
	EXPECT_EQ(authorize_multiway_workflow_path(
		overwrite_policy, existing, MultiwayWorkflowPathKind::OutputFile),
		std::filesystem::weakly_canonical(existing));

	const auto append_target = authorize_multiway_workflow_path(
		policy, existing, MultiwayWorkflowPathKind::AppendFile);
	EXPECT_EQ(append_target, std::filesystem::weakly_canonical(existing));
	EXPECT_THROW(authorize_multiway_workflow_path(
		policy, existing, MultiwayWorkflowPathKind::OutputDirectory), std::invalid_argument);
}
