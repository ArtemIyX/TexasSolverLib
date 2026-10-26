#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
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
