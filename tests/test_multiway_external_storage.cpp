#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
	class TestDirectory
	{
	public:
		TestDirectory()
		{
			static std::atomic<std::uint32_t> next{ 0U };
			const auto root = std::filesystem::temp_directory_path();
			for (;;)
			{
				path_ = root / ("texassolver_external_store_" + std::to_string(next.fetch_add(1U)));
				std::error_code error;
				if (std::filesystem::create_directory(path_, error)) break;
				if (error) throw std::runtime_error("test directory cannot be created");
			}
		}
		~TestDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
		const std::filesystem::path& path() const noexcept { return path_; }
	private:
		std::filesystem::path path_;
	};

	texas::solver::multiway::MultiwayExternalStorageConfig config(const std::filesystem::path& path)
	{
		return { path, 4096U, 4096U, 4096U, 2U, 8192U, 1U, false };
	}

	std::vector<texas::solver::multiway::MultiwayExternalRecord> records(std::uint64_t start)
	{
		using texas::solver::multiway::MultiwayExternalRecord;
		return { { start, { static_cast<std::uint8_t>(start), 0xa5U, 0x5aU, 0xffU } },
			{ start + 2U, { static_cast<std::uint8_t>(start + 2U), 0xa5U, 0x5aU, 0xffU } } };
	}
}

TEST_CASE(multiway_external_record_store_round_trips_and_recovers_batches)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		store.append_batch(1U, 0x1234U, records(10U));
		store.append_batch(3U, 0x5678U, records(20U));
		EXPECT_EQ(store.record_count(), 4U);
		EXPECT_EQ(store.generation(), 2U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(store.lookup(22U, payload));
		EXPECT_EQ(payload[0], 22U);
		EXPECT_TRUE(store.lookup(22U, payload));
		EXPECT_TRUE(!store.lookup(21U, payload));
		const auto stats = store.stats();
		EXPECT_TRUE(stats.cache_hits >= 1U);
		EXPECT_TRUE(stats.cache_misses >= 2U);
		EXPECT_TRUE(stats.disk_reads >= 2U);
	}
	{
		MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
		EXPECT_EQ(reopened.record_count(), 4U);
		EXPECT_EQ(reopened.generation(), 2U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(reopened.lookup(10U, payload));
		EXPECT_EQ(payload[1], 0xa5U);
		EXPECT_TRUE(reopened.cache_capacity_records() > 0U);
		EXPECT_TRUE(reopened.cache_bytes() <= config(directory.path()).cache_bytes);
	}
}

TEST_CASE(multiway_external_record_store_uses_bounded_constant_time_cache_slots)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto settings = config(directory.path());
	settings.cache_bytes = 64U;
	MultiwayExternalRecordStore store(settings, 4U);
	EXPECT_EQ(store.cache_capacity_records(), 1U);
	store.append_batch(1U, 0x55U, records(10U));
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup(10U, payload));
	std::array<std::uint8_t, 4U> fixed_payload{};
	EXPECT_TRUE(store.lookup(10U, fixed_payload.data(), fixed_payload.size()));
	EXPECT_EQ(fixed_payload[0], payload[0]);
	EXPECT_TRUE(store.lookup(12U, payload));
	EXPECT_TRUE(store.lookup(12U, payload));
	const auto stats = store.stats();
	EXPECT_EQ(stats.cache_misses, 2U);
	EXPECT_EQ(stats.cache_hits, 2U);
	EXPECT_EQ(stats.cache_evictions, 1U);
}

TEST_CASE(multiway_external_generation_manifest_is_atomic_and_checksummed)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 7U;
	first.merge_fingerprint = 0x1234U;
	first.catalog_generation = 2U;
	first.public_state_count = 100U;
	first.row_generation = 3U;
	first.row_count = 40U;
	first.page_generation = 4U;
	first.page_count = 75U;
	first.content_hash = 0x9876U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalGenerationManifest loaded{};
	EXPECT_TRUE(load_multiway_external_generation(directory.path(), loaded));
	EXPECT_EQ(loaded.generation, first.generation);
	EXPECT_EQ(loaded.committed_batch_number, first.committed_batch_number);
	EXPECT_EQ(loaded.public_state_count, first.public_state_count);
	EXPECT_EQ(loaded.page_count, first.page_count);
	EXPECT_EQ(loaded.content_hash, first.content_hash);

	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 8U;
	std::ofstream(directory.path() / "TRAINING_CURRENT.tmp", std::ios::binary).put('x');
	bool publication_rejected = false;
	try { publish_multiway_external_generation(directory.path(), second, false); }
	catch (const std::runtime_error&) { publication_rejected = true; }
	EXPECT_TRUE(publication_rejected);
	EXPECT_TRUE(load_multiway_external_generation(directory.path(), loaded));
	EXPECT_EQ(loaded.generation, first.generation);

	std::error_code ignored;
	std::filesystem::remove(directory.path() / "TRAINING_CURRENT.tmp", ignored);
	{
		std::fstream current(directory.path() / "TRAINING_CURRENT", std::ios::binary | std::ios::in | std::ios::out);
		char byte = 0;
		current.read(&byte, 1);
		byte ^= 1;
		current.seekp(0);
		current.write(&byte, 1);
	}
	bool corruption_rejected = false;
	try { load_multiway_external_generation(directory.path(), loaded); }
	catch (const std::invalid_argument&) { corruption_rejected = true; }
	EXPECT_TRUE(corruption_rejected);
}

TEST_CASE(multiway_external_batch_transaction_rolls_back_partial_component_publication)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	const std::vector<MultiwayExternalRecord> initial = { { 1U, { 1U, 2U, 3U, 4U } } };
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, initial);
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	first.public_state_count = first.row_count = first.page_count = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	{
		MultiwayExternalBatchTransaction transaction(directory.path(), false);
		transaction.begin_batch(2U, 0x222U);
		for (const auto& path : { catalog_path, rows_path })
		{
			MultiwayExternalRecordStore store(config(path), 4U);
			store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
		}
	}
	MultiwayExternalBatchTransaction recovered(directory.path(), false);
	EXPECT_EQ(recovered.current_manifest().committed_batch_number, 1U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(store.lookup(1U, payload));
		EXPECT_TRUE(!store.lookup(2U, payload));
		EXPECT_EQ(store.generation(), 1U);
	}
}

TEST_CASE(multiway_external_batch_transaction_publishes_one_verified_commit_point)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalBatchTransaction transaction(directory.path(), false);
	transaction.begin_batch(2U, 0U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 2U;
	second.merge_fingerprint = 0x222U;
	second.catalog_generation = second.row_generation = second.page_generation = 2U;
	second.public_state_count = second.row_count = second.page_count = 2U;
	transaction.set_merge_fingerprint(0x222U);
	transaction.commit_batch(second);
	EXPECT_TRUE(!transaction.batch_open());
	EXPECT_EQ(transaction.current_manifest().generation, 2U);
	MultiwayExternalBatchTransaction reopened(directory.path(), false);
	EXPECT_EQ(reopened.current_manifest().committed_batch_number, 2U);
}

TEST_CASE(multiway_external_batch_transaction_rolls_back_when_global_publish_fails)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalBatchTransaction transaction(directory.path(), false);
	transaction.begin_batch(2U, 0x222U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 2U;
	second.merge_fingerprint = 0x222U;
	second.catalog_generation = second.row_generation = second.page_generation = 2U;
	std::ofstream(directory.path() / "TRAINING_CURRENT.tmp", std::ios::binary).put('x');
	bool commit_rejected = false;
	try { transaction.commit_batch(second); }
	catch (const std::runtime_error&) { commit_rejected = true; }
	EXPECT_TRUE(commit_rejected);
	transaction.abort_batch();
	MultiwayExternalBatchTransaction reopened(directory.path(), false);
	EXPECT_EQ(reopened.current_manifest().committed_batch_number, 1U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(!store.lookup(2U, payload));
		EXPECT_EQ(store.generation(), 1U);
	}
}

TEST_CASE(multiway_external_batch_transaction_recovers_unfinalized_merge_fingerprint)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	{
		MultiwayExternalBatchTransaction transaction(directory.path(), false);
		transaction.begin_batch(2U, 0U);
		MultiwayExternalRecordStore rows(config(rows_path), 4U);
		rows.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	MultiwayExternalBatchTransaction recovered(directory.path(), false);
	EXPECT_EQ(recovered.current_manifest().committed_batch_number, 1U);
	MultiwayExternalRecordStore rows(config(rows_path), 4U);
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(!rows.lookup(2U, payload));
	EXPECT_EQ(rows.generation(), 1U);
}

TEST_CASE(multiway_external_training_storage_commits_components_atomically)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto catalog_config = config(directory.path() / "catalog");
	auto row_config = config(directory.path() / "rows");
	auto page_config = config(directory.path() / "pages");
	page_config.compaction_scratch_bytes = 32768U;
	MultiwayPackedPublicNode node{};
	node.id.value = 1U;
	MultiwayPublicStateDescriptor descriptor{};
	descriptor.id = node.id;
	const MultiwaySparseRowShape shape{ { node.id, 0 }, 2U, 2U };
	std::uint64_t committed_content_hash = 0U;
	{
		MultiwayExternalTrainingStorage storage(directory.path(), catalog_config,
			row_config, page_config, 16U, 65536U, 8U, false);
		storage.begin_batch(1U);
		storage.append_catalog(0x101U, { node }, { descriptor });
		storage.admit_rows(0x102U, { shape });
		storage.begin_page_batch();
		MultiwayExternalRowBinding binding{};
		EXPECT_TRUE(storage.compact_storage().lookup_row(shape.infoset, binding));
		auto page = storage.compact_storage().pin_bucket(binding, 1U, true);
		storage.compact_storage().apply_delta(page, 1U, 2.0, 3.0);
		storage.compact_storage().unpin_bucket(page);
		storage.commit_batch(0xabcU, 1U);
		EXPECT_EQ(storage.current_manifest().generation, 1U);
		EXPECT_EQ(storage.current_manifest().public_state_count, 1U);
		EXPECT_EQ(storage.current_manifest().row_count, 1U);
		EXPECT_EQ(storage.current_manifest().page_count, 1U);
		EXPECT_TRUE(storage.current_manifest().content_hash != 0U);
		committed_content_hash = storage.current_manifest().content_hash;
	}
	{
		MultiwayExternalTrainingStorage reopened(directory.path(), catalog_config,
			row_config, page_config, 16U, 65536U, 8U, false);
		EXPECT_EQ(reopened.current_manifest().committed_batch_number, 1U);
		EXPECT_EQ(reopened.current_manifest().content_hash, committed_content_hash);
		EXPECT_TRUE(reopened.catalog().contains(node.id));
		MultiwayExternalRowBinding binding{};
		EXPECT_TRUE(reopened.compact_storage().lookup_row(shape.infoset, binding));
		std::array<double, 2U> average{};
		reopened.compact_storage().average_strategy_into(binding, 1U,
			average.data(), average.size());
		EXPECT_EQ(average[0], 0.0);
		EXPECT_EQ(average[1], 1.0);
		reopened.begin_batch(2U);
		MultiwayPackedPublicNode child{};
		child.id.value = 2U;
		child.parent_id = node.id;
		MultiwayPublicStateDescriptor child_descriptor{};
		child_descriptor.id = child.id;
		const MultiwaySparseRowShape child_shape{ { child.id, 1 }, 1U, 1U };
		reopened.append_catalog(0x201U, { child }, { child_descriptor });
		reopened.admit_rows(0x202U, { child_shape });
		reopened.begin_page_batch();
		MultiwayExternalRowBinding child_binding{};
		EXPECT_TRUE(reopened.compact_storage().lookup_row(child_shape.infoset, child_binding));
		auto dirty_page = reopened.compact_storage().pin_bucket(child_binding, 0U, true);
		reopened.compact_storage().apply_delta(dirty_page, 0U, 1.0, 1.0);
		reopened.compact_storage().unpin_bucket(dirty_page);
		std::ofstream(directory.path() / "TRAINING_CURRENT.tmp", std::ios::binary).put('x');
		bool publication_failed = false;
		try { reopened.commit_batch(0xdefU, 2U); }
		catch (const std::runtime_error&) { publication_failed = true; }
		EXPECT_TRUE(publication_failed);
		reopened.abort_batch();
		EXPECT_EQ(reopened.current_manifest().generation, 1U);
		EXPECT_EQ(reopened.current_manifest().content_hash, committed_content_hash);
		EXPECT_TRUE(!reopened.catalog().contains(child.id));
		EXPECT_TRUE(!reopened.compact_storage().lookup_row(child_shape.infoset, child_binding));
	}
}

TEST_CASE(multiway_external_batch_transaction_keeps_published_generation_after_cleanup_failure)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	first.public_state_count = first.row_count = first.page_count = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalBatchTransaction transaction(directory.path(), false);
	transaction.begin_batch(2U, 0x222U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 2U;
	second.merge_fingerprint = 0x222U;
	second.catalog_generation = second.row_generation = second.page_generation = 2U;
	second.public_state_count = second.row_count = second.page_count = 2U;
	const auto catalog_backup = directory.path() / "CATALOG_CURRENT.previous";
	std::filesystem::remove(catalog_backup);
	std::filesystem::create_directory(catalog_backup);
	std::ofstream(catalog_backup / "held", std::ios::binary).put('x');
	bool cleanup_failed = false;
	try { transaction.commit_batch(second); }
	catch (const std::runtime_error&) { cleanup_failed = true; }
	EXPECT_TRUE(cleanup_failed);
	MultiwayExternalBatchTransaction reopened(directory.path(), false);
	EXPECT_EQ(reopened.current_manifest().committed_batch_number, 2U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(store.lookup(2U, payload));
		EXPECT_EQ(store.generation(), 2U);
	}
}

TEST_CASE(multiway_external_public_node_codec_is_packed_and_deterministic)
{
	using namespace texas::solver::multiway;
	MultiwayPackedPublicNode node{};
	node.id.value = 0x0102030405060708ULL;
	node.parent_id.value = 17U;
	node.canonical_history_id = 0x8877665544332211ULL;
	node.betting.seat_count = 6U;
	node.betting.current_player = 4;
	node.betting.last_aggressor = 2;
	node.betting.current_bet = 1200;
	node.betting.last_full_raise_size = 600;
	node.betting.big_blind = 100;
	node.betting.street = texas::core::Street::Turn;
	node.betting.stacks[0] = 50000;
	node.betting.folded[3] = 1U;
	node.board = { 1U, 2U, 3U, 4U, 5U };
	node.board_count = 5U;
	node.board_runout.remaining_board_cards = 1U;
	node.board_runout.chance_only_runout = true;
	node.incoming_kind = MultiwayPublicParentEdgeKind::BettingAction;
	node.incoming_action.action_index = 3U;
	node.incoming_action.target_street_contribution = 1200;
	node.incoming_action.action_menu_id = 0x123456789ULL;
	node.incoming_actor = 4;
	node.dealt_card = 7U;
	node.dealt_cards = { 7U, 8U, 9U };
	node.dealt_card_count = 3U;
	node.transition_board = { 1U, 2U, 3U, 4U, 5U };
	node.transition_board_count = 5U;
	node.legal_menu_handle = 19U;

	std::vector<std::uint8_t> encoded;
	encode_external_public_node(node, encoded);
	EXPECT_EQ(encoded.size(), external_public_node_record_size());
	EXPECT_TRUE(encoded.size() < sizeof(MultiwayPackedPublicNode));
	MultiwayPackedPublicNode decoded{};
	EXPECT_TRUE(decode_external_public_node(encoded.data(), encoded.size(), decoded));
	std::vector<std::uint8_t> reencoded;
	encode_external_public_node(decoded, reencoded);
	EXPECT_TRUE(reencoded == encoded);
	encoded[0] ^= 1U;
	EXPECT_TRUE(!decode_external_public_node(encoded.data(), encoded.size(), decoded));
}

TEST_CASE(multiway_external_public_catalog_reconstructs_parent_first_after_restart)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayPackedPublicNode root{};
	root.id.value = 1U;
	MultiwayPackedPublicNode child{};
	child.id.value = 2U;
	child.parent_id.value = root.id.value;
	{
		MultiwayExternalPublicCatalog catalog(config(directory.path()), 8U);
		catalog.append_batch(1U, 0x44U, { root, child });
		EXPECT_EQ(catalog.record_count(), 2U);
		EXPECT_TRUE(catalog.contains(child.id));
		std::vector<MultiwayPackedPublicNode> chain;
		EXPECT_TRUE(catalog.reconstruct_chain(child.id, chain));
		EXPECT_EQ(chain.size(), 2U);
		EXPECT_EQ(chain[0].id.value, root.id.value);
		EXPECT_EQ(chain[1].id.value, child.id.value);
		std::size_t empty_menu_count = 99U;
		EXPECT_TRUE(catalog.lookup_menu(child.id, nullptr, 0U, empty_menu_count));
		EXPECT_EQ(empty_menu_count, 0U);
		bool duplicate_rejected = false;
		try { catalog.append_batch(2U, 0x45U, { child }); }
		catch (const std::invalid_argument&) { duplicate_rejected = true; }
		EXPECT_TRUE(duplicate_rejected);
	}
	MultiwayExternalPublicCatalog reopened(config(directory.path()), 8U);
	MultiwayPackedPublicNode found{};
	EXPECT_TRUE(reopened.lookup(child.id, found));
	EXPECT_EQ(found.parent_id.value, root.id.value);
}

TEST_CASE(multiway_external_public_catalog_persists_action_menus)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayPackedPublicNode root{};
	root.id.value = 11U;
	root.legal_menu_handle = 4U;
	MultiwayPublicStateDescriptor descriptor{};
	descriptor.id = root.id;
	descriptor.legal_actions = {
		{ texas::games::multiway::MultiwayAction::Check, 0U, 100, 0xabcU },
		{ texas::games::multiway::MultiwayAction::Bet, 1U, 300, 0xabcU } };
	{
		MultiwayExternalPublicCatalog catalog(config(directory.path()), 8U);
		catalog.append_batch(1U, 0x55U, { root }, { descriptor });
		std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
		std::size_t action_count = 0U;
		EXPECT_TRUE(catalog.lookup_menu(root.id, actions.data(), actions.size(), action_count));
		EXPECT_EQ(action_count, descriptor.legal_actions.size());
		for (std::size_t index = 0U; index < action_count; ++index)
			EXPECT_EQ(actions[index], descriptor.legal_actions[index]);
	}
	MultiwayExternalPublicCatalog reopened(config(directory.path()), 8U);
	std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
	std::size_t action_count = 0U;
	EXPECT_TRUE(reopened.lookup_menu(root.id, actions.data(), actions.size(), action_count));
	EXPECT_EQ(action_count, 2U);
	EXPECT_EQ(actions[0].action_menu_id, 0xabcU);
	MultiwayPublicStateDescriptor reconstructed{};
	EXPECT_TRUE(reopened.reconstruct(root.id, reconstructed));
	EXPECT_EQ(reconstructed.id.value, descriptor.id.value);
	EXPECT_EQ(reconstructed.legal_actions.size(), descriptor.legal_actions.size());
	for (std::size_t index = 0U; index < descriptor.legal_actions.size(); ++index)
		EXPECT_EQ(reconstructed.legal_actions[index], descriptor.legal_actions[index]);
	EXPECT_EQ(external_public_catalog_record_size(), external_public_node_record_size() + 156U);
}

TEST_CASE(multiway_external_row_index_preserves_full_ids_and_deterministic_page_ranges)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	std::uint64_t next_page_id = 1U;
	{
		MultiwayExternalRowIndex rows(config(directory.path()), 16U);
		rows.append_batch(1U, 0x77U, {
			{ { { 10U }, 0 }, 17U, 3U },
			{ { { 10U }, 1 }, 17U, 2U },
			{ { { 20U }, 2 }, 16U, 1U },
		}, next_page_id);
		EXPECT_EQ(next_page_id, 6U);
		MultiwayExternalRowMetadata metadata{};
		EXPECT_TRUE(rows.lookup({ 10U }, 1, metadata));
		EXPECT_EQ(metadata.bucket_count, 17U);
		EXPECT_EQ(metadata.action_count, 2U);
		EXPECT_EQ(metadata.first_page_id, 3U);
		EXPECT_EQ(metadata.page_count, 2U);
		MultiwayExternalRowPage page{};
		EXPECT_TRUE(rows.locate_bucket({ 10U }, 1, 16U, page));
		EXPECT_EQ(page.page_id, 4U);
		EXPECT_EQ(page.page_index, 1U);
		EXPECT_EQ(page.bucket_count, 1U);
		EXPECT_EQ(page.action_count, 2U);
		EXPECT_TRUE(!rows.locate_bucket({ 10U }, 1, 17U, page));
		EXPECT_TRUE(!rows.lookup({ 10U }, 5, metadata));
		std::uint64_t unchanged_page_id = 6U;
		bool invalid_rejected = false;
		try
		{
			rows.append_batch(2U, 0x78U, {
				{ { { 30U }, 0 }, 1U, 1U },
				{ { { 30U }, 6 }, 1U, 1U },
			}, unchanged_page_id);
		}
		catch (const std::invalid_argument&) { invalid_rejected = true; }
		EXPECT_TRUE(invalid_rejected);
		EXPECT_EQ(unchanged_page_id, 6U);
		rows.append_batch(2U, 0x79U, {
			{ { { 30U }, 0 }, 1U, 1U },
			{ { { 25U }, 1 }, 1U, 1U },
		}, next_page_id);
		EXPECT_EQ(next_page_id, 8U);
	}
	MultiwayExternalRowIndex reopened(config(directory.path()), 16U);
	EXPECT_EQ(reopened.next_page_id(), 8U);
	MultiwayExternalRowMetadata metadata{};
	EXPECT_TRUE(reopened.lookup({ 20U }, 2, metadata));
	EXPECT_EQ(metadata.first_page_id, 5U);
	EXPECT_EQ(metadata.page_count, 1U);
	EXPECT_TRUE(reopened.lookup({ 25U }, 1, metadata));
	EXPECT_EQ(metadata.first_page_id, 6U);
	EXPECT_TRUE(reopened.lookup({ 30U }, 0, metadata));
	EXPECT_EQ(metadata.first_page_id, 7U);
}

TEST_CASE(multiway_external_compact_pages_commit_reopen_and_rollback)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path());
	page_config.cache_bytes = 10000U;
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	{
		MultiwayExternalCompactPages pages(page_config, 16U, page_config.cache_bytes);
		EXPECT_EQ(pages.page_capacity(), 1U);
		EXPECT_TRUE(pages.memory_bytes() <= page_config.cache_bytes);
		pages.begin_batch(1U, 0xabcdefU);
		auto view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		EXPECT_EQ(view.regrets[2U * view.action_stride + 7U], 0);
		EXPECT_EQ(view.strategy_masses[1U * view.action_stride + 5U], 0U);
		pages.mark_dirty(1U);
		view.regrets[2U * view.action_stride + 7U] = -12345;
		view.strategy_masses[1U * view.action_stride + 5U] = 0xfedcba9876543210ULL;
		pages.unpin(1U);
		pages.commit_batch();
	}
	{
		MultiwayExternalCompactPages pages(page_config, 16U, page_config.cache_bytes);
		auto view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		EXPECT_EQ(view.regrets[2U * view.action_stride + 7U], -12345);
		EXPECT_EQ(view.strategy_masses[1U * view.action_stride + 5U], 0xfedcba9876543210ULL);
		pages.unpin(1U);
		pages.begin_batch(2U, 0x123456U);
		view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		pages.mark_dirty(1U);
		view.regrets[2U * view.action_stride + 7U] = 999;
		pages.unpin(1U);
		pages.abort_batch();
		view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		EXPECT_EQ(view.regrets[2U * view.action_stride + 7U], -12345);
		pages.unpin(1U);
		const auto stats = pages.stats();
		EXPECT_EQ(stats.page_cache_misses, 1U);
		EXPECT_EQ(stats.page_cache_hits, 2U);
	}
}

TEST_CASE(multiway_external_compact_pages_match_reference_cell_encoding)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path());
	page_config.cache_bytes = 10000U;
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const MultiwaySparseRowShape shape{ { { 42U }, 3 }, 16U, 4U };
	MultiwayCompactStorage reference(1U, 64U, 16U);
	reference.admit_row(shape);
	MultiwayExternalCompactPages external(page_config, 16U, page_config.cache_bytes);
	external.begin_batch(1U, 0xabcU);
	auto page = external.pin(1U, { 42U }, 3, 0U, 16U, 4U);
	const auto* row = reference.metadata(shape.infoset);
	const std::array<std::array<double, 2U>, 4U> deltas = {{
		{{ -1.25, 0.5 }}, {{ 2.0, 3.25 }}, {{ -0.75, 1.0 }}, {{ 4.5, 2.0 }} }};
	for (std::uint8_t action = 0U; action < shape.action_count; ++action)
	{
		reference.apply_delta(*row, 7U, action, deltas[action][0], deltas[action][1]);
		const auto cell = static_cast<std::size_t>(action) * page.action_stride + 7U;
		page.regrets[cell] = static_cast<std::int32_t>(page.regrets[cell] + deltas[action][0] * 1024.0);
		page.strategy_masses[cell] += static_cast<std::uint64_t>(deltas[action][1] * MultiwayCompactStorage::kMassScale);
	}
	external.mark_dirty(1U);
	external.unpin(1U);
	external.commit_batch();
	const auto reference_page = reference.checkpoint_page(0U);
	auto persisted = external.pin(1U, { 42U }, 3, 0U, 16U, 4U);
	for (std::uint8_t action = 0U; action < shape.action_count; ++action)
	{
		const auto cell = static_cast<std::size_t>(action) * persisted.action_stride + 7U;
		EXPECT_EQ(persisted.regrets[cell], reference_page.regrets[cell]);
		EXPECT_EQ(persisted.strategy_masses[cell], reference_page.strategy_masses[cell]);
	}
	external.unpin(1U);
}

TEST_CASE(multiway_external_compact_storage_matches_reference_batch_arithmetic)
{
	using namespace texas::solver::multiway;
	static_assert(!std::is_copy_constructible<MultiwayExternalCompactStorage::PinnedBucket>::value,
		"pinned external bucket handles must not duplicate cache pins");
	TestDirectory directory;
	auto page_config = config(directory.path() / "pages");
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const auto row_config = config(directory.path() / "rows");
	const MultiwaySparseRowShape shape{ { { 42U }, 3 }, 17U, 3U };
	MultiwayCompactStorage reference(1U, 51U, 16U);
	reference.admit_row(shape);
	MultiwayExternalCompactStorage external(row_config, page_config, 16U, 16000U);
	external.admit_rows(1U, 0xabcU, { shape });
	MultiwayExternalRowBinding binding{};
	EXPECT_TRUE(external.lookup_row(shape.infoset, binding));
	external.begin_batch(1U, 0xabcU);
	const std::array<MultiwayWorkerDelta, 6U> deltas = {{
		{ shape.infoset, 0U, 0U, -2.5, 1.25, 0U },
		{ shape.infoset, 0U, 1U, 3.0, 4.5, 1U },
		{ shape.infoset, 15U, 1U, -0.75, 2.0, 2U },
		{ shape.infoset, 16U, 2U, 1'900'000.0, 1'099'511'627'776.0, 3U },
		{ shape.infoset, 16U, 2U, 200'000.0, 1'099'511'627'776.0, 4U },
		{ shape.infoset, 16U, 0U, -2'500'000.0, 0.0, 5U },
	}};
	std::size_t delta_index = 0U;
	while (delta_index < deltas.size())
	{
		auto pinned = external.pin_bucket(binding, deltas[delta_index].bucket, true);
		const auto bucket = deltas[delta_index].bucket;
		while (delta_index < deltas.size() && deltas[delta_index].bucket == bucket)
		{
			const auto& delta = deltas[delta_index++];
			reference.apply_delta(delta.infoset, delta.bucket, delta.action, delta.regret, delta.strategy_sum);
			external.apply_delta(pinned, delta.action, delta.regret, delta.strategy_sum);
		}
		external.unpin_bucket(pinned);
	}
	external.commit_batch();
	for (const auto bucket : { 0U, 15U, 16U })
	{
		std::array<double, 3U> expected_regret{};
		std::array<double, 3U> actual_regret{};
		std::array<double, 3U> expected_average{};
		std::array<double, 3U> actual_average{};
		reference.regret_matched_strategy_into(shape.infoset, bucket,
			expected_regret.data(), expected_regret.size());
		reference.average_strategy_into(shape.infoset, bucket,
			expected_average.data(), expected_average.size());
		external.regret_matched_strategy_into(binding, bucket,
			actual_regret.data(), actual_regret.size());
		external.average_strategy_into(binding, bucket,
			actual_average.data(), actual_average.size());
		for (std::size_t action = 0U; action < expected_regret.size(); ++action)
		{
			EXPECT_EQ(actual_regret[action], expected_regret[action]);
			EXPECT_EQ(actual_average[action], expected_average[action]);
		}
	}
	EXPECT_EQ(external.action_below_regret(binding, 16U, 2U, 0.0),
		reference.action_below_regret(shape.infoset, 16U, 2U, 0.0));
	external.compact();
	MultiwayExternalCompactStorage reopened(row_config, page_config, 16U, 16000U);
	MultiwayExternalRowBinding reopened_binding{};
	EXPECT_TRUE(reopened.lookup_row(shape.infoset, reopened_binding));
	std::array<double, 3U> reopened_policy{};
	reopened.regret_matched_strategy_into(reopened_binding, 16U,
		reopened_policy.data(), reopened_policy.size());
	std::array<double, 3U> expected_policy{};
	reference.regret_matched_strategy_into(shape.infoset, 16U,
		expected_policy.data(), expected_policy.size());
	for (std::size_t action = 0U; action < expected_policy.size(); ++action)
		EXPECT_EQ(reopened_policy[action], expected_policy[action]);
	auto reopened_pinned = reopened.pin_bucket(reopened_binding, 16U, false);
	std::array<double, 3U> pinned_policy{};
	reopened.regret_matched_strategy_into(reopened_pinned,
		pinned_policy.data(), pinned_policy.size());
	for (std::size_t action = 0U; action < expected_policy.size(); ++action)
		EXPECT_EQ(pinned_policy[action], expected_policy[action]);
	EXPECT_EQ(reopened.action_below_regret(reopened_pinned, 2U, 0.0),
		reference.action_below_regret(shape.infoset, 16U, 2U, 0.0));
	bool read_only_update_rejected = false;
	try
	{
		reopened.apply_delta(reopened_pinned, 2U, 1.0, 0.0);
	}
	catch (const std::invalid_argument&) { read_only_update_rejected = true; }
	EXPECT_TRUE(read_only_update_rejected);
	reopened.unpin_bucket(reopened_pinned);
	bool foreign_binding_rejected = false;
	try
	{
		external.regret_matched_strategy_into(reopened_binding, 16U,
			reopened_policy.data(), reopened_policy.size());
	}
	catch (const std::invalid_argument&) { foreign_binding_rejected = true; }
	EXPECT_TRUE(foreign_binding_rejected);
}

TEST_CASE(multiway_external_record_store_compacts_deterministically)
{
	using namespace texas::solver::multiway;
	TestDirectory left_directory;
	TestDirectory right_directory;
	for (const auto& path : { left_directory.path(), right_directory.path() })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 1U, records(1U));
		store.append_batch(2U, 2U, records(8U));
		store.compact();
		EXPECT_EQ(store.record_count(), 4U);
		EXPECT_EQ(store.generation(), 3U);
	}
	const auto left = std::filesystem::file_size(left_directory.path() / "run_3.dat");
	const auto right = std::filesystem::file_size(right_directory.path() / "run_3.dat");
	EXPECT_EQ(left, right);
	std::ifstream left_data(left_directory.path() / "run_3.dat", std::ios::binary);
	std::ifstream right_data(right_directory.path() / "run_3.dat", std::ios::binary);
	std::array<char, 128U> left_bytes{};
	std::array<char, 128U> right_bytes{};
	left_data.read(left_bytes.data(), left_bytes.size());
	right_data.read(right_bytes.data(), right_bytes.size());
	EXPECT_EQ(left_data.gcount(), right_data.gcount());
	EXPECT_TRUE(std::equal(left_bytes.begin(), left_bytes.begin() + left_data.gcount(), right_bytes.begin()));
}

TEST_CASE(multiway_external_compaction_can_retain_previous_runs_until_commit)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		store.append_batch(1U, 1U, records(1U));
		store.append_batch(2U, 2U, records(8U));
		store.compact(true);
		EXPECT_EQ(store.generation(), 3U);
		EXPECT_TRUE(std::filesystem::exists(directory.path() / "run_1.dat"));
		EXPECT_TRUE(std::filesystem::exists(directory.path() / "run_2.dat"));
		EXPECT_TRUE(std::filesystem::exists(directory.path() / "run_3.dat"));
	}
	{
		MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
		EXPECT_EQ(reopened.record_count(), 4U);
		EXPECT_TRUE(!std::filesystem::exists(directory.path() / "run_1.dat"));
		EXPECT_TRUE(!std::filesystem::exists(directory.path() / "run_2.dat"));
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(reopened.lookup(10U, payload));
	}
}

TEST_CASE(multiway_external_record_store_compaction_keeps_newest_page_version)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 4U);
	store.append_batch(1U, 1U, records(10U));
	store.append_batch(2U, 2U, { { 10U, { 0xeeU, 0U, 0U, 0U } }, { 30U, { 0x30U, 0U, 0U, 0U } } });
	EXPECT_EQ(store.record_count(), 4U);
	store.compact();
	EXPECT_EQ(store.record_count(), 3U);
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup(10U, payload));
	EXPECT_EQ(payload[0], 0xeeU);
	MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
	EXPECT_TRUE(reopened.lookup(10U, payload));
	EXPECT_EQ(payload[0], 0xeeU);
}

TEST_CASE(multiway_external_page_cache_pins_bounds_commits_and_rolls_back_pages)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 16U);
	MultiwayExternalPageCache cache(store, 8192U);
	EXPECT_TRUE(cache.page_capacity() > 0U);
	cache.begin_batch(1U, 77U);
	auto page = cache.pin(101U);
	EXPECT_EQ(page.size, 16U);
	cache.mark_dirty(page.key);
	page.bytes[0] = 0x33U;
	page.bytes[15] = 0x44U;
	cache.unpin(page.key);
	EXPECT_EQ(cache.dirty_page_count(), 1U);
	cache.commit_batch();
	MultiwayExternalPageCache restored(store, 8192U);
	const auto restored_page = restored.pin(101U);
	EXPECT_EQ(restored_page.bytes[0], 0x33U);
	EXPECT_EQ(restored_page.bytes[15], 0x44U);
	restored.unpin(restored_page.key);

	cache.begin_batch(2U, 88U);
	page = cache.pin(101U);
	cache.mark_dirty(page.key);
	page.bytes[0] = 0x99U;
	cache.unpin(page.key);
	cache.abort_batch();
	page = cache.pin(101U);
	EXPECT_EQ(page.bytes[0], 0x33U);
	cache.unpin(page.key);
}

TEST_CASE(multiway_external_page_cache_does_not_evict_pinned_or_dirty_pages)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 16U);
	MultiwayExternalPageCache cache(store, 4096U);
	std::vector<MultiwayExternalPageCache::PageView> pinned;
	pinned.reserve(cache.page_capacity());
	for (std::size_t index = 0U; index < cache.page_capacity(); ++index)
		pinned.push_back(cache.pin(index + 1U));
	EXPECT_THROW(cache.pin(cache.page_capacity() + 1U), std::length_error);
	for (const auto page : pinned) cache.unpin(page.key);
	cache.begin_batch(1U, 1U);
	for (const auto page : pinned)
	{
		const auto dirty = cache.pin(page.key);
		cache.mark_dirty(dirty.key);
		cache.unpin(dirty.key);
	}
	EXPECT_THROW(cache.pin(cache.page_capacity() + 1U), std::length_error);
	cache.abort_batch();
	EXPECT_EQ(cache.pin(cache.page_capacity() + 1U).key, cache.page_capacity() + 1U);
}

TEST_CASE(multiway_external_record_store_rejects_invalid_batches_without_publication)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 4U);
	auto unsorted = records(4U);
	std::swap(unsorted[0], unsorted[1]);
	EXPECT_THROW(store.append_batch(1U, 0U, unsorted), std::invalid_argument);
	EXPECT_EQ(store.record_count(), 0U);
	EXPECT_TRUE(!std::filesystem::exists(directory.path() / "CURRENT"));
	const std::vector<MultiwayExternalRecord> wrong_width = { { 7U, { 1U, 2U } } };
	EXPECT_THROW(store.append_batch(1U, 0U, wrong_width), std::invalid_argument);
	EXPECT_EQ(store.record_count(), 0U);
}

TEST_CASE(multiway_external_record_store_discards_a_partial_uncommitted_wal)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		std::ofstream wal(directory.path() / "BATCH.wal", std::ios::binary);
		wal << "partial";
	}
	MultiwayExternalRecordStore recovered(config(directory.path()), 4U);
	EXPECT_EQ(recovered.record_count(), 0U);
	EXPECT_TRUE(!std::filesystem::exists(directory.path() / "BATCH.wal"));
}

TEST_CASE(multiway_external_record_store_replays_a_committed_wal_after_segment_failure)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		std::ofstream collision(directory.path() / "run_1.dat", std::ios::binary);
		collision << "reserved test collision";
		collision.close();
		EXPECT_THROW(store.append_batch(1U, 99U, records(30U)), std::runtime_error);
		EXPECT_EQ(store.record_count(), 0U);
	}
	{
		MultiwayExternalRecordStore recovered(config(directory.path()), 4U);
		EXPECT_EQ(recovered.record_count(), 2U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(recovered.lookup(32U, payload));
		EXPECT_EQ(payload[0], 32U);
	}
}
