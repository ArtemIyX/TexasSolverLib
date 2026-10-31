#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "multiway_workflow_config_detail.hpp"

#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace texas::solver::multiway
{
	namespace
	{
		std::string trim(std::string value)
		{
			const auto first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};
			const auto last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1U);
		}
		std::uint64_t number(const std::string& value, const char* key)
		{
			if (value.empty() || value[0] == '-')
				throw std::invalid_argument(std::string("invalid value for ") + key);
			std::size_t consumed = 0U;
			std::uint64_t result = 0U;
			try
			{
				result = std::stoull(value, &consumed, 10);
			}
			catch (const std::out_of_range&)
			{
				throw std::invalid_argument(std::string("value overflows ") + key);
			}
			if (consumed != value.size())
				throw std::invalid_argument(std::string("invalid value for ") + key);
			return result;
		}
		void expect(std::uint64_t actual, std::uint64_t expected, const char* key)
		{
			if (actual != expected)
				throw std::invalid_argument(std::string("invalid value for ") + key);
		}
	} // namespace

	MultiwayWorkflowConfig parse_multiway_workflow_config(const std::string& text)
	{
		MultiwayWorkflowConfig result;
		std::set<std::string> seen;
		std::istringstream input(text);
		std::string line;
		while (std::getline(input, line))
		{
			line = trim(line);
			if (line.empty() || line[0] == '#')
				continue;
			const auto separator = line.find('=');
			if (separator == std::string::npos)
				throw std::invalid_argument("configuration line requires '='");
			const auto key = trim(line.substr(0, separator));
			const auto value = trim(line.substr(separator + 1U));
			if (!seen.insert(key).second)
				throw std::invalid_argument("duplicate configuration key: " + key);
			const auto n = [&] { return number(value, key.c_str()); };
			if (key == "schema_version")
				result.schema_version = static_cast<std::uint32_t>(n());
			else if (key == "profile_kind")
			{
				if (value == "sizing")
					result.profile_kind = MultiwayWorkflowProfileKind::Sizing;
				else if (value == "acceptance")
					result.profile_kind = MultiwayWorkflowProfileKind::Acceptance;
				else
					throw std::invalid_argument("invalid profile_kind");
			}
			else if (key == "profile_id")
				result.profile_id = value;
			else if (key == "players")
				result.model.player_count = static_cast<std::uint8_t>(n());
			else if (key == "initial_stack_chips")
				result.model.initial_stack_chips = static_cast<int>(n());
			else if (key == "small_blind_chips")
				result.model.small_blind_chips = static_cast<int>(n());
			else if (key == "big_blind_chips")
				result.model.big_blind_chips = static_cast<int>(n());
			else if (key == "ante_chips")
				result.model.ante_chips = static_cast<int>(n());
			else if (key == "rake")
				expect(n(), 0U, "rake");
			else if (key == "preflop_classes")
				result.preflop_classes = static_cast<std::uint32_t>(n());
			else if (key == "flop_buckets")
				result.model.flop_bucket_count = static_cast<std::uint32_t>(n());
			else if (key == "turn_buckets")
				result.model.turn_bucket_count = static_cast<std::uint32_t>(n());
			else if (key == "river_buckets")
				result.model.river_bucket_count = static_cast<std::uint32_t>(n());
			else if (key == "storage_backend")
				result.storage_backend = value;
			else if (key == "storage_directory")
				result.storage_directory = value;
			else if (key == "public_catalog_cache_bytes")
				result.public_catalog_cache_bytes = n();
			else if (key == "row_page_cache_bytes")
				result.row_page_cache_bytes = n();
			else if (key == "maximum_wal_bytes")
				result.maximum_wal_bytes = n();
			else if (key == "segment_target_bytes")
				result.segment_target_bytes = n();
			else if (key == "compaction_fan_in")
			{
				const auto fan_in = n();
				if (fan_in > std::numeric_limits<std::uint32_t>::max())
					throw std::invalid_argument("compaction_fan_in exceeds uint32 range");
				result.compaction_fan_in = static_cast<std::uint32_t>(fan_in);
			}
			else if (key == "compaction_scratch_bytes")
				result.compaction_scratch_bytes = n();
			else if (key == "disk_reserve_bytes")
				result.disk_reserve_bytes = n();
			else if (key == "synchronous_durability")
			{
				if (value == "true")
					result.synchronous_durability = true;
				else if (value == "false")
					result.synchronous_durability = false;
				else
					throw std::invalid_argument("invalid synchronous_durability");
			}
			else if (key == "max_decision_depth")
				result.max_decision_depth = static_cast<std::uint32_t>(n());
			else if (key == "max_public_chance_depth")
				result.max_public_chance_depth = static_cast<std::uint32_t>(n());
			else if (key == "deterministic_seed")
				result.deterministic_seed = n();
			else if (key == "reference_worker_count" || key == "training_worker_count" || key == "threads")
			{
				const auto workers = n();
				if (workers > 16U)
					throw std::invalid_argument("training worker count exceeds 16");
				result.training_worker_count = static_cast<std::uint32_t>(workers);
			}
			else if (key == "scheduler_mode")
			{
				if (value == "fixed")
					result.scheduler_mode = MultiwaySchedulerMode::Fixed;
				else if (value == "static-block")
					result.scheduler_mode = MultiwaySchedulerMode::StaticBlock;
				else if (value == "dynamic")
					result.scheduler_mode = MultiwaySchedulerMode::Dynamic;
				else
					throw std::invalid_argument("invalid scheduler_mode");
			}
			else if (key == "dynamic_chunk_size")
			{
				const auto chunk_size = n();
				if (chunk_size > std::numeric_limits<std::uint32_t>::max())
					throw std::invalid_argument("dynamic_chunk_size exceeds uint32 range");
				result.dynamic_chunk_size = static_cast<std::uint32_t>(chunk_size);
			}
			else if (key == "placement_policy")
				result.placement_policy = parse_multiway_worker_placement_policy(value.c_str());
			else if (key == "target_trajectories")
				result.target_trajectories = n();
			else if (key == "maximum_public_states" || key == "maximum_sparse_rows" || key == "maximum_sparse_values" || key == "worker_delta_capacity" || key == "discovery_capacity" || key == "max_regret_delta_entries_per_worker" || key == "max_continuation_delta_entries_per_worker" || key == "max_discovery_public_states_per_worker" || key == "max_discovery_rows_per_worker" || key == "trajectories_per_batch" || key == "checkpoint_interval" || key == "disk_space_requirement_bytes" || key == "process_memory_limit_bytes" || key == "process_working_set_limit_bytes" || key == "process_working_set_resident_allowance_bytes")
			{
				const auto capacity = value == "UNRESOLVED" ? 0U : number(value, key.c_str());
				if (key == "maximum_public_states")
					result.maximum_public_states = capacity;
				else if (key == "maximum_sparse_rows")
					result.maximum_sparse_rows = capacity;
				else if (key == "maximum_sparse_values")
					result.maximum_sparse_values = capacity;
				else if (key == "worker_delta_capacity")
					result.worker_delta_capacity = capacity;
				else if (key == "discovery_capacity")
					result.discovery_capacity = capacity;
				else if (key == "max_regret_delta_entries_per_worker")
				{
					result.max_regret_delta_entries_per_worker = capacity;
					result.explicit_stream_capacity_mask |= workflow_config_detail::kRegretCapacity;
				}
				else if (key == "max_continuation_delta_entries_per_worker")
				{
					result.max_continuation_delta_entries_per_worker = capacity;
					result.explicit_stream_capacity_mask |= workflow_config_detail::kContinuationCapacity;
				}
				else if (key == "max_discovery_public_states_per_worker")
				{
					result.max_discovery_public_states_per_worker = capacity;
					result.explicit_stream_capacity_mask |= workflow_config_detail::kDiscoveryPublicCapacity;
				}
				else if (key == "max_discovery_rows_per_worker")
				{
					result.max_discovery_rows_per_worker = capacity;
					result.explicit_stream_capacity_mask |= workflow_config_detail::kDiscoveryRowsCapacity;
				}
				else if (key == "trajectories_per_batch")
					result.trajectories_per_batch = capacity;
				else if (key == "checkpoint_interval")
					result.checkpoint_interval = capacity;
				else if (key == "disk_space_requirement_bytes")
					result.disk_space_requirement_bytes = capacity;
				else if (key == "process_working_set_limit_bytes")
					result.process_working_set_limit_bytes = capacity;
				else if (key == "process_working_set_resident_allowance_bytes")
					result.process_working_set_resident_allowance_bytes = capacity;
				else
					result.process_memory_limit_bytes = capacity;
			}
			else if (key == "input_path")
				result.training_input_path = value;
			else if (key == "output_path")
				result.training_output_path = value;
			else if (key == "report_path")
				result.training_report_path = value;
			else if (key == "checkpoint_dir")
				result.training_checkpoint_dir = value;
			else if (key == "batches")
				result.training_batches = n();
			else if (key == "log_path")
				result.training_log_path = value;
			else
				throw std::invalid_argument("unknown configuration key: " + key);
		}
		if (result.schema_version == 1U)
		{
			if (seen.count("reference_worker_count") == 0U || seen.count("training_worker_count") != 0U)
			{
				throw std::invalid_argument("schema 1 requires reference_worker_count");
			}
		}
		else if (result.schema_version == 2U)
		{
			if (seen.count("training_worker_count") == 0U || seen.count("reference_worker_count") != 0U)
			{
				throw std::invalid_argument("schema 2 requires training_worker_count");
			}
		}
		result.validate();
		return result;
	}

	std::string serialize_multiway_workflow_config(const MultiwayWorkflowConfig& config)
	{
		config.validate();
		std::ostringstream output;
		const auto line = [&output](const char* key, const auto& value) { output << key << '=' << value << '\n'; };
		line("schema_version", config.schema_version);
		line("profile_kind", config.profile_kind == MultiwayWorkflowProfileKind::Sizing ? "sizing" : "acceptance");
		line("profile_id", config.profile_id);
		line("players", static_cast<unsigned>(config.model.player_count));
		line("initial_stack_chips", config.model.initial_stack_chips);
		line("small_blind_chips", config.model.small_blind_chips);
		line("big_blind_chips", config.model.big_blind_chips);
		line("ante_chips", config.model.ante_chips);
		line("rake", 0U);
		line("preflop_classes", config.preflop_classes);
		line("flop_buckets", config.model.flop_bucket_count);
		line("turn_buckets", config.model.turn_bucket_count);
		line("river_buckets", config.model.river_bucket_count);
		line("storage_backend", config.storage_backend);
		if (config.storage_backend == "ExternalCompactInt32")
		{
			line("storage_directory", config.storage_directory.generic_string());
			line("public_catalog_cache_bytes", config.public_catalog_cache_bytes);
			line("row_page_cache_bytes", config.row_page_cache_bytes);
			line("maximum_wal_bytes", config.maximum_wal_bytes);
			line("segment_target_bytes", config.segment_target_bytes);
			line("compaction_fan_in", config.compaction_fan_in);
			line("compaction_scratch_bytes", config.compaction_scratch_bytes);
			line("disk_reserve_bytes", config.disk_reserve_bytes);
			line("synchronous_durability", config.synchronous_durability ? "true" : "false");
		}
		line("max_decision_depth", config.max_decision_depth);
		line("max_public_chance_depth", config.max_public_chance_depth);
		line("deterministic_seed", config.deterministic_seed);
		if (config.schema_version == 1U)
			line("reference_worker_count", config.training_worker_count);
		else
			line("training_worker_count", config.training_worker_count);
		line("scheduler_mode", multiway_scheduler_mode_name(config.scheduler_mode));
		line("dynamic_chunk_size", config.dynamic_chunk_size);
		line("placement_policy", multiway_worker_placement_policy_name(config.placement_policy));
		line("target_trajectories", config.target_trajectories);
		const auto unresolved = [](std::uint64_t value) { return value == 0U ? std::string("UNRESOLVED") : std::to_string(value); };
		line("maximum_public_states", unresolved(config.maximum_public_states));
		line("maximum_sparse_rows", unresolved(config.maximum_sparse_rows));
		line("maximum_sparse_values", unresolved(config.maximum_sparse_values));
		const bool explicit_capacities = config.explicit_stream_capacity_mask != 0U || config.max_regret_delta_entries_per_worker != 0U || config.max_continuation_delta_entries_per_worker != 0U || config.max_discovery_public_states_per_worker != 0U || config.max_discovery_rows_per_worker != 0U;
		if (explicit_capacities)
		{
			line("max_regret_delta_entries_per_worker", config.resolved_regret_delta_capacity());
			line("max_continuation_delta_entries_per_worker", config.resolved_continuation_delta_capacity());
			line("max_discovery_public_states_per_worker", config.resolved_discovery_public_state_capacity());
			line("max_discovery_rows_per_worker", config.resolved_discovery_row_capacity());
		}
		else
		{
			line("worker_delta_capacity", unresolved(config.worker_delta_capacity));
			line("discovery_capacity", unresolved(config.discovery_capacity));
		}
		line("trajectories_per_batch", unresolved(config.trajectories_per_batch));
		line("checkpoint_interval", unresolved(config.checkpoint_interval));
		line("disk_space_requirement_bytes", unresolved(config.disk_space_requirement_bytes));
		line("process_memory_limit_bytes", unresolved(config.process_memory_limit_bytes));
		line("process_working_set_limit_bytes", config.process_working_set_limit_bytes);
		line("process_working_set_resident_allowance_bytes", config.process_working_set_resident_allowance_bytes);
		if (!config.training_input_path.empty())
			line("input_path", config.training_input_path.string());
		if (!config.training_output_path.empty())
			line("output_path", config.training_output_path.string());
		if (!config.training_report_path.empty())
			line("report_path", config.training_report_path.string());
		if (!config.training_checkpoint_dir.empty())
			line("checkpoint_dir", config.training_checkpoint_dir.string());
		if (config.training_batches != 0U)
			line("batches", config.training_batches);
		if (!config.training_log_path.empty())
			line("log_path", config.training_log_path.string());
		return output.str();
	}

	MultiwayWorkflowConfig load_multiway_workflow_config(const std::filesystem::path& path)
	{
		std::ifstream input(path);
		if (!input)
			throw std::runtime_error("cannot open workflow configuration");
		return parse_multiway_workflow_config({ std::istreambuf_iterator<char>(input), {} });
	}
} // namespace texas::solver::multiway
