#pragma once

#include <filesystem>

namespace texas::solver::multiway
{

	enum class MultiwayWorkflowPathKind
	{
		InputFile,
		InputDirectory,
		OutputFile,
		OutputDirectory,
		AppendFile
	};

	struct MultiwayWorkflowPathPolicy
	{
		std::filesystem::path root;
		bool allow_output_overwrite = false;

		void validate() const;
	};

	[[nodiscard]] MultiwayWorkflowPathPolicy make_default_multiway_workflow_path_policy();

	// Returns an absolute, weakly canonical path after checking the path and its
	// existing components against the policy.
	[[nodiscard]] std::filesystem::path authorize_multiway_workflow_path(
		const MultiwayWorkflowPathPolicy& policy,
		const std::filesystem::path& raw_path,
		MultiwayWorkflowPathKind kind);

} // namespace texas::solver::multiway
