#pragma once

/** @file
 * Root-constrained authorization for workflow input and output paths.
 */

#include <filesystem>

namespace texas::solver::multiway
{

	/** Access mode applied while validating a workflow path. */
	enum class MultiwayWorkflowPathKind
	{
		InputFile,
		InputDirectory,
		OutputFile,
		OutputDirectory,
		AppendFile
	};

	/** Root directory and overwrite policy used to authorize workflow paths. */
#pragma pack(push, 8)
	struct MultiwayWorkflowPathPolicy
	{
		std::filesystem::path root;
		bool allow_output_overwrite = false;

		/** Checks root configuration and path authorization settings. */
		void validate() const;
	};
#pragma pack(pop)

	/** Creates the repository default workflow path boundary. */
	[[nodiscard]] MultiwayWorkflowPathPolicy make_default_multiway_workflow_path_policy();

	// Returns an absolute, weakly canonical path after checking the path and its
	// existing components against the policy.
	/** Resolves and authorizes a raw path under policy for its access kind. */
	[[nodiscard]] std::filesystem::path authorize_multiway_workflow_path(
		const MultiwayWorkflowPathPolicy& policy,
		const std::filesystem::path& raw_path,
		MultiwayWorkflowPathKind kind);

} // namespace texas::solver::multiway
