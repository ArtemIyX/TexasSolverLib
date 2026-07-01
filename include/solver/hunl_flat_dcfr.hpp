#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_bucket_map.hpp"
#include "solver/hunl_bucket_terminal.hpp"
#include "solver/hunl_flat_state.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace core {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
struct alignas(HUNL_CACHELINE_BYTES) HUNLFlatStageProfile {
    double discount_seconds = 0.0;
    double strategy_seconds = 0.0;
    double reach_seconds = 0.0;
    double terminal_seconds = 0.0;
    double backward_seconds = 0.0;
    double regret_seconds = 0.0;
    double average_strategy_seconds = 0.0;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

enum class HUNLFlatStageKind : std::uint8_t {
    Discount = 0,
    Strategy = 1,
    Reach = 2,
    Terminal = 3,
    Backward = 4,
    Regret = 5,
    AverageStrategy = 6,
};

struct HUNLFlatSchedulerDiagnostics {
    std::vector<HUNLFlatStageProfile> worker_profiles;
};

static_assert(alignof(HUNLFlatStageProfile) == HUNL_CACHELINE_BYTES,
              "HUNLFlatStageProfile should be cache-line aligned");

class HUNLFlatDCFR {
public:
    explicit HUNLFlatDCFR(
        HUNLFlatSolveGraph graph,
        std::array<std::size_t, 2> bucket_count_per_player,
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
        std::size_t workers = 1,
        double alpha = 1.5,
        double beta = 0.0,
        double gamma = 2.0);

    explicit HUNLFlatDCFR(
        HUNLFlatSolveGraph graph,
        std::array<std::size_t, 2> bucket_count_per_player,
        HUNLFlatSolveMode solve_mode,
        HUNLFlatValueLayout layout,
        std::size_t workers = 1,
        double alpha = 1.5,
        double beta = 0.0,
        double gamma = 2.0);

    ~HUNLFlatDCFR();

    HUNLFlatDCFR(const HUNLFlatDCFR&) = delete;
    HUNLFlatDCFR& operator=(const HUNLFlatDCFR&) = delete;
    HUNLFlatDCFR(HUNLFlatDCFR&&) = delete;
    HUNLFlatDCFR& operator=(HUNLFlatDCFR&&) = delete;

    void run_iteration();
    void run_iterations(std::uint32_t iterations);

    [[nodiscard]] const HUNLFlatSolveGraph& graph() const noexcept;
    [[nodiscard]] const HUNLFlatInfosetTable& infoset_table() const noexcept;
    [[nodiscard]] HUNLFlatInfosetTable& infoset_table_mut() noexcept;
    [[nodiscard]] const HUNLFlatStageProfile& profile() const noexcept;
    [[nodiscard]] std::uint32_t iterations() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& player0_reach() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& player1_reach() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& chance_reach() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& bucket_reach() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& normalized_bucket_reach() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& infoset_bucket_totals() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& terminal_values() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& node_values() const noexcept;
    [[nodiscard]] const HUNLAlignedVector<double>& action_values() const noexcept;
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] const HUNLFlatSchedulerDiagnostics& scheduler_diagnostics() const noexcept;
    [[nodiscard]] const HUNLFlatBucketMap* bucket_map() const noexcept;

    [[nodiscard]] std::unordered_map<std::string, std::vector<double>> export_average_strategy() const;

private:
    class WorkerPool {
    public:
        explicit WorkerPool(std::size_t worker_count);
        ~WorkerPool();

        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        void run_stage(const std::function<void(std::size_t)>& fn);
        [[nodiscard]] std::size_t worker_count() const noexcept;

    private:
        void worker_loop(std::size_t worker_index);

        std::vector<std::thread> threads_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::condition_variable finished_cv_;
        std::function<void(std::size_t)> stage_fn_;
        std::exception_ptr stage_error_;
        std::size_t generation_ = 0;
        std::size_t completed_workers_ = 0;
        bool stop_ = false;
    };

    void run_stage_workers(HUNLFlatStageKind stage, const std::function<void(std::size_t)>& fn);
    static void add_stage_seconds(HUNLFlatStageProfile& profile, HUNLFlatStageKind stage, double seconds);
    void apply_dcfr_discount_stage();
    void compute_strategy_stage();
    void forward_reach_stage();
    void normalize_bucket_reach_stage();
    void terminal_utility_stage();
    void backward_value_stage();
    void regret_update_stage();
    void average_strategy_stage();
    static std::optional<HUNLFlatBucketMap> load_bucket_map_for_graph(
        const HUNLFlatSolveGraph& graph,
        HUNLFlatSolveMode solve_mode);
    static std::optional<HUNLBucketTerminalTable> build_terminal_table_for_graph(
        const HUNLFlatSolveGraph& graph,
        const std::optional<HUNLFlatBucketMap>& bucket_map);

    HUNLFlatSolveGraph graph_;
    std::optional<HUNLFlatBucketMap> bucket_map_;
    std::optional<HUNLBucketTerminalTable> terminal_table_;
    HUNLFlatInfosetTable infoset_table_;
    HUNLAlignedVector<double> player0_reach_;
    HUNLAlignedVector<double> player1_reach_;
    HUNLAlignedVector<double> chance_reach_;
    HUNLAlignedVector<double> bucket_reach_;
    HUNLAlignedVector<double> normalized_bucket_reach_;
    HUNLAlignedVector<double> infoset_bucket_totals_;
    HUNLAlignedVector<double> terminal_values_;
    HUNLAlignedVector<double> node_values_;
    HUNLAlignedVector<double> action_values_;
    HUNLFlatStageProfile profile_;
    std::size_t worker_count_ = 1;
    HUNLFlatParallelPlan parallel_plan_;
    std::unique_ptr<WorkerPool> worker_pool_;
    std::vector<HUNLFlatWorkerScratch, AlignedAllocator<HUNLFlatWorkerScratch, HUNL_CACHELINE_BYTES>> worker_scratch_;
    HUNLFlatSchedulerDiagnostics scheduler_diagnostics_;
    double alpha_ = 1.5;
    double beta_ = 0.0;
    double gamma_ = 2.0;
    std::uint32_t iterations_ = 0;
};

}  // namespace core
