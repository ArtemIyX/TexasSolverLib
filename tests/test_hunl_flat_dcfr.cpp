#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_bucket_map.hpp"
#include "test_harness.hpp"
#include "util/abstraction.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

std::uint32_t crc32_byte(std::uint32_t crc, std::uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1U) ? (0xEDB88320U ^ (crc >> 1U)) : (crc >> 1U);
    }
    return static_cast<std::uint16_t>(crc);
}

std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (auto b : data) {
        crc = crc32_byte(crc, b);
    }
    return ~crc;
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

std::vector<std::uint8_t> make_npy_u8(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out;
    const std::string header_dict =
        "{'descr': '|u1', 'fortran_order': False, 'shape': (" + std::to_string(data.size()) + ",), }";
    std::string header = header_dict;
    while ((10 + header.size() + 1) % 16 != 0) {
        header.push_back(' ');
    }
    header.push_back('\n');

    out.insert(out.end(), {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0});
    append_u16(out, static_cast<std::uint16_t>(header.size()));
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<std::uint8_t> make_json_bytes(const std::string& json) {
    return std::vector<std::uint8_t>(json.begin(), json.end());
}

struct ZipBuilder {
    struct Entry {
        std::string name;
        std::vector<std::uint8_t> data;
        std::uint32_t crc = 0;
        std::uint32_t local_offset = 0;
    };

    std::vector<Entry> entries;

    void add(const std::string& name, const std::vector<std::uint8_t>& data) {
        entries.push_back(Entry{name, data, crc32(data), 0});
    }

    std::vector<std::uint8_t> finish() {
        std::vector<std::uint8_t> out;
        std::vector<std::uint8_t> central;
        for (auto& entry : entries) {
            entry.local_offset = static_cast<std::uint32_t>(out.size());
            const auto name_len = static_cast<std::uint16_t>(entry.name.size());
            append_u32(out, 0x04034b50U);
            append_u16(out, 20);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u32(out, entry.crc);
            append_u32(out, static_cast<std::uint32_t>(entry.data.size()));
            append_u32(out, static_cast<std::uint32_t>(entry.data.size()));
            append_u16(out, name_len);
            append_u16(out, 0);
            out.insert(out.end(), entry.name.begin(), entry.name.end());
            out.insert(out.end(), entry.data.begin(), entry.data.end());

            append_u32(central, 0x02014b50U);
            append_u16(central, 20);
            append_u16(central, 20);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u32(central, entry.crc);
            append_u32(central, static_cast<std::uint32_t>(entry.data.size()));
            append_u32(central, static_cast<std::uint32_t>(entry.data.size()));
            append_u16(central, name_len);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u32(central, 0);
            append_u32(central, entry.local_offset);
            central.insert(central.end(), entry.name.begin(), entry.name.end());
        }

        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        out.insert(out.end(), central.begin(), central.end());
        append_u32(out, 0x06054b50U);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, static_cast<std::uint16_t>(entries.size()));
        append_u16(out, static_cast<std::uint16_t>(entries.size()));
        append_u32(out, static_cast<std::uint32_t>(central.size()));
        append_u32(out, cd_offset);
        append_u16(out, 0);
        return out;
    }
};

std::vector<std::array<std::uint8_t, 2>> enumerate_live_hands(const std::vector<std::uint8_t>& board) {
    std::array<bool, 64> blocked = {};
    for (const auto card : board) {
        blocked[card] = true;
    }

    std::vector<std::uint8_t> live_cards;
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            const auto card = core::card_to_int(rank, suit);
            if (!blocked[card]) {
                live_cards.push_back(card);
            }
        }
    }

    std::vector<std::array<std::uint8_t, 2>> hands;
    for (std::size_t i = 0; i < live_cards.size(); ++i) {
        for (std::size_t j = i + 1; j < live_cards.size(); ++j) {
            hands.push_back({live_cards[i], live_cards[j]});
        }
    }
    return hands;
}

std::filesystem::path make_tmp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

std::filesystem::path write_two_bucket_river_abstraction(const std::vector<std::uint8_t>& board) {
    const auto tmp = make_tmp_path("texas_hunl_flat_dcfr_bucket_ranges.npz");
    const auto board_key = core::canonicalize_board(board);

    std::string river_hand_index = "{\"" + board_key + "\":{";
    std::vector<std::uint8_t> river_assignments;
    const auto live_hands = enumerate_live_hands(board);
    river_assignments.reserve(live_hands.size());
    for (std::size_t i = 0; i < live_hands.size(); ++i) {
        const auto [canonical_board_key, hand_key] = core::canonicalize(board, live_hands[i]);
        (void)canonical_board_key;
        if (i > 0) {
            river_hand_index += ",";
        }
        river_hand_index += "\"" + hand_key + "\":" + std::to_string(i);
        river_assignments.push_back(static_cast<std::uint8_t>(i % 2));
    }
    river_hand_index += "}}";

    const std::string board_index = "{\"" + board_key + "\":0}";
    const std::string metadata =
        "{\"bucket_counts\":[1,1,2],\"feature_bins\":1,\"schema_version\":1,\"seed\":7,\"version\":\"v1\"}";

    ZipBuilder zip;
    auto add_named = [&](const std::string& name, const std::vector<std::uint8_t>& raw) {
        zip.add(name, make_npy_u8(raw));
    };

    add_named("flop_assignments.npy", {0});
    add_named("turn_assignments.npy", {0});
    add_named("river_assignments.npy", river_assignments);
    add_named("flop_board_index.npy", make_json_bytes("{}"));
    add_named("turn_board_index.npy", make_json_bytes("{}"));
    add_named("river_board_index.npy", make_json_bytes(board_index));
    add_named("flop_hand_index.npy", make_json_bytes("{}"));
    add_named("turn_hand_index.npy", make_json_bytes("{}"));
    add_named("river_hand_index.npy", make_json_bytes(river_hand_index));
    add_named("metadata.npy", make_json_bytes(metadata));

    const auto bytes = zip.finish();
    std::ofstream out(tmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return tmp;
}

}  // namespace

TEST_CASE(hunl_flat_dcfr_runs_explicit_stage_iteration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 3},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iterations(2);

    EXPECT_EQ(solver.iterations(), 2U);
    EXPECT_TRUE(solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().reach_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().terminal_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().backward_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().regret_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().average_strategy_seconds >= 0.0);
}

TEST_CASE(hunl_flat_dcfr_keeps_configured_worker_pool_across_iterations) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 3},
        core::HUNLFlatValueLayout::InfosetActionHand,
        3);

    EXPECT_EQ(solver.worker_count(), 3U);
    solver.run_iteration();
    solver.run_iteration();

    EXPECT_EQ(solver.worker_count(), 3U);
    EXPECT_EQ(solver.iterations(), 2U);
}

TEST_CASE(hunl_flat_dcfr_accepts_single_worker_pool_configuration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand,
        1);

    EXPECT_EQ(solver.worker_count(), 1U);
    solver.run_iteration();
    EXPECT_EQ(solver.iterations(), 1U);
}

TEST_CASE(hunl_flat_dcfr_strategy_stage_writes_normalized_rows) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    for (const auto& meta : table.meta()) {
        auto* regret = table.regret_mut(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            regret[i] = 0.0;
        }
        if (meta.action_count >= 2 && meta.hand_count >= 1) {
            regret[0] = 2.0;
            regret[meta.hand_count] = 1.0;
        }
    }

    solver.run_iteration();

    for (const auto& meta : table.meta()) {
        const auto* strategy = table.current_strategy(meta.id);
        for (std::size_t h = 0; h < meta.hand_count; ++h) {
            double sum = 0.0;
            for (std::size_t a = 0; a < meta.action_count; ++a) {
                const auto idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                EXPECT_TRUE(strategy[idx] >= 0.0);
                sum += strategy[idx];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_dcfr_exports_average_strategy_by_infoset_key) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();
    const auto exported = solver.export_average_strategy();

    EXPECT_EQ(exported.size(), graph.infosets.size());
    for (const auto& infoset : graph.infosets) {
        const auto it = exported.find(infoset.key);
        EXPECT_TRUE(it != exported.end());
        EXPECT_EQ(it->second.size(),
                  static_cast<std::size_t>(infoset.action_count) *
                      solver.infoset_table().meta()[infoset.id.value].hand_count);
    }
}

TEST_CASE(hunl_flat_dcfr_forward_reach_initializes_root_reaches) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    EXPECT_NEAR(solver.player0_reach()[graph.root], 1.0, 1e-12);
    EXPECT_NEAR(solver.player1_reach()[graph.root], 1.0, 1e-12);
    EXPECT_NEAR(solver.chance_reach()[graph.root], 1.0, 1e-12);
}

TEST_CASE(hunl_flat_dcfr_forward_reach_distributes_mass_across_explicit_rows) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {4, 4},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    const auto root_infoset = graph.node_meta[graph.root].infoset_id;
    const auto bucket_range = solver.infoset_table().infoset_bucket_range(root_infoset);
    double total_bucket_mass = 0.0;
    for (std::uint32_t idx = bucket_range.begin; idx < bucket_range.end; ++idx) {
        EXPECT_TRUE(solver.bucket_reach()[idx] > 0.0);
        total_bucket_mass += solver.bucket_reach()[idx];
    }
    EXPECT_NEAR(total_bucket_mass, 1.0, 1e-12);
}

TEST_CASE(hunl_flat_dcfr_forward_reach_propagates_strategy_on_decision_nodes) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    const auto root_idx = graph.root;
    const auto& root_meta = graph.node_meta[root_idx];
    EXPECT_EQ(root_meta.type, core::HUNLFlatNodeType::Decision);
    EXPECT_TRUE(root_meta.child_count >= 2);

    auto& table = solver.infoset_table_mut();
    const auto infoset_id = root_meta.infoset_id;
    auto* regret = table.regret_mut(infoset_id);
    for (std::size_t i = 0; i < table.row_value_count(infoset_id); ++i) {
        regret[i] = 0.0;
    }
    const auto hand_count = table.meta()[infoset_id.value].hand_count;
    regret[0] = 3.0;
    regret[hand_count] = 1.0;

    solver.run_iteration();

    const auto child0 = graph.children[root_meta.child_begin];
    const auto child1 = graph.children[root_meta.child_begin + 1];
    if (root_meta.player == 0) {
        EXPECT_TRUE(solver.player0_reach()[child0] > solver.player0_reach()[child1]);
        EXPECT_NEAR(solver.player1_reach()[child0], 1.0, 1e-12);
        EXPECT_NEAR(solver.player1_reach()[child1], 1.0, 1e-12);
    } else {
        EXPECT_TRUE(solver.player1_reach()[child0] > solver.player1_reach()[child1]);
        EXPECT_NEAR(solver.player0_reach()[child0], 1.0, 1e-12);
        EXPECT_NEAR(solver.player0_reach()[child1], 1.0, 1e-12);
    }
    EXPECT_NEAR(solver.chance_reach()[child0], 1.0, 1e-12);
    EXPECT_NEAR(solver.chance_reach()[child1], 1.0, 1e-12);
}

TEST_CASE(hunl_flat_dcfr_forward_reach_weights_chance_nodes) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::benchmark_turn_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool checked_chance = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type != core::HUNLFlatNodeType::Chance || meta.chance_count == 0) {
            continue;
        }

        const auto& outcome = graph.chance_outcomes[meta.chance_begin];
        const auto parent_chance = solver.chance_reach()[node_idx];
        const auto child_chance = solver.chance_reach()[outcome.child];
        if (parent_chance > 0.0) {
            EXPECT_TRUE(child_chance > 0.0);
            EXPECT_TRUE(child_chance <= parent_chance);
            checked_chance = true;
            break;
        }
    }

    EXPECT_TRUE(checked_chance);
}

TEST_CASE(hunl_flat_dcfr_terminal_stage_uses_precomputed_leaf_utilities) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool saw_terminal = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type != core::HUNLFlatNodeType::TerminalFold &&
            meta.type != core::HUNLFlatNodeType::TerminalShowdown) {
            continue;
        }

        saw_terminal = true;
        if (meta.type == core::HUNLFlatNodeType::TerminalFold) {
            EXPECT_TRUE(meta.terminal_kind.tag == core::TerminalKindTag::Fold);
        } else {
            EXPECT_TRUE(meta.terminal_kind.tag == core::TerminalKindTag::Showdown);
        }
        EXPECT_NEAR(meta.terminal_utility[0], solver.terminal_values()[node_idx], 1e-12);
    }

    EXPECT_TRUE(saw_terminal);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_copies_terminal_values_into_node_values) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool saw_terminal = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type != core::HUNLFlatNodeType::TerminalFold &&
            meta.type != core::HUNLFlatNodeType::TerminalShowdown) {
            continue;
        }
        saw_terminal = true;
        EXPECT_NEAR(solver.node_values()[node_idx], solver.terminal_values()[node_idx], 1e-12);
    }

    EXPECT_TRUE(saw_terminal);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_writes_action_values_from_children) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    bool checked_parent = false;
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.child_count == 0) {
            continue;
        }

        for (std::size_t i = 0; i < meta.child_count; ++i) {
            const auto child = graph.children[meta.child_begin + i];
            EXPECT_NEAR(
                solver.action_values()[meta.child_begin + i],
                solver.node_values()[child],
                1e-12);
        }
        checked_parent = true;
        break;
    }

    EXPECT_TRUE(checked_parent);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_computes_root_value_from_children) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    const auto& root_meta = graph.node_meta[graph.root];
    const auto infoset_id = root_meta.infoset_id;
    auto* regret = table.regret_mut(infoset_id);
    for (std::size_t i = 0; i < table.row_value_count(infoset_id); ++i) {
        regret[i] = 0.0;
    }
    if (root_meta.child_count >= 2) {
        const auto hand_count = table.meta()[infoset_id.value].hand_count;
        regret[0] = 4.0;
        regret[hand_count] = 1.0;
    }

    solver.run_iteration();

    if (root_meta.type == core::HUNLFlatNodeType::Decision && root_meta.child_count >= 2) {
        const auto a0 = solver.action_values()[root_meta.child_begin];
        const auto a1 = solver.action_values()[root_meta.child_begin + 1];
        EXPECT_TRUE(solver.node_values()[graph.root] <= std::max(a0, a1) + 1e-12);
        EXPECT_TRUE(solver.node_values()[graph.root] >= std::min(a0, a1) - 1e-12);
    } else {
        EXPECT_TRUE(root_meta.child_count > 0);
    }
}

TEST_CASE(hunl_flat_dcfr_regret_update_uses_action_minus_node_value) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    const auto root_infoset = graph.node_meta[graph.root].infoset_id;
    auto* regret_before = table.regret_mut(root_infoset);
    const auto before0 = regret_before[0];
    const auto action_count = table.meta()[root_infoset.value].action_count;
    const auto hand_count = table.meta()[root_infoset.value].hand_count;
    const auto before1 = action_count >= 2 ? regret_before[hand_count] : before0;

    solver.run_iteration();

    const auto* regret_after = table.regret(root_infoset);
    if (action_count >= 2) {
        EXPECT_TRUE(regret_after[0] != before0 || regret_after[hand_count] != before1);
    } else {
        EXPECT_TRUE(regret_after[0] != before0);
    }
}

TEST_CASE(hunl_flat_dcfr_average_strategy_update_is_reach_weighted) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();

    const auto root_infoset = graph.node_meta[graph.root].infoset_id;
    const auto* strategy = solver.infoset_table().current_strategy(root_infoset);
    const auto* strategy_sum = solver.infoset_table().strategy_sum(root_infoset);
    const auto action_count = solver.infoset_table().meta()[root_infoset.value].action_count;
    const auto hand_count = solver.infoset_table().meta()[root_infoset.value].hand_count;
    const auto own_reach = graph.node_meta[graph.root].player == 0
        ? solver.player0_reach()[graph.root] * solver.chance_reach()[graph.root]
        : solver.player1_reach()[graph.root] * solver.chance_reach()[graph.root];

    EXPECT_NEAR(strategy_sum[0], own_reach * strategy[0], 1e-12);
    if (action_count >= 2) {
        EXPECT_NEAR(strategy_sum[hand_count], own_reach * strategy[hand_count], 1e-12);
    }
}

TEST_CASE(hunl_flat_dcfr_discount_stage_updates_infoset_discount_iteration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iterations(2);

    for (const auto& meta : solver.infoset_table().meta()) {
        EXPECT_EQ(meta.last_discount_iter, 2U);
    }
}

TEST_CASE(hunl_flat_dcfr_produces_normalized_strategies_after_simd_passes) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::benchmark_turn_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetHandAction,
        2);

    solver.run_iterations(3);

    EXPECT_EQ(solver.iterations(), 3U);
    EXPECT_TRUE(solver.profile().discount_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().reach_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().terminal_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().backward_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().regret_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().average_strategy_seconds >= 0.0);

    for (const auto& meta : solver.infoset_table().meta()) {
        const auto* strategy = solver.infoset_table().current_strategy(meta.id);
        for (std::size_t h = 0; h < meta.hand_count; ++h) {
            double sum = 0.0;
            for (std::size_t a = 0; a < meta.action_count; ++a) {
                const auto idx = h * static_cast<std::size_t>(meta.action_count) + a;
                EXPECT_TRUE(strategy[idx] >= 0.0 || std::isnan(strategy[idx]));
                sum += strategy[idx];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_dcfr_matches_across_worker_counts_on_small_tree) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph_a = core::HUNLFlatSolveGraph::build(config);
    const auto graph_b = core::HUNLFlatSolveGraph::build(config);

    core::HUNLFlatDCFR single_worker(
        graph_a,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetHandAction,
        1);
    core::HUNLFlatDCFR two_workers(
        graph_b,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetHandAction,
        2);

    single_worker.run_iterations(2);
    two_workers.run_iterations(2);

    const auto exported_single = single_worker.export_average_strategy();
    const auto exported_parallel = two_workers.export_average_strategy();

    EXPECT_EQ(exported_single.size(), exported_parallel.size());
    for (const auto& [key, strategy] : exported_single) {
        const auto it = exported_parallel.find(key);
        EXPECT_TRUE(it != exported_parallel.end());
        EXPECT_EQ(strategy.size(), it->second.size());
        for (std::size_t i = 0; i < strategy.size(); ++i) {
            EXPECT_NEAR(strategy[i], it->second[i], 1e-12);
        }
    }

    EXPECT_TRUE(single_worker.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(two_workers.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(single_worker.profile().backward_seconds >= 0.0);
    EXPECT_TRUE(two_workers.profile().backward_seconds >= 0.0);
}

TEST_CASE(hunl_flat_dcfr_rejects_negative_range_weights_in_config_validation) {
    auto config = core::default_tiny_subgame();
    core::HUNLRangeInput range;
    range.hand_weights.push_back({{c(14, 1), c(13, 3)}, -0.5});
    config.player_ranges[0] = range;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(hunl_flat_bucket_map_applies_mixed_range_inputs_per_infoset_player) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto abstraction_path = write_two_bucket_river_abstraction(config->initial_board);
    const auto hole = (*config->initial_hole_cards)[0];

    auto bucket_map = core::HUNLFlatBucketMap::from_abstraction(
        graph,
        core::load_abstraction(abstraction_path));

    std::array<std::optional<core::HUNLRangeInput>, 2> player_ranges = {std::nullopt, std::nullopt};
    core::HUNLRangeInput mixed_range;
    mixed_range.hand_weights.push_back({hole, 3.0});
    mixed_range.bucket_weights.push_back({core::Street::River, 1U, 1.0});
    player_ranges[0] = mixed_range;

    bucket_map.apply_range_inputs(graph, player_ranges);

    bool checked_player_zero_infoset = false;
    for (const auto& infoset : graph.infosets) {
        if (infoset.player != 0 || infoset.street != core::Street::River) {
            continue;
        }
        const auto mapped_bucket = bucket_map.lookup_bucket(infoset.id, hole);
        const auto* weights = bucket_map.bucket_weights(infoset.id);
        EXPECT_TRUE(weights != nullptr);
        EXPECT_EQ(weights->size(), 2U);
        const auto expected_bucket0 = mapped_bucket == 0 ? 0.75 : 0.0;
        const auto expected_bucket1 = mapped_bucket == 1 ? 1.0 : 0.25;
        EXPECT_NEAR((*weights)[0], expected_bucket0, 1e-12);
        EXPECT_NEAR((*weights)[1], expected_bucket1, 1e-12);
        checked_player_zero_infoset = true;
        break;
    }

    EXPECT_TRUE(checked_player_zero_infoset);
    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_flat_bucket_map_applies_direct_bucket_weights) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto abstraction_path = write_two_bucket_river_abstraction(config->initial_board);

    auto bucket_map = core::HUNLFlatBucketMap::from_abstraction(
        graph,
        core::load_abstraction(abstraction_path));

    std::array<std::optional<core::HUNLRangeInput>, 2> player_ranges = {std::nullopt, std::nullopt};
    core::HUNLRangeInput bucket_only_range;
    bucket_only_range.bucket_weights.push_back({core::Street::River, 0U, 2.0});
    bucket_only_range.bucket_weights.push_back({core::Street::River, 1U, 6.0});
    player_ranges[1] = bucket_only_range;

    bucket_map.apply_range_inputs(graph, player_ranges);

    bool checked_player_one_infoset = false;
    for (const auto& infoset : graph.infosets) {
        if (infoset.player != 1 || infoset.street != core::Street::River) {
            continue;
        }
        const auto* weights = bucket_map.bucket_weights(infoset.id);
        EXPECT_TRUE(weights != nullptr);
        EXPECT_EQ(weights->size(), 2U);
        EXPECT_NEAR((*weights)[0], 0.25, 1e-12);
        EXPECT_NEAR((*weights)[1], 0.75, 1e-12);
        checked_player_one_infoset = true;
        break;
    }

    EXPECT_TRUE(checked_player_one_infoset);
    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_flat_bucket_map_range_inputs_ignore_non_matching_streets) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto abstraction_path = write_two_bucket_river_abstraction(config->initial_board);

    auto bucket_map = core::HUNLFlatBucketMap::from_abstraction(
        graph,
        core::load_abstraction(abstraction_path));

    std::array<std::optional<core::HUNLRangeInput>, 2> player_ranges = {std::nullopt, std::nullopt};
    core::HUNLRangeInput turn_only_range;
    turn_only_range.bucket_weights.push_back({core::Street::Turn, 0U, 5.0});
    turn_only_range.bucket_weights.push_back({core::Street::Turn, 1U, 1.0});
    player_ranges[0] = turn_only_range;

    bucket_map.apply_range_inputs(graph, player_ranges);

    bool checked_infoset = false;
    for (const auto& infoset : graph.infosets) {
        if (infoset.player != 0 || infoset.street != core::Street::River) {
            continue;
        }
        const auto* weights = bucket_map.bucket_weights(infoset.id);
        EXPECT_TRUE(weights != nullptr);
        EXPECT_NEAR((*weights)[0] + (*weights)[1], 0.0, 1e-12);
        checked_infoset = true;
        break;
    }

    EXPECT_TRUE(checked_infoset);
    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_flat_bucket_map_range_inputs_ignore_blocked_hands) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    const auto abstraction_path = write_two_bucket_river_abstraction(config->initial_board);

    auto bucket_map = core::HUNLFlatBucketMap::from_abstraction(
        graph,
        core::load_abstraction(abstraction_path));

    std::array<std::optional<core::HUNLRangeInput>, 2> player_ranges = {std::nullopt, std::nullopt};
    core::HUNLRangeInput blocked_hand_range;
    blocked_hand_range.hand_weights.push_back({{config->initial_board[0], c(9, 1)}, 5.0});
    player_ranges[0] = blocked_hand_range;

    bucket_map.apply_range_inputs(graph, player_ranges);

    bool checked_infoset = false;
    for (const auto& infoset : graph.infosets) {
        if (infoset.player != 0 || infoset.street != core::Street::River) {
            continue;
        }
        const auto* weights = bucket_map.bucket_weights(infoset.id);
        EXPECT_TRUE(weights != nullptr);
        EXPECT_NEAR((*weights)[0] + (*weights)[1], 0.0, 1e-12);
        checked_infoset = true;
        break;
    }

    EXPECT_TRUE(checked_infoset);
    std::filesystem::remove(abstraction_path);
}

TEST_CASE(hunl_flat_dcfr_backward_stage_uses_bucket_mass_in_node_values) {
    auto config = core::default_tiny_subgame();
    config.abstraction_path = write_two_bucket_river_abstraction(config.initial_board).string();
    config.flat_solve_mode = core::HUNLFlatSolveMode::Bucketed;

    const auto shared = std::make_shared<const core::HUNLConfig>(config);
    const auto graph_a = core::HUNLFlatSolveGraph::build(shared);
    const auto graph_b = core::HUNLFlatSolveGraph::build(shared);

    core::HUNLFlatDCFR solver_bucket0(
        graph_a,
        {2, 2},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetActionHand);
    core::HUNLFlatDCFR solver_bucket1(
        graph_b,
        {2, 2},
        core::HUNLFlatSolveMode::Bucketed,
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto set_root_preference = [](core::HUNLFlatDCFR& solver, std::size_t favored_bucket) {
        const auto& graph = solver.graph();
        const auto root_infoset = graph.node_meta[graph.root].infoset_id;
        auto& table = solver.infoset_table_mut();
        auto* regret = table.regret_mut(root_infoset);
        const auto& meta = table.meta()[root_infoset.value];
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            regret[i] = 0.0;
        }
        if (meta.action_count >= 2 && meta.bucket_count >= 2) {
            regret[favored_bucket] = 5.0;
            regret[meta.bucket_count + (1U - favored_bucket)] = 5.0;
        }
    };

    set_root_preference(solver_bucket0, 0);
    set_root_preference(solver_bucket1, 1);

    solver_bucket0.run_iteration();
    solver_bucket1.run_iteration();

    EXPECT_TRUE(std::abs(solver_bucket0.node_values()[graph_a.root] - solver_bucket1.node_values()[graph_b.root]) > 1e-12);
    std::filesystem::remove(config.abstraction_path.value());
}
