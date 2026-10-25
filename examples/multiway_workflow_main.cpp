#include <cstdlib>
#include <exception>
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"
#include "solver/multiway/abstraction/multiway_bucket_generation.hpp"
#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_staged_qualification.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"
#include "solver/multiway/workflow/multiway_artifact_preflight.hpp"
#include "solver/multiway/workflow/multiway_f1_acceptance.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "core/canonical_combo.hpp"
#include "util/tracy.hpp"

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>
#include <memory>
#include "core/atomic_publish.hpp"

namespace
{

	enum class Workflow
	{
		Train,
		Buckets,
		Inspect,
		Evaluate,
		Finalize
	};

	texas::solver::multiway::MultiwayEvidenceHeader make_evidence_header(
		std::uint32_t schema_version,
		texas::solver::multiway::MultiwayModelIdentity model_identity,
		std::uint64_t workflow_fingerprint)
	{
		using namespace texas::solver::multiway;
		const auto environment = [](const char* name) {
			const auto* value = std::getenv(name);
			return value == nullptr || *value == '\0' ? std::string("unknown") : std::string(value);
		};
		MultiwayEvidenceHeader header;
		header.schema_version = schema_version;
		header.producer.git_commit = environment("TEXASSOLVER_GIT_COMMIT");
		header.producer.build_configuration = environment("TEXASSOLVER_BUILD_CONFIGURATION");
		header.producer.compiler_identity = environment("TEXASSOLVER_COMPILER_IDENTITY");
		header.producer.model_identity = model_identity;
		header.producer.workflow_config_fingerprint = workflow_fingerprint;
		header.producer.artifact_schemas = { 4U, 2U, 1U };
		return header;
	}

	Workflow workflow_from_name(std::string_view name)
	{
		if (name == "train")
			return Workflow::Train;
		if (name == "buckets")
			return Workflow::Buckets;
		if (name == "inspect")
			return Workflow::Inspect;
		if (name == "finalize")
			return Workflow::Finalize;
		return Workflow::Evaluate;
	}

	void print_help(std::string_view name, Workflow workflow)
	{
		std::cout << "TexasSolver multiway " << name << " workflow\n"
				  << "Usage: " << name << " [options]\n\n"
				  << "Options:\n"
				  << "  --help                 Show this help\n"
				  << "  --config <path>       Versioned workflow configuration\n"
				  << "  --seed <integer>       Deterministic seed\n";
		std::cout << "  --tiny                 Run the bounded disk qualification pipeline\n";
		if (workflow == Workflow::Train)
		{
			std::cout << "  --batches <integer>    Bounded training batch count\n"
					  << "  --input <path>         Verified bucket artifact\n"
					  << "  --output <path>        Full blueprint artifact\n"
					  << "  --report <path>        JSON training report\n"
					  << "  --checkpoint-dir <dir> Output directory for incomplete runs\n"
					  << "  --resume <path>        Checkpoint to resume\n"
					  << "  --progress <true|false>  Print training progress (default true)\n"
					  << "  --progress-every <N>  Print training progress every N batches\n"
					  << "  --progress-memory <true|false>  Show working-set telemetry\n"
					  << "  --progress-speed <true|false>  Show interval and overall trajectories per second\n";
			std::cout << "  --threads <1..16>      Training worker override\n"
					  << "  --pin-workers <true|false>  Pin training workers to logical processors\n"
					  << "  --scheduler <fixed|dynamic>  Training worker scheduler\n"
					  << "  --authorize-50m       Explicitly authorize the 50M qualification stage\n";
		}
		else if (workflow == Workflow::Buckets)
		{
			std::cout << "  --output <path>        Atomically published bucket artifact\n"
					  << "  --checkpoint-dir <dir> Resume/checkpoint directory\n"
					  << "  --threads <integer>   Bucket worker count (default physical-core cap)\n";
			std::cout << "  --benchmark-start <integer>   Global catalog start index\n"
					  << "  --benchmark-tables <integer>  Run bounded benchmark\n"
					  << "  --benchmark-mode <mode>       build-only or end-to-end\n";
		}
		else if (workflow == Workflow::Inspect)
		{
			std::cout << "  --input <path>         Artifact to inspect\n"
					  << "  --report <path>        Atomically published JSON inspection report\n"
					  << "  --progress <true|false>  Print inspection progress (default true)\n"
					  << "  --progress-every <N>  Print progress every N tables\n"
					  << "  --hash <parallel|legacy|both>  Inspection fingerprint mode\n";
		}
		else if (workflow == Workflow::Finalize)
		{
			std::cout << "  --bucket-report <path>       Bucket inspection report\n"
					  << "  --training-report <path>     Training report\n"
					  << "  --equivalence-report <path> Checkpoint equivalence report\n"
					  << "  --lookup-first <path>        First lookup report\n"
					  << "  --lookup-second <path>       Second lookup report\n"
					  << "  --operator <id>              Human operator identifier\n"
					  << "  --start-utc <timestamp>      Human-run start timestamp\n"
					  << "  --end-utc <timestamp>        Human-run end timestamp\n"
					  << "  --command <text>             Exact human command\n"
					  << "  --machine-report <path>      Machine provenance report\n"
					  << "  --exit-code <integer>        Human command exit code\n"
					  << "  --output <path>              final_acceptance.json\n";
		}
		else
		{
			std::cout << "  --artifacts <dir>      Verified artifact directory\n"
					  << "  --duplicates <integer> Duplicate deal count\n";
		}
	}

	int run_tiny_pipeline()
	{
		using namespace texas;
		using namespace texas::solver::multiway;
		const auto directory = std::filesystem::temp_directory_path() / "texas_solver_f2_tiny";
		std::filesystem::create_directories(directory);
		const auto identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
		MultiwayFutureBucketProfile profile;
		profile.flop_bucket_count = 2U;
		profile.turn_bucket_count = 2U;
		profile.river_bucket_count = 2U;
		const std::vector<MultiwayBucketBoardRequest> boards = {
			{ Street::Flop, { 0U, 5U, 10U } }, { Street::Turn, { 0U, 5U, 10U, 15U } }
		};
		const auto bucket = build_multiway_future_bucket_artifact(identity, boards, profile);
		const auto bucket_path = directory / "future-buckets.bin";
		save_multiway_future_bucket_artifact_atomic(bucket_path, bucket);
		const auto loaded_bucket = load_multiway_future_bucket_artifact(bucket_path, identity);
		if (loaded_bucket.registry().identity() != identity)
			throw std::runtime_error("bucket identity mismatch");

		MultiwayBlueprintSnapshot snapshot;
		snapshot.identity = identity;
		snapshot.public_state = { 71U };
		snapshot.infoset = { { 71U }, 0 };
		snapshot.trajectories = 1U;
		snapshot.training.trajectories = 1U;
		snapshot.training.deterministic_seed = 7U;
		snapshot.actions = { { { MultiwayAction::Check, 0U, 0, 17U }, 65535U } };
		snapshot.validate();
		const auto root_path = directory / "root.bin";
		MultiwayBlueprintArtifacts::save_atomic(root_path, snapshot);
		const auto loaded_root = MultiwayBlueprintArtifacts::load_verified(root_path, identity);
		if (MultiwayBlueprintArtifacts::snapshot_hash(loaded_root.snapshot) != MultiwayBlueprintArtifacts::snapshot_hash(snapshot))
		{
			throw std::runtime_error("root artifact hash mismatch");
		}

		MultiwayFullBlueprintArtifact full;
		full.identity = identity;
		full.training = snapshot.training;
		full.rows = { { { { 71U }, 0 }, 0U, 17U, { { { MultiwayAction::Check, 0U, 0, 17U }, 65535U } } } };
		const auto full_path = directory / "full-blueprint.bin";
		MultiwayFullBlueprintArtifacts::save_atomic(full_path, full);
		const auto loaded_full = MultiwayFullBlueprintArtifacts::load_verified(full_path, identity);
		if (loaded_full.rows.size() != 1U)
			throw std::runtime_error("full blueprint row count mismatch");
		std::cout << "tiny F2 pipeline passed: buckets=1 root_rows=1 full_rows="
				  << loaded_full.rows.size() << "\n";
		return EXIT_SUCCESS;
	}

	void save_bucket_manifest_atomic(const std::filesystem::path& path,
		const texas::solver::multiway::MultiwayWorkflowConfig& workflow,
		const texas::solver::multiway::MultiwayBucketArtifactInspection& report,
		std::uintmax_t byte_length)
	{
		const auto temporary = path.string() + ".tmp";
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot open bucket manifest");
		output << "{\n  \"config_fingerprint\": " << workflow.fingerprint()
			   << ",\n  \"identity\": " << report.identity.combined_hash
			   << ",\n  \"byte_length\": " << byte_length
			   << ",\n  \"flop_tables\": " << report.flop_tables
			   << ",\n  \"turn_tables\": " << report.turn_tables
			   << ",\n  \"river_tables\": " << report.river_tables
			   << ",\n  \"live_assignments\": " << report.live_assignments
			   << ",\n  \"payload_hash\": " << report.payload_hash << "\n}\n";
		output.close();
		texas::core::publish_atomic_replace(temporary, path, "cannot publish bucket manifest");
	}

	std::uint64_t estimate_full_blueprint_bytes(
		const texas::solver::multiway::MultiwayWorkflowConfig& workflow)
	{
		using namespace texas::solver::multiway;
		constexpr std::uint64_t max_actions_per_row = 64U;
		constexpr std::uint64_t fixed_bytes = 256U;
		const auto row_bytes = static_cast<std::uint64_t>(sizeof(MultiwayBlueprintRow));
		const auto action_bytes = static_cast<std::uint64_t>(sizeof(MultiwayQuantizedRootAction));
		if (action_bytes != 0U && max_actions_per_row > (std::numeric_limits<std::uint64_t>::max() - row_bytes) / action_bytes)
		{
			throw std::length_error("blueprint size estimate overflows");
		}
		const auto per_row = row_bytes + max_actions_per_row * action_bytes;
		if (workflow.maximum_sparse_rows > (std::numeric_limits<std::uint64_t>::max() - fixed_bytes) / per_row)
		{
			throw std::length_error("blueprint size estimate overflows");
		}
		return fixed_bytes + workflow.maximum_sparse_rows * per_row;
	}

	int run_buckets(const std::filesystem::path& config_path, const std::filesystem::path& output_path,
		const std::filesystem::path& checkpoint_dir, std::uint32_t requested_threads)
	{
		using namespace texas::solver::multiway;
		std::cout << "texas_multiway_buckets: starting bucket generation\n"
				  << "  config=" << config_path.string() << "\n"
				  << "  output=" << output_path.string() << "\n"
				  << "  checkpoint_dir="
				  << (checkpoint_dir.empty() ? std::string("<disabled>") : checkpoint_dir.string()) << "\n"
				  << "  requested_threads=" << requested_threads << std::endl;
		const auto workflow = load_multiway_workflow_config(config_path);
		const auto identity = make_multiway_model_identity(workflow.model);
		MultiwayBucketBaselineProfile profile = MultiwayBucketBaselineProfile::standard();
		profile.flop_bucket_count = workflow.model.flop_bucket_count;
		profile.turn_bucket_count = workflow.model.turn_bucket_count;
		profile.river_bucket_count = workflow.model.river_bucket_count;
		const auto temporary = output_path.string() + ".tmp";
		if (std::filesystem::exists(output_path))
			throw std::runtime_error("bucket artifact already exists; refusing overwrite");
		if (!output_path.parent_path().empty())
			std::filesystem::create_directories(output_path.parent_path());
		const auto expected_tables = multiway_bucket_board_count(texas::core::Street::Flop) + multiway_bucket_board_count(texas::core::Street::Turn) + multiway_bucket_board_count(texas::core::Street::River);
		const auto hardware_threads = multiway_bucket_hardware_thread_count();
		const auto effective_threads = resolve_multiway_bucket_thread_count(
			requested_threads, hardware_threads);
		std::cout << "bucket generation goal:\n"
				  << "  profile=" << workflow.profile_id
				  << " kind=" << static_cast<unsigned>(workflow.profile_kind)
				  << " schema=" << workflow.schema_version << "\n"
				  << "  model_identity=" << identity.combined_hash
				  << " config_fingerprint=" << workflow.fingerprint() << "\n"
				  << "  players=" << static_cast<unsigned>(workflow.model.player_count)
				  << " buckets=" << workflow.model.flop_bucket_count << '/'
				  << workflow.model.turn_bucket_count << '/'
				  << workflow.model.river_bucket_count
				  << " seed=" << workflow.deterministic_seed << "\n"
				  << "  tables=" << expected_tables
				  << " chunk_size=" << MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE
				  << " hardware_threads=" << hardware_threads
				  << " physical_cores=" << multiway_bucket_physical_core_count()
				  << " effective_threads=" << effective_threads << std::endl;
		if (!checkpoint_dir.empty())
			std::filesystem::create_directories(checkpoint_dir);
		constexpr std::uint64_t max_table_bytes = 2U + 5U + 4U + static_cast<std::uint64_t>(MULTIWAY_HOLE_COMBINATION_COUNT) * 4U;
		if (expected_tables > (std::numeric_limits<std::uint64_t>::max() - 116U) / max_table_bytes)
		{
			throw std::length_error("bucket artifact size estimate overflows");
		}
		const auto estimated_bytes = 116U + expected_tables * max_table_bytes;
		const auto progress_path = checkpoint_dir.empty() ? std::filesystem::path{} : checkpoint_dir / "latest.progress";
		preflight_multiway_artifact({ output_path,
			output_path.string() + ".tmp",
			progress_path,
			identity,
			expected_tables,
			estimated_bytes,
			workflow.disk_space_requirement_bytes == 0U
				? estimated_bytes
				: workflow.disk_space_requirement_bytes,
			estimate_multiway_bucket_generation_process_memory_bytes(
				requested_threads, hardware_threads, MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE),
			workflow.process_memory_limit_bytes });
		std::unique_ptr<MultiwayBucketArtifactWriter> writer;
		if (!checkpoint_dir.empty() && std::filesystem::exists(temporary) && std::filesystem::exists(progress_path))
		{
			writer = std::make_unique<MultiwayBucketArtifactWriter>(MultiwayBucketArtifactWriter::resume(
				temporary, identity, expected_tables,
				load_multiway_bucket_progress(progress_path, identity, expected_tables)));
		}
		else
		{
			if (std::filesystem::exists(temporary))
			{
				throw std::runtime_error("bucket temporary artifact has no verified resume sidecar");
			}
			writer = std::make_unique<MultiwayBucketArtifactWriter>(temporary, identity, expected_tables);
		}
		const auto already_written = writer->progress().table_count;
		const auto progress_start = std::chrono::steady_clock::now();
		const auto publish_checkpoint = [&]() {
			if (checkpoint_dir.empty())
				return;
			writer->flush_checkpoint();
			save_multiway_bucket_progress_atomic(checkpoint_dir / "latest.progress", identity,
				expected_tables, writer->progress());
			const auto completed = writer->progress().table_count;
			const auto elapsed = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - progress_start)
									 .count();
			const auto rate = elapsed > 0.0
				? static_cast<double>(completed - already_written) / elapsed
				: 0.0;
			const auto remaining = rate > 0.0
				? static_cast<double>(expected_tables - completed) / rate
				: 0.0;
			const auto percentage = expected_tables == 0U
				? 100.0
				: 100.0 * static_cast<double>(completed) / static_cast<double>(expected_tables);
			std::cout << "bucket progress: tables=" << completed << '/' << expected_tables
					  << " (" << std::fixed << std::setprecision(2) << percentage << "%)"
					  << " rate=" << rate << "/s elapsed=" << elapsed
					  << "s eta=" << remaining << "s\n";
		};
		const auto build_chunk = [identity, profile](
									 std::uint64_t begin_index,
									 std::uint64_t end_index,
									 std::vector<MultiwayBucketTable>& tables) {
			build_multiway_baseline_bucket_chunk(
				identity, profile, begin_index, end_index, tables);
		};
		const auto direct_build_chunk = [identity, profile](std::uint64_t begin_index,
											std::uint64_t end_index,
											std::vector<std::uint8_t>& payload) {
			build_multiway_baseline_direct_serialized_chunk(identity, profile, begin_index, end_index, payload);
		};
		try
		{
			generate_multiway_bucket_serialized_chunks(
				already_written, expected_tables,
				{ requested_threads, MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE, 0U, nullptr, direct_build_chunk },
				build_chunk,
				[&](std::uint64_t begin_index, std::uint64_t table_count, std::vector<std::uint8_t>& payload) {
					if (begin_index != writer->progress().table_count)
					{
						throw std::logic_error("bucket chunks were published out of order");
					}
					writer->append_serialized_chunk(table_count, payload);
					const auto completed = begin_index + table_count;
					const auto flop_end = multiway_bucket_board_count(texas::core::Street::Flop);
					const auto turn_end = flop_end + multiway_bucket_board_count(texas::core::Street::Turn);
					if (!checkpoint_dir.empty() && (completed % 4096U == 0U || completed == flop_end || completed == turn_end || completed == expected_tables))
					{
						publish_checkpoint();
					}
				});
		}
		catch (...)
		{
			try
			{
				publish_checkpoint();
			}
			catch (...)
			{
			}
			throw;
		}
		writer->finish(output_path);
		const auto inspection = inspect_multiway_bucket_artifact(output_path, identity);
		if (inspection.flop_tables != multiway_bucket_board_count(texas::core::Street::Flop) || inspection.turn_tables != multiway_bucket_board_count(texas::core::Street::Turn) || inspection.river_tables != multiway_bucket_board_count(texas::core::Street::River))
		{
			throw std::runtime_error("published bucket artifact has incomplete canonical coverage");
		}
		auto manifest_path = output_path;
		manifest_path.replace_extension(".manifest");
		save_bucket_manifest_atomic(manifest_path, workflow, inspection, std::filesystem::file_size(output_path));
		std::cout << "published bucket artifact: tables=" << writer->progress().table_count
				  << " bytes=" << writer->progress().byte_length << "\n";
		return EXIT_SUCCESS;
	}

	int run_bucket_benchmark(const std::filesystem::path& config_path, std::uint64_t start_index,
		std::uint64_t table_count, std::uint32_t requested_threads, bool publish)
	{
		using namespace texas::solver::multiway;
		const auto total_tables = multiway_bucket_board_count(texas::core::Street::Flop) + multiway_bucket_board_count(texas::core::Street::Turn) + multiway_bucket_board_count(texas::core::Street::River);
		if (table_count == 0U || start_index > total_tables || table_count > total_tables - start_index)
		{
			throw std::invalid_argument("benchmark range exceeds the bucket catalog");
		}
		const auto workflow = load_multiway_workflow_config(config_path);
		const auto identity = make_multiway_model_identity(workflow.model);
		auto profile = MultiwayBucketBaselineProfile::standard();
		profile.flop_bucket_count = workflow.model.flop_bucket_count;
		profile.turn_bucket_count = workflow.model.turn_bucket_count;
		profile.river_bucket_count = workflow.model.river_bucket_count;
		std::uint64_t checksum = 0U;
		MultiwayBucketGenerationStats stats;
		const auto output = std::filesystem::temp_directory_path() / ("texas_solver_bucket_benchmark_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
		const auto temporary = output.string() + ".tmp";
		std::unique_ptr<MultiwayBucketArtifactWriter> writer;
		if (publish)
			writer = std::make_unique<MultiwayBucketArtifactWriter>(temporary, identity, table_count);
		const auto start = std::chrono::steady_clock::now();
		const auto direct_build = [identity, profile](std::uint64_t begin, std::uint64_t end,
									  std::vector<std::uint8_t>& payload) {
			build_multiway_baseline_direct_serialized_chunk(identity, profile, begin, end, payload);
		};
		generate_multiway_bucket_serialized_chunks(start_index, start_index + table_count, { requested_threads, MULTIWAY_BUCKET_GENERATION_CHUNK_SIZE, 0U, &stats, direct_build }, [identity, profile](std::uint64_t begin, std::uint64_t end, std::vector<MultiwayBucketTable>& tables) { build_multiway_baseline_bucket_chunk(identity, profile, begin, end, tables); }, [&checksum, &writer, publish](std::uint64_t, std::uint64_t count, std::vector<std::uint8_t>& payload) {
            for (const auto byte : payload) checksum = checksum * 131U + byte;
            if (publish) writer->append_serialized_chunk(count, payload); });
		const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (publish)
		{
			writer->finish(output);
			std::filesystem::remove(output);
		}
		std::cout << "bucket benchmark,mode=" << (publish ? "end-to-end" : "build-only") << ",start=" << start_index
				  << ",tables=" << table_count
				  << ",threads=" << requested_threads << ",seconds=" << seconds
				  << ",tables_per_second=" << static_cast<double>(table_count) / seconds
				  << ",hardware_threads=" << multiway_bucket_hardware_thread_count()
				  << ",physical_cores=" << multiway_bucket_physical_core_count()
				  << ",checksum=" << checksum << ",chunks_built=" << stats.chunks_built
				  << ",chunks_published=" << stats.chunks_published
				  << ",ready_high_watermark=" << stats.ready_queue_high_watermark
				  << ",worker_wait_ns=" << stats.worker_wait_nanoseconds
				  << ",publish_wait_ns=" << stats.ordered_publish_wait_nanoseconds
				  << ",worker_active_ns=" << stats.worker_active_nanoseconds << "\n";
		return EXIT_SUCCESS;
	}

	int run_inspect(const std::filesystem::path& config_path, const std::filesystem::path& input_path,
		const std::filesystem::path& report_path, bool progress_enabled,
		std::uint64_t progress_interval, std::uint32_t requested_threads,
		texas::solver::multiway::MultiwayBucketInspectionHashMode hash_mode)
	{
		using namespace texas::solver::multiway;
		const auto workflow = load_multiway_workflow_config(config_path);
		auto progress_phase = hash_mode == MultiwayBucketInspectionHashMode::Legacy || requested_threads == 1U
			? "legacy-stream"
			: "parallel-tree";
		const auto report = inspect_multiway_bucket_artifact(
			input_path, make_multiway_model_identity(workflow.model),
			MultiwayBucketInspectionOptions{ requested_threads, hash_mode },
			progress_enabled ? MultiwayBucketInspectionProgressCallback(
								   [&progress_phase](std::uint64_t completed, std::uint64_t total) {
									   if (completed == 0U)
										   progress_phase = "legacy-stream";
									   std::cout << "inspection progress: phase=" << progress_phase
												 << " tables=" << completed << '/' << total << std::endl;
								   })
							 : MultiwayBucketInspectionProgressCallback{},
			progress_interval);
		const auto hash_algorithm = report.hash_mode == MultiwayBucketInspectionHashMode::Legacy
			? "fnv1a64-stream-v1"
			: report.hash_mode == MultiwayBucketInspectionHashMode::Both
			? "fnv1a64-stream-v1+fnv1a64-tree-v1"
			: "fnv1a64-tree-v1";
		if (report.flop_tables == 0U || report.turn_tables == 0U || report.river_tables == 0U)
		{
			throw std::runtime_error("bucket artifact has incomplete street coverage");
		}
		const auto text = std::string("{\n  \"identity\": ") + std::to_string(report.identity.combined_hash) + ",\n  \"evidence\": " + serialize_multiway_evidence_header(make_evidence_header(MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION, make_multiway_model_identity(workflow.model), workflow.fingerprint())) + ",\n  \"flop_tables\": " + std::to_string(report.flop_tables) + ",\n  \"turn_tables\": " + std::to_string(report.turn_tables) + ",\n  \"river_tables\": " + std::to_string(report.river_tables) + ",\n  \"live_assignments\": " + std::to_string(report.live_assignments) + ",\n  \"payload_hash\": " + std::to_string(report.payload_hash) + ",\n  \"parallel_payload_hash\": " + std::to_string(report.parallel_payload_hash) + ",\n  \"hash_algorithm\": \"" + std::string(hash_algorithm) + "\"" + ",\n  \"effective_threads\": " + std::to_string(report.effective_threads) + ",\n  \"identity_matches\": " + (report.identity == make_multiway_model_identity(workflow.model) ? "true" : "false") + ",\n  \"payload_hash_matches\": " + (report.payload_hash != 0U ? "true" : "false") + "\n}\n";
		if (!report_path.empty())
		{
			if (!report_path.parent_path().empty())
				std::filesystem::create_directories(report_path.parent_path());
			preflight_multiway_artifact({ report_path,
				report_path.string() + ".tmp",
				{},
				make_multiway_model_identity(workflow.model),
				1U,
				4096U,
				4096U,
				0U,
				0U });
			const auto temporary = report_path.string() + ".tmp";
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("cannot open inspection report");
			output << text;
			output.close();
			texas::core::publish_atomic_replace(temporary, report_path, "cannot publish inspection report");
		}
		std::cout << "flop_tables=" << report.flop_tables << "\n"
				  << "turn_tables=" << report.turn_tables << "\n"
				  << "river_tables=" << report.river_tables << "\n"
				  << "live_assignments=" << report.live_assignments << "\n"
				  << "payload_hash=" << report.payload_hash << "\n"
				  << "parallel_payload_hash=" << report.parallel_payload_hash << "\n"
				  << "effective_threads=" << report.effective_threads << "\n";
		return EXIT_SUCCESS;
	}

	struct MultiwayTrainingProgressContext
	{
		bool enabled = true;
		bool show_memory = false;
		bool show_speed = false;
		std::uint64_t interval = 1U;
		std::uint64_t starting_batches = 0U;
		std::uint64_t requested_batches = 0U;
		std::uint32_t effective_workers = 0U;
		std::ofstream* log = nullptr;
		std::uint64_t starting_trajectories = 0U;
		std::uint64_t last_reported_trajectories = 0U;
		std::chrono::steady_clock::time_point started_at{};
		std::chrono::steady_clock::time_point last_reported_at{};
	};

	void report_multiway_training_progress(
		const texas::solver::multiway::MultiwayBlueprintTrainingStatus& status,
		void* context)
	{
		auto& progress = *static_cast<MultiwayTrainingProgressContext*>(context);
		if (!progress.enabled || progress.interval == 0U || status.batches < progress.starting_batches)
			return;
		const auto completed_batches = status.batches - progress.starting_batches;
		if (completed_batches == 0U ||
			(completed_batches % progress.interval != 0U && completed_batches != progress.requested_batches))
			return;
		const auto now = std::chrono::steady_clock::now();
		const auto line = std::string("training progress: batches_completed=") +
			std::to_string(completed_batches) + "/" + std::to_string(progress.requested_batches) +
			" trajectories=" + std::to_string(status.trajectories) +
			" discarded=" + std::to_string(status.discarded_trajectories) +
			" effective_workers=" + std::to_string(progress.effective_workers);
		auto output_line = line;
		if (progress.show_memory)
		{
			if (status.process_rss_available)
			{
				output_line += " working_set_current_bytes=" + std::to_string(status.current_process_rss_bytes) +
					" working_set_peak_bytes=" + std::to_string(status.peak_process_rss_bytes) +
					" working_set_gate_bytes=" + std::to_string(status.working_set_gate_config.hard_limit_bytes);
			}
			else
			{
				output_line += " working_set=unavailable";
			}
		}
		if (progress.show_speed)
		{
			const auto interval_seconds = std::chrono::duration<double>(now - progress.last_reported_at).count();
			const auto overall_seconds = std::chrono::duration<double>(now - progress.started_at).count();
			const auto interval_trajectories = status.trajectories - progress.last_reported_trajectories;
			const auto overall_trajectories = status.trajectories - progress.starting_trajectories;
			if (interval_seconds > 0.0)
				output_line += " interval_trajectories_per_second=" + std::to_string(
					static_cast<double>(interval_trajectories) / interval_seconds);
			if (overall_seconds > 0.0)
				output_line += " overall_trajectories_per_second=" + std::to_string(
					static_cast<double>(overall_trajectories) / overall_seconds);
			progress.last_reported_trajectories = status.trajectories;
			progress.last_reported_at = now;
		}
		std::cout << output_line << std::endl;
		if (progress.log != nullptr)
			*progress.log << output_line << '\n' << std::flush;
	}

	int run_train(const std::filesystem::path& config_path, std::filesystem::path input_path,
		std::filesystem::path output_path, std::uint64_t batches,
		std::filesystem::path report_path,
		std::filesystem::path checkpoint_dir,
		const std::filesystem::path& resume_path,
		std::uint32_t worker_override,
		bool pin_workers,
		bool progress_enabled,
		std::uint64_t progress_interval,
		bool progress_memory,
		bool progress_speed,
		texas::solver::multiway::MultiwaySchedulerMode scheduler_mode,
		bool authorize_50m_stage)
	{
		using namespace texas;
		using namespace texas::solver::multiway;
		MultiwayProcessMemorySnapshotCollection process_memory_snapshots;
		if (!process_memory_snapshots.record_current(MultiwayMemoryPhase::ProcessStart))
			throw std::logic_error("multiway process memory snapshot phase order is invalid");
		MultiwayMemorySubsystemSnapshotCollection memory_subsystem_snapshots;
		if (!memory_subsystem_snapshots.record(MultiwayMemoryPhase::ProcessStart, {}))
			throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		TEXASSOLVER_TRACY_SET_THREAD_NAME("mw.train.main");
		TEXASSOLVER_TRACY_ZONE_SCOPED("mw.train.run");
		std::cout << "training starting: config=" << config_path.string() << std::endl;
		const auto workflow = load_multiway_workflow_config(config_path);
		if (input_path.empty())
			input_path = workflow.training_input_path;
		if (output_path.empty())
			output_path = workflow.training_output_path;
		if (report_path.empty())
			report_path = workflow.training_report_path;
		if (checkpoint_dir.empty())
			checkpoint_dir = workflow.training_checkpoint_dir;
		if (batches == 0U)
			batches = workflow.training_batches;
		if (worker_override == 0U)
			worker_override = workflow.training_worker_count;
		if (batches == 0U || input_path.empty() || output_path.empty())
			throw std::invalid_argument("train requires input_path, output_path, and batches");
		const auto reaches_50m = [&](std::uint64_t starting_trajectories) {
			if (starting_trajectories >= 50'000'000U)
				return true;
			const auto remaining_trajectories = 50'000'000U - starting_trajectories;
			const auto full_batches = remaining_trajectories / workflow.trajectories_per_batch;
			return batches > full_batches ||
				(batches == full_batches && remaining_trajectories % workflow.trajectories_per_batch == 0U);
		};
		if (resume_path.empty() && reaches_50m(0U) && !authorize_50m_stage)
			throw std::invalid_argument("50M qualification stage is gated; pass --authorize-50m after explicit approval");
		const auto log_path = workflow.training_log_path.empty()
			? std::filesystem::path(report_path.empty() ? output_path.string() + ".log" : report_path.string() + ".log")
			: workflow.training_log_path;
		if (!log_path.parent_path().empty())
			std::filesystem::create_directories(log_path.parent_path());
		std::ofstream log(log_path, std::ios::app);
		if (!log)
			throw std::runtime_error("cannot open training log");
		const auto log_line = [&log](const std::string& line) {
			std::cout << line << std::endl;
			log << line << '\n'
				<< std::flush;
		};
		log_line("training configured: input=" + input_path.string() + " output=" + output_path.string() + " batches=" + std::to_string(batches) + " threads=" + std::to_string(worker_override) + " pin_workers=" + (pin_workers ? "true" : "false"));
		log_line("training initialization: validating resolved capacities");
		if (!workflow.capacities_resolved())
		{
			throw std::invalid_argument("train requires a sizing-frozen workflow configuration");
		}
		log_line("training initialization: loading bucket artifact");
		if (!process_memory_snapshots.record_current(MultiwayMemoryPhase::BeforeBucketMap))
			throw std::logic_error("multiway process memory snapshot phase order is invalid");
		if (!memory_subsystem_snapshots.record(MultiwayMemoryPhase::BeforeBucketMap, {}))
			throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		const auto buckets = load_multiway_bucket_registry(input_path, &process_memory_snapshots);
		if (!memory_subsystem_snapshots.record(
				MultiwayMemoryPhase::AfterBucketMapMetadata,
				capture_multiway_memory_subsystems(&buckets, nullptr, nullptr)))
			throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		if (!memory_subsystem_snapshots.record(
				MultiwayMemoryPhase::AfterBucketAssignmentValidation,
				capture_multiway_memory_subsystems(&buckets, nullptr, nullptr)))
			throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		log_line("training initialization: bucket artifact loaded tables=" + std::to_string(buckets.tables().size()));
		log_line("training initialization: preparing output and model identity");
		if (!output_path.parent_path().empty())
			std::filesystem::create_directories(output_path.parent_path());
		const auto identity = make_multiway_model_identity(workflow.model);
		const auto estimated_blueprint_bytes = estimate_full_blueprint_bytes(workflow);
		log_line("training initialization: running artifact preflight");
		preflight_multiway_artifact({ output_path,
			output_path.string() + ".tmp",
			{},
			identity,
			1U,
			estimated_blueprint_bytes,
			workflow.disk_space_requirement_bytes,
			estimated_blueprint_bytes,
			workflow.process_memory_limit_bytes });
		log_line("training initialization: constructing training configuration");
		MultiwayBlueprintTrainingConfig config;
		config.blueprint = workflow.model;
		config.bucket_profile.flop_bucket_count = workflow.model.flop_bucket_count;
		config.bucket_profile.turn_bucket_count = workflow.model.turn_bucket_count;
		config.bucket_profile.river_bucket_count = workflow.model.river_bucket_count;
		config.rules = MultiwayGameRules::standard_6max();
		config.max_decision_depth = workflow.max_decision_depth;
		config.max_public_chance_depth = workflow.max_public_chance_depth;
		config.deterministic_seed = workflow.deterministic_seed;
		config.limits.seed = workflow.deterministic_seed;
		const auto workers = resolve_multiway_training_workers(
			workflow.training_worker_count, worker_override, workflow.trajectories_per_batch);
		config.requested_worker_count = workers.requested;
		config.scheduler_mode = scheduler_mode;
		config.pin_workers = pin_workers;
		config.limits.worker_count = workers.effective;
		config.limits.trajectories_per_batch = static_cast<std::uint32_t>(workflow.trajectories_per_batch);
		config.limits.max_public_states = static_cast<std::size_t>(workflow.maximum_public_states);
		config.limits.max_sparse_rows = static_cast<std::size_t>(workflow.maximum_sparse_rows);
		config.limits.max_sparse_values = static_cast<std::size_t>(workflow.maximum_sparse_values);
		config.limits.max_regret_delta_entries_per_worker = static_cast<std::size_t>(workflow.resolved_regret_delta_capacity());
		config.limits.max_continuation_delta_entries_per_worker = static_cast<std::size_t>(workflow.resolved_continuation_delta_capacity());
		config.limits.max_discovery_public_states_per_worker = static_cast<std::size_t>(workflow.resolved_discovery_public_state_capacity());
		config.limits.max_discovery_rows_per_worker = static_cast<std::size_t>(workflow.resolved_discovery_row_capacity());
		config.limits.max_batches = batches;
		config.limits.storage_backend = workflow.storage_backend == "CompactInt32"
			? MultiwaySolverLimits::StorageBackend::CompactInt32
			: MultiwaySolverLimits::StorageBackend::Float64Reference;
		if (workflow.process_memory_limit_bytes < 3U)
		{
			throw std::invalid_argument("training process memory limit is too small");
		}
		const auto operating_bytes = workflow.process_memory_limit_bytes - 1U;
		const auto warning_bytes = std::max<std::uint64_t>(
			1U, operating_bytes - operating_bytes / 5U);
		MultiwayMemoryInputs memory_inputs;
		log_line("training initialization: running memory preflight");
		if (!process_memory_snapshots.record_current(MultiwayMemoryPhase::BeforeMemoryPreflight))
			throw std::logic_error("multiway process memory snapshot phase order is invalid");
		if (!memory_subsystem_snapshots.record(
				MultiwayMemoryPhase::BeforeMemoryPreflight,
				capture_multiway_memory_subsystems(&buckets, nullptr, nullptr)))
			throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		const auto& memory_baseline = process_memory_snapshots.snapshots[process_memory_snapshots.count - 1U];
		memory_inputs.existing_working_set_bytes = memory_baseline.current_working_set_available
			? memory_baseline.current_working_set_bytes
			: 0U;
		memory_inputs.existing_private_committed_bytes = memory_baseline.private_committed_available
			? memory_baseline.private_committed_bytes
			: 0U;
		memory_inputs.resident_baseline_bytes = memory_inputs.existing_working_set_bytes;
		memory_inputs.bucket_registry_bytes = buckets.owned_memory_bytes();
		memory_inputs.mapped_virtual_bytes = buckets.mapped_artifact_bytes();
		memory_inputs.mapped_bucket_artifact_bytes = memory_inputs.mapped_virtual_bytes;
		if (memory_baseline.file_resident_available)
		{
			const auto resident_mapped = std::min(
				memory_inputs.mapped_virtual_bytes, memory_baseline.file_resident_bytes);
			memory_inputs.mapped_resident_allowance_bytes = memory_inputs.mapped_virtual_bytes - resident_mapped;
		}
		memory_inputs.range_row_count = workflow.model.player_count;
		memory_inputs.range_entry_count = static_cast<std::uint64_t>(workflow.model.player_count) * core::CANONICAL_HOLE_COMBINATION_COUNT;
		memory_inputs.export_action_capacity = workflow.maximum_sparse_values;
		memory_inputs.checkpoint_peak_bytes = estimated_blueprint_bytes;
		memory_inputs.export_peak_bytes = estimated_blueprint_bytes;
		const auto memory_preflight = preflight_multiway_memory(
			config.limits,
			{ warning_bytes, workflow.process_memory_limit_bytes, operating_bytes },
			memory_inputs);
		const auto working_set_gate = evaluate_multiway_working_set_preflight(
			memory_preflight,
			{ workflow.process_working_set_limit_bytes,
				workflow.process_working_set_resident_allowance_bytes });
		if (working_set_gate.rejected())
		{
			throw std::runtime_error(std::string("training memory preflight rejected: abort_phase=preflight; ") + working_set_gate.message + "; rejected_category=" + working_set_gate.rejected_category + "; admission_stage=" + std::to_string(static_cast<unsigned>(working_set_gate.admission_stage)) + "; estimated_private_peak_bytes=" + std::to_string(memory_preflight.estimate.estimated_private_peak_bytes) + "; estimated_working_set_peak_bytes=" + std::to_string(memory_preflight.estimate.estimated_working_set_peak_bytes) + "; compared_working_set_bytes=" + std::to_string(working_set_gate.compared_working_set_bytes) + "; gate_bytes=" + std::to_string(working_set_gate.hard_limit_bytes));
		}
		config.memory_preflight_estimate_bytes = memory_preflight.estimate.total_bytes;
		config.memory_ledger = memory_preflight.estimate.ledger;
		config.process_memory_snapshots = process_memory_snapshots;
		config.memory_subsystem_snapshots = memory_subsystem_snapshots;
		config.working_set_monitor.gate = {
			workflow.process_working_set_limit_bytes,
			workflow.process_working_set_resident_allowance_bytes };
		config.working_set_monitor.system_memory_guard = {
			true, MULTIWAY_DEFAULT_SYSTEM_MEMORY_RESERVE_BYTES };
		config.working_set_monitor.export_gate.system = {
			true, MULTIWAY_DEFAULT_SYSTEM_MEMORY_RESERVE_BYTES };
		log_line("training workers requested=" + std::to_string(workers.requested) + " effective=" + std::to_string(workers.effective) + " memory_estimate_bytes=" + std::to_string(memory_preflight.estimate.total_bytes));
		log_line("system memory guard: minimum_available_physical_bytes=" + std::to_string(MULTIWAY_DEFAULT_SYSTEM_MEMORY_RESERVE_BYTES));
		log_line("training initialization: loading bucket artifact bytes=" + std::to_string(std::filesystem::file_size(input_path)));
		log_line("training initialization: constructing six-player uniform ranges");
		MultiwayPrivateConfig ranges;
		ranges.ranges.resize(6U);
		for (std::size_t seat = 0U; seat < ranges.ranges.size(); ++seat)
		{
			ranges.ranges[seat].reserve(core::CANONICAL_HOLE_COMBINATION_COUNT);
			for (core::CanonicalComboId id = 0U; id < core::CANONICAL_HOLE_COMBINATION_COUNT; ++id)
				ranges.ranges[seat].push_back({ core::canonical_combos().cards(id), 1.0 });
		}
		log_line("training initialization: constructing flop/preflop root");
		MultiwayActionAbstraction abstraction;
		const auto root = make_multiway_initial_blueprint_root(config.rules, std::move(ranges), abstraction,
			config.blueprint.action_abstraction_version, config.blueprint.terminal_model_version);
		log_line("training initialization: constructing training session and worker pool");
		MultiwayBlueprintTrainingSession session(config, root, buckets);
		log_line("worker pool constructed: requested=" + std::to_string(workers.requested) + " effective=" + std::to_string(workers.effective));
		const auto checkpoint_path = checkpoint_dir.empty()
			? (resume_path.empty() ? std::filesystem::path{} : resume_path)
			: checkpoint_dir / "latest.bin";
		if (!resume_path.empty())
		{
			log_line("training initialization: loading checkpoint");
			session.resume_from_checkpoint(MultiwayTrainingCheckpointArtifacts::load_verified(
				resume_path, config.identity(), config.schedule.identity(), config.deterministic_seed));
		}
		if (!authorize_50m_stage && reaches_50m(session.status().trajectories))
			throw std::invalid_argument("50M qualification stage is gated; pass --authorize-50m after explicit approval");
		const auto progress_started_at = std::chrono::steady_clock::now();
		MultiwayTrainingProgressContext progress_context{
			progress_enabled,
			progress_memory,
			progress_speed,
			progress_interval,
			session.status().batches,
			batches,
			workers.effective,
			&log,
			session.status().trajectories,
			session.status().trajectories,
			progress_started_at,
			progress_started_at};
		std::uint64_t remaining_batches = batches;
		std::uint64_t checkpoint_bytes = 0U;
		std::uint64_t checkpoint_write_nanoseconds = 0U;
		MultiwayCheckpointTelemetry checkpoint_telemetry{};
		bool checkpoint_snapshots_recorded = false;
		const auto publish_training_failure = [&]() {
			auto failure_report = make_multiway_training_report(
				session.status(), session.status().merged_stream_fingerprint,
				make_evidence_header(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
					config.identity(), workflow.fingerprint()),
				workflow.process_memory_limit_bytes);
			failure_report.checkpoint_bytes = checkpoint_bytes;
			failure_report.checkpoint_write_nanoseconds = checkpoint_write_nanoseconds;
			failure_report.checkpoint_telemetry = checkpoint_telemetry;
			failure_report.failed = true;
			save_multiway_training_report_atomic(
				report_path.empty() ? output_path.string() + ".json" : report_path,
				failure_report);
		};
		try
		{
			while (remaining_batches != 0U)
			{
				const auto next = std::min(remaining_batches, workflow.checkpoint_interval);
				log_line("training started: running batches " + std::to_string(batches - remaining_batches + 1U) + "-" + std::to_string(batches - remaining_batches + next));
				session.run_batches(next, report_multiway_training_progress, &progress_context);
				remaining_batches -= next;
				if (!checkpoint_path.empty())
				{
					if (!checkpoint_path.parent_path().empty())
					{
						std::filesystem::create_directories(checkpoint_path.parent_path());
					}
					const bool capture_checkpoint_phases = remaining_batches == 0U;
					if (capture_checkpoint_phases)
					{
						session.record_process_memory_snapshot(MultiwayMemoryPhase::BeforeCheckpoint);
					}
					const auto checkpoint_start = std::chrono::steady_clock::now();
					auto checkpoint = session.checkpoint();
					checkpoint.validate();
					checkpoint_telemetry.accumulate(checkpoint.telemetry);
					MultiwayTrainingCheckpointArtifacts::save_atomic_sealed(checkpoint_path, checkpoint, &checkpoint_telemetry);
					checkpoint_write_nanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - checkpoint_start)
							.count());
					std::error_code checkpoint_error;
					const auto bytes = std::filesystem::file_size(checkpoint_path, checkpoint_error);
					if (checkpoint_error)
						throw std::runtime_error("cannot measure training checkpoint");
					checkpoint_bytes = std::max(checkpoint_bytes, static_cast<std::uint64_t>(bytes));
					if (capture_checkpoint_phases)
					{
						session.record_process_memory_snapshot(MultiwayMemoryPhase::AfterCheckpoint);
						checkpoint_snapshots_recorded = true;
					}
				}
			}
		}
		catch (const std::exception& error)
		{
			log_line("training failed: " + std::string(error.what()));
			publish_training_failure();
			throw;
		}
		catch (...)
		{
			log_line("training failed: unknown exception");
			publish_training_failure();
			throw;
		}
		const auto& status = session.status();
		auto training_report = make_multiway_training_report(
			status, status.merged_stream_fingerprint,
			make_evidence_header(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
				config.identity(), workflow.fingerprint()),
			workflow.process_memory_limit_bytes);
		training_report.checkpoint_telemetry = checkpoint_telemetry;
		if (status.discarded_trajectories != 0U || status.preflop_rows == 0U || status.flop_rows == 0U || status.turn_rows == 0U || status.river_rows == 0U || status.terminal_visits == 0U)
		{
			training_report.failed = true;
			save_multiway_training_report_atomic(
				report_path.empty() ? output_path.string() + ".json" : report_path,
				training_report);
			throw std::runtime_error("training failed the F1 coverage or discard gate");
		}
		if (!checkpoint_snapshots_recorded)
		{
			session.record_process_memory_snapshot(MultiwayMemoryPhase::BeforeCheckpoint);
			session.record_process_memory_snapshot(MultiwayMemoryPhase::AfterCheckpoint);
		}
		MultiwayFullBlueprintExportResult export_result;
		try
		{
			export_result = session.save_full_policy_atomic(output_path);
		}
		catch (const std::exception& error)
		{
			log_line("training export failed: " + std::string(error.what()));
			publish_training_failure();
			throw;
		}
		catch (...)
		{
			log_line("training export failed: unknown exception");
			publish_training_failure();
			throw;
		}
		training_report = make_multiway_training_report(
			session.status(), status.merged_stream_fingerprint,
			make_evidence_header(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
				config.identity(), workflow.fingerprint()),
			workflow.process_memory_limit_bytes);
		training_report.checkpoint_bytes = checkpoint_bytes;
		training_report.checkpoint_write_nanoseconds = checkpoint_write_nanoseconds;
		training_report.blueprint_bytes = session.status().export_telemetry.result_bytes;
		training_report.blueprint_export_nanoseconds = session.status().export_telemetry.duration_nanoseconds;
		save_multiway_training_report_atomic(
			report_path.empty() ? output_path.string() + ".json" : report_path,
			training_report);
		log_line("training export: plan_bytes=" + std::to_string(export_result.plan.serialized_bytes) +
			" result_bytes=" + std::to_string(session.status().export_telemetry.result_bytes) +
			" payload_hash=" + std::to_string(export_result.payload_hash) +
			" duration_nanoseconds=" + std::to_string(session.status().export_telemetry.duration_nanoseconds) +
			" publication_mode=" + core::atomic_publish_mode_name(export_result.publication_mode) +
			" process_gate=" + multiway_export_process_rejection_reason_name(session.status().export_telemetry.process_gate.rejection_reason) +
			" system_gate=" + multiway_export_system_rejection_reason_name(session.status().export_telemetry.system_gate.rejection_reason));
		log_line("training finished: batches=" + std::to_string(batches) + " trajectories=" + std::to_string(status.trajectories) + " blueprint=" + output_path.string());
		return EXIT_SUCCESS;
	}

	int run_evaluate(const std::filesystem::path& config_path,
		const std::filesystem::path& artifacts_path,
		std::uint64_t trajectories,
		const std::filesystem::path& report_path)
	{
		using namespace texas;
		using namespace texas::solver::multiway;
		if (artifacts_path.empty() || trajectories == 0U)
		{
			throw std::invalid_argument("evaluate requires --artifacts and --duplicates");
		}
		const auto workflow = load_multiway_workflow_config(config_path);
		if (!workflow.capacities_resolved())
		{
			throw std::invalid_argument("evaluate requires a sizing-frozen workflow configuration");
		}
		const auto identity = make_multiway_model_identity(workflow.model);
		const auto buckets = load_multiway_bucket_registry(artifacts_path / "buckets.bin");
		const auto blueprint = MultiwayFullBlueprintArtifacts::load_verified(
			artifacts_path / "blueprint.bin", identity);
		MultiwayBlueprintTrainingConfig config;
		config.blueprint = workflow.model;
		config.bucket_profile.flop_bucket_count = workflow.model.flop_bucket_count;
		config.bucket_profile.turn_bucket_count = workflow.model.turn_bucket_count;
		config.bucket_profile.river_bucket_count = workflow.model.river_bucket_count;
		config.rules = MultiwayGameRules::standard_6max();
		config.max_decision_depth = workflow.max_decision_depth;
		config.max_public_chance_depth = workflow.max_public_chance_depth;
		config.deterministic_seed = workflow.deterministic_seed;
		config.limits.seed = workflow.deterministic_seed;
		config.limits.worker_count = workflow.training_worker_count;
		config.limits.trajectories_per_batch = static_cast<std::uint32_t>(workflow.trajectories_per_batch);
		config.limits.max_public_states = static_cast<std::size_t>(workflow.maximum_public_states);
		config.limits.max_sparse_rows = static_cast<std::size_t>(workflow.maximum_sparse_rows);
		config.limits.max_sparse_values = static_cast<std::size_t>(workflow.maximum_sparse_values);
		config.limits.max_regret_delta_entries_per_worker = static_cast<std::size_t>(workflow.resolved_regret_delta_capacity());
		config.limits.max_continuation_delta_entries_per_worker = static_cast<std::size_t>(workflow.resolved_continuation_delta_capacity());
		config.limits.max_discovery_public_states_per_worker = static_cast<std::size_t>(workflow.resolved_discovery_public_state_capacity());
		config.limits.max_discovery_rows_per_worker = static_cast<std::size_t>(workflow.resolved_discovery_row_capacity());
		config.limits.storage_backend = MultiwaySolverLimits::StorageBackend::CompactInt32;
		MultiwayPrivateConfig ranges;
		ranges.ranges.resize(6U);
		for (auto& range : ranges.ranges)
		{
			range.reserve(core::CANONICAL_HOLE_COMBINATION_COUNT);
			for (core::CanonicalComboId id = 0U; id < core::CANONICAL_HOLE_COMBINATION_COUNT; ++id)
			{
				range.push_back({ core::canonical_combos().cards(id), 1.0 });
			}
		}
		MultiwayActionAbstraction abstraction;
		const auto root = make_multiway_initial_blueprint_root(
			config.rules, std::move(ranges), abstraction,
			config.blueprint.action_abstraction_version, config.blueprint.terminal_model_version);
		const auto evidence = make_evidence_header(
			MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION, config.identity(), workflow.fingerprint());
		auto report = qualify_multiway_required_lookups(
			config, root, buckets, blueprint, trajectories, evidence);
		const auto repeat = qualify_multiway_required_lookups(
			config, root, buckets, blueprint, trajectories, evidence);
		report.second_replay_fingerprint = repeat.replay_fingerprint;
		const auto output = report_path.empty() ? artifacts_path / "lookup_report.json" : report_path;
		if (!output.parent_path().empty())
			std::filesystem::create_directories(output.parent_path());
		save_multiway_lookup_qualification_report_atomic(output, report);
		if (!report.passed() || repeat.lookup_hits != report.lookup_hits || repeat.missing_infosets != report.missing_infosets || repeat.missing_buckets != report.missing_buckets || repeat.action_menu_mismatches != report.action_menu_mismatches)
		{
			throw std::runtime_error("required blueprint lookup qualification failed");
		}
		return EXIT_SUCCESS;
	}

	int run_finalize(const std::filesystem::path& output_path,
		const std::filesystem::path& bucket_report,
		const std::filesystem::path& training_report,
		const std::filesystem::path& equivalence_report,
		const std::filesystem::path& lookup_first,
		const std::filesystem::path& lookup_second,
		texas::solver::multiway::MultiwayF1HumanRunRecord human_run)
	{
		using namespace texas::solver::multiway;
		if (output_path.empty())
			throw std::invalid_argument("finalize requires --output");
		MultiwayF1AcceptanceInputPaths paths;
		paths.bucket_inspection_report = bucket_report;
		paths.training_report = training_report;
		paths.checkpoint_equivalence_report = equivalence_report;
		paths.first_lookup_report = lookup_first;
		paths.second_lookup_report = lookup_second;
		paths.human_run = std::move(human_run);
		finalize_multiway_f1_acceptance_atomic(output_path, paths);
		std::cout << "published F1 acceptance report: " << output_path.string() << "\n";
		return EXIT_SUCCESS;
	}

	bool consume_value(int& index, int argc, char** argv, std::string_view option)
	{
		if (std::string_view(argv[index]) != option || index + 1 >= argc)
			return false;
		++index;
		return true;
	}

	int run(std::string_view name, int argc, char** argv)
	{
		const Workflow workflow = workflow_from_name(name);
		std::filesystem::path config_path;
		std::filesystem::path output_path;
		std::filesystem::path input_path;
		std::filesystem::path report_path;
		std::filesystem::path checkpoint_dir;
		std::filesystem::path resume_path;
		std::filesystem::path artifacts_path;
		std::filesystem::path bucket_report;
		std::filesystem::path training_report;
		std::filesystem::path equivalence_report;
		std::filesystem::path lookup_first;
		std::filesystem::path lookup_second;
		std::string operator_id;
		std::string start_utc;
		std::string end_utc;
		std::string command;
		std::filesystem::path machine_report;
		std::int32_t exit_code = -1;
		std::uint64_t batches = 0U;
		std::uint64_t duplicates = 0U;
		std::uint32_t threads = (std::min)(texas::solver::multiway::MULTIWAY_BUCKET_DEFAULT_THREADS,
			texas::solver::multiway::multiway_bucket_physical_core_count());
		bool threads_specified = false;
		bool pin_workers = false;
		bool authorize_50m_stage = false;
		auto scheduler_mode = texas::solver::multiway::MultiwaySchedulerMode::Fixed;
		std::uint64_t benchmark_tables = 0U;
		std::uint64_t benchmark_start = 0U;
		std::string benchmark_mode = "build-only";
		bool progress_enabled = true;
		bool progress_memory = false;
		bool progress_speed = false;
		std::uint64_t inspect_progress_interval =
			texas::solver::multiway::MULTIWAY_BUCKET_INSPECTION_DEFAULT_PROGRESS_INTERVAL;
		std::uint64_t training_progress_interval = 1U;
		auto inspect_hash_mode = texas::solver::multiway::MultiwayBucketInspectionHashMode::Parallel;
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument(argv[index]);
			if (argument == "--help")
			{
				print_help(name, workflow);
				return EXIT_SUCCESS;
			}
			if (argument == "--tiny")
				return run_tiny_pipeline();
			if (argument == "--authorize-50m")
			{
				authorize_50m_stage = true;
				continue;
			}
			if (argument == "--config" || argument == "--seed" || argument == "--batches" || argument == "--checkpoint-dir" || argument == "--resume" || argument == "--output" || argument == "--input" || argument == "--report" || argument == "--artifacts" || argument == "--duplicates" || argument == "--threads" || argument == "--pin-workers" || argument == "--scheduler" || argument == "--benchmark-start" || argument == "--benchmark-tables" || argument == "--benchmark-mode" || argument == "--progress" || argument == "--progress-every" || argument == "--progress-memory" || argument == "--progress-speed" || argument == "--hash" || argument == "--bucket-report" || argument == "--training-report" || argument == "--equivalence-report" || argument == "--lookup-first" || argument == "--lookup-second" || argument == "--operator" || argument == "--start-utc" || argument == "--end-utc" || argument == "--command" || argument == "--machine-report" || argument == "--exit-code")
			{
				if (!consume_value(index, argc, argv, argument))
				{
					std::cerr << argument << " requires a value\n";
					return EXIT_FAILURE;
				}
				if (argument == "--config")
					config_path = argv[index];
				if (argument == "--output")
					output_path = argv[index];
				if (argument == "--input")
					input_path = argv[index];
				if (argument == "--report")
					report_path = argv[index];
				if (argument == "--checkpoint-dir")
					checkpoint_dir = argv[index];
				if (argument == "--resume")
					resume_path = argv[index];
				if (argument == "--artifacts")
					artifacts_path = argv[index];
				if (argument == "--batches")
					batches = std::stoull(argv[index]);
				if (argument == "--duplicates")
					duplicates = std::stoull(argv[index]);
				if (argument == "--threads")
				{
					const auto parsed = std::stoull(argv[index]);
					if (parsed > std::numeric_limits<std::uint32_t>::max())
					{
						throw std::invalid_argument("--threads exceeds uint32 range");
					}
					threads = static_cast<std::uint32_t>(parsed);
					threads_specified = true;
				}
				if (argument == "--pin-workers")
				{
					const std::string_view value(argv[index]);
					if (value == "true")
						pin_workers = true;
					else if (value == "false")
						pin_workers = false;
					else
						throw std::invalid_argument("--pin-workers must be true or false");
				}
				if (argument == "--scheduler")
				{
					const std::string_view value(argv[index]);
					if (value == "fixed")
						scheduler_mode = texas::solver::multiway::MultiwaySchedulerMode::Fixed;
					else if (value == "dynamic")
						scheduler_mode = texas::solver::multiway::MultiwaySchedulerMode::Dynamic;
					else
						throw std::invalid_argument("--scheduler must be fixed or dynamic");
				}
				if (argument == "--benchmark-tables")
					benchmark_tables = std::stoull(argv[index]);
				if (argument == "--benchmark-start")
					benchmark_start = std::stoull(argv[index]);
				if (argument == "--benchmark-mode")
					benchmark_mode = argv[index];
				if (argument == "--progress")
				{
					const std::string_view value(argv[index]);
					if (value == "true")
						progress_enabled = true;
					else if (value == "false")
						progress_enabled = false;
					else
						throw std::invalid_argument("--progress must be true or false");
				}
				if (argument == "--progress-every")
				{
					const auto interval = std::stoull(argv[index]);
					if (interval == 0U)
						throw std::invalid_argument("--progress-every must be positive");
					inspect_progress_interval = interval;
					training_progress_interval = interval;
				}
				if (argument == "--progress-memory")
				{
					const std::string_view value(argv[index]);
					if (value == "true")
						progress_memory = true;
					else if (value == "false")
						progress_memory = false;
					else
						throw std::invalid_argument("--progress-memory must be true or false");
				}
				if (argument == "--progress-speed")
				{
					const std::string_view value(argv[index]);
					if (value == "true")
						progress_speed = true;
					else if (value == "false")
						progress_speed = false;
					else
						throw std::invalid_argument("--progress-speed must be true or false");
				}
				if (argument == "--hash")
				{
					const std::string_view value(argv[index]);
					if (value == "parallel")
						inspect_hash_mode = texas::solver::multiway::MultiwayBucketInspectionHashMode::Parallel;
					else if (value == "legacy")
						inspect_hash_mode = texas::solver::multiway::MultiwayBucketInspectionHashMode::Legacy;
					else if (value == "both")
						inspect_hash_mode = texas::solver::multiway::MultiwayBucketInspectionHashMode::Both;
					else
						throw std::invalid_argument("--hash must be parallel, legacy, or both");
				}
				if (argument == "--bucket-report")
					bucket_report = argv[index];
				if (argument == "--training-report")
					training_report = argv[index];
				if (argument == "--equivalence-report")
					equivalence_report = argv[index];
				if (argument == "--lookup-first")
					lookup_first = argv[index];
				if (argument == "--lookup-second")
					lookup_second = argv[index];
				if (argument == "--operator")
					operator_id = argv[index];
				if (argument == "--start-utc")
					start_utc = argv[index];
				if (argument == "--end-utc")
					end_utc = argv[index];
				if (argument == "--command")
					command = argv[index];
				if (argument == "--machine-report")
					machine_report = argv[index];
				if (argument == "--exit-code")
					exit_code = static_cast<std::int32_t>(std::stoll(argv[index]));
				continue;
			}
			std::cerr << "unknown option: " << argument << "\n";
			return EXIT_FAILURE;
		}
		if (workflow == Workflow::Buckets && !config_path.empty() && benchmark_tables != 0U)
		{
			if (benchmark_mode != "build-only" && benchmark_mode != "end-to-end")
			{
				throw std::invalid_argument("--benchmark-mode must be build-only or end-to-end");
			}
			return run_bucket_benchmark(config_path, benchmark_start, benchmark_tables, threads, benchmark_mode == "end-to-end");
		}
		if (workflow == Workflow::Buckets && !config_path.empty() && !output_path.empty())
		{
			return run_buckets(config_path, output_path, checkpoint_dir, threads);
		}
		if (workflow == Workflow::Inspect && !config_path.empty() && !input_path.empty())
		{
			return run_inspect(config_path, input_path, report_path, progress_enabled, inspect_progress_interval, threads, inspect_hash_mode);
		}
		if (workflow == Workflow::Train && !config_path.empty())
		{
			return run_train(config_path, input_path, output_path, batches, report_path, checkpoint_dir,
				resume_path, threads_specified ? threads : 0U, pin_workers, progress_enabled,
				training_progress_interval, progress_memory, progress_speed, scheduler_mode, authorize_50m_stage);
		}
		if (workflow == Workflow::Evaluate && !config_path.empty())
		{
			return run_evaluate(config_path, artifacts_path, duplicates, report_path);
		}
		if (workflow == Workflow::Finalize)
		{
			return run_finalize(output_path, bucket_report, training_report, equivalence_report,
				lookup_first, lookup_second,
				{ operator_id, start_utc, end_utc, command, machine_report.string(), exit_code });
		}
		std::cerr << name << " requires an explicit workflow configuration; use --help\n";
		return EXIT_FAILURE;
	}

} // namespace

int main(int argc, char** argv) noexcept
{
	try
	{
		const std::string_view executable = argc == 0 ? "multiway" : argv[0];
		const auto slash = executable.find_last_of("/\\");
		auto name = executable.substr(slash == std::string_view::npos ? 0 : slash + 1);
		if (name.size() > 4U && name.substr(name.size() - 4U) == ".exe")
		{
			name.remove_suffix(4U);
		}
		if (name == "texas_multiway_train" || name == "texas_multiway_train_tracy")
			return run("train", argc, argv);
		if (name == "texas_multiway_buckets")
			return run("buckets", argc, argv);
		if (name == "texas_multiway_inspect")
			return run("inspect", argc, argv);
		if (name == "texas_multiway_finalize")
			return run("finalize", argc, argv);
		return run("evaluate", argc, argv);
	}
	catch (const std::exception& error)
	{
		std::cerr << "workflow failed: " << error.what() << "\n";
		return EXIT_FAILURE;
	}
	catch (...)
	{
		std::cerr << "workflow failed: unknown exception\n";
		return EXIT_FAILURE;
	}
}
