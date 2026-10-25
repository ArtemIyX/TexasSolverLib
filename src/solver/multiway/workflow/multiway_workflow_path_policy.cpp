#include "solver/multiway/workflow/multiway_workflow_path_policy.hpp"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace texas::solver::multiway
{
	namespace
	{
		[[nodiscard]] bool is_dot_dot(const std::filesystem::path& component)
		{
			return component == std::filesystem::path("..");
		}

		[[nodiscard]] bool has_dot_dot_component(const std::filesystem::path& path)
		{
			for (const auto& component : path)
				if (is_dot_dot(component))
					return true;
			return false;
		}

		[[nodiscard]] bool has_device_or_stream_syntax(const std::filesystem::path& path)
		{
			const auto text = path.generic_string();
			if (text.rfind("//?", 0U) == 0U || text.rfind("//.", 0U) == 0U || text.rfind("\\\\?\\", 0U) == 0U)
				return true;

			const auto root_name = path.root_name().generic_string();
			if (!root_name.empty() && path.root_directory().empty())
				return true; // Drive-relative paths such as C:foo are not rooted.

			const auto root_length = root_name.size();
			for (std::size_t index = root_length; index < text.size(); ++index)
				if (text[index] == ':')
					return true; // Alternate data streams and malformed device paths.
			return false;
		}

		[[nodiscard]] bool is_reparse_point(const std::filesystem::path& path)
		{
#ifdef _WIN32
			const auto attributes = GetFileAttributesW(path.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES)
			{
				const auto error = GetLastError();
				if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
					return false;
				throw std::runtime_error("cannot inspect workflow path attributes");
			}
			return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
			(void)path;
			return false;
#endif
		}

		[[nodiscard]] bool is_within(const std::filesystem::path& root,
			const std::filesystem::path& candidate)
		{
			const auto relative = candidate.lexically_relative(root);
			if (relative.empty())
				return true;
			if (relative.is_absolute())
				return false;
			for (const auto& component : relative)
				if (is_dot_dot(component))
					return false;
			return true;
		}

		void validate_existing_components(const std::filesystem::path& root,
			const std::filesystem::path& candidate)
		{
			const auto relative = candidate.lexically_normal().lexically_relative(root);
			if (!is_within(root, candidate))
				throw std::invalid_argument("workflow path escapes the authorized root");

			auto current = root;
			for (const auto& component : relative)
			{
				if (component.empty() || component == std::filesystem::path("."))
					continue;
				current /= component;
				std::error_code error;
				const auto status = std::filesystem::symlink_status(current, error);
				if (error && error != std::errc::no_such_file_or_directory)
					throw std::runtime_error("cannot inspect workflow path component");
				if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
					break;
				if (status.type() == std::filesystem::file_type::symlink || is_reparse_point(current))
					throw std::invalid_argument("workflow path contains a symlink or reparse point");
			}
		}

		[[nodiscard]] std::filesystem::path canonical_root(const MultiwayWorkflowPathPolicy& policy)
		{
			policy.validate();
			std::error_code error;
			const auto root = std::filesystem::weakly_canonical(policy.root, error);
			if (error || root.empty() || !std::filesystem::is_directory(root, error) || error)
				throw std::invalid_argument("workflow path policy root is unavailable");
			validate_existing_components(root, root);
			return root;
		}

		[[nodiscard]] const char* kind_name(MultiwayWorkflowPathKind kind)
		{
			switch (kind)
			{
			case MultiwayWorkflowPathKind::InputFile:
				return "input file";
			case MultiwayWorkflowPathKind::InputDirectory:
				return "input directory";
			case MultiwayWorkflowPathKind::OutputFile:
				return "output file";
			case MultiwayWorkflowPathKind::OutputDirectory:
				return "output directory";
			case MultiwayWorkflowPathKind::AppendFile:
				return "append file";
			}
			return "workflow path";
		}

		[[nodiscard]] bool status_exists(const std::filesystem::file_status& status)
		{
			return status.type() != std::filesystem::file_type::not_found &&
				status.type() != std::filesystem::file_type::none;
		}
	}

	void MultiwayWorkflowPathPolicy::validate() const
	{
		if (root.empty() || !root.is_absolute() || has_dot_dot_component(root) || has_device_or_stream_syntax(root))
			throw std::invalid_argument("workflow path policy root must be an absolute safe directory");
		std::error_code error;
		const auto status = std::filesystem::symlink_status(root, error);
		if (error || status.type() == std::filesystem::file_type::symlink || !std::filesystem::is_directory(status))
			throw std::invalid_argument("workflow path policy root must be a real directory");
		if (is_reparse_point(root))
			throw std::invalid_argument("workflow path policy root cannot be a reparse point");
	}

	MultiwayWorkflowPathPolicy make_default_multiway_workflow_path_policy()
	{
		std::error_code error;
		MultiwayWorkflowPathPolicy policy;
		policy.root = std::filesystem::weakly_canonical(std::filesystem::current_path(error), error);
		if (error)
			throw std::runtime_error("cannot resolve workflow path policy root");
		policy.validate();
		return policy;
	}

	std::filesystem::path authorize_multiway_workflow_path(
		const MultiwayWorkflowPathPolicy& policy,
		const std::filesystem::path& raw_path,
		MultiwayWorkflowPathKind kind)
	{
		if (raw_path.empty())
			throw std::invalid_argument(std::string("workflow ") + kind_name(kind) + " is empty");
		if (has_dot_dot_component(raw_path) || has_device_or_stream_syntax(raw_path))
			throw std::invalid_argument(std::string("workflow ") + kind_name(kind) + " contains unsafe path syntax");

		const auto root = canonical_root(policy);
		const auto candidate = raw_path.is_absolute() ? raw_path : root / raw_path;
		std::error_code error;
		const auto resolved = std::filesystem::weakly_canonical(candidate, error);
		if (error || resolved.empty() || !is_within(root, resolved))
			throw std::invalid_argument(std::string("workflow ") + kind_name(kind) + " escapes the authorized root");
		validate_existing_components(root, candidate);

		const auto status = std::filesystem::symlink_status(resolved, error);
		if (error && error != std::errc::no_such_file_or_directory)
			throw std::runtime_error(std::string("cannot inspect workflow ") + kind_name(kind));
		const bool present = !error && status_exists(status);
		if (present && (status.type() == std::filesystem::file_type::symlink || is_reparse_point(resolved)))
			throw std::invalid_argument(std::string("workflow ") + kind_name(kind) + " cannot be a symlink or reparse point");

		switch (kind)
		{
		case MultiwayWorkflowPathKind::InputFile:
			if (!present || !std::filesystem::is_regular_file(status))
				throw std::invalid_argument("workflow input file is not a regular file");
			break;
		case MultiwayWorkflowPathKind::InputDirectory:
			if (!present || !std::filesystem::is_directory(status))
				throw std::invalid_argument("workflow input directory is unavailable");
			break;
		case MultiwayWorkflowPathKind::OutputFile:
			if (present && (!std::filesystem::is_regular_file(status) || !policy.allow_output_overwrite))
				throw std::invalid_argument("workflow output file already exists or is not regular");
			break;
		case MultiwayWorkflowPathKind::OutputDirectory:
			if (present && !std::filesystem::is_directory(status))
				throw std::invalid_argument("workflow output directory is not a directory");
			break;
		case MultiwayWorkflowPathKind::AppendFile:
			if (present && !std::filesystem::is_regular_file(status))
				throw std::invalid_argument("workflow append target is not a regular file");
			break;
		}
		return resolved;
	}

} // namespace texas::solver::multiway
