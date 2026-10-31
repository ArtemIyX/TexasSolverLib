#pragma once

/** @file
 * Request-local deadline, batch, and row-capacity budgeting for search.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway
{

	/** Deadline reserve and bounded batch, trajectory, and row limits. */
#pragma pack(push, 8)
	struct MultiwayResolverBudgetConfig
	{
		std::chrono::steady_clock::time_point external_deadline{};
		std::chrono::milliseconds deadline_reserve{};
		std::uint32_t max_batches = 0U;
		std::uint32_t trajectories_per_batch = 0U;
		std::size_t max_sparse_rows = 0U;
		std::size_t max_sparse_values = 0U;

		/** Checks deadline and capacity limits for a resolver request. */
		void validate() const;
	};
#pragma pack(pop)

	/** Reason a request-local budget checkpoint allows or stops further search. */
	enum class MultiwayResolverBudgetCheckpoint : std::uint8_t
	{
		Ready,
		DeadlineExpired,
		LimitReached,
		Cancelled,
	};

	// Request-local budget state. It is controlled by the resolver thread at
	// bounded batch checkpoints and does not own worker or solver state.
	/** Tracks bounded search progress and cancellation for one resolver request. */
	class MultiwayResolverBudget final
	{
	public:
		/** Creates a budget with a deadline derived from the external deadline. */
		explicit MultiwayResolverBudget(MultiwayResolverBudgetConfig config);

		/** Checks deadline and hard limits before starting a batch. */
		[[nodiscard]] MultiwayResolverBudgetCheckpoint checkpoint(
			std::uint32_t batch,
			std::uint64_t first_trajectory) noexcept;

		/** Accepts telemetry and row counts from one clean batch when within limits. */
		[[nodiscard]] bool accept_clean_batch(
			bool clean,
			std::uint64_t accepted_trajectories,
			std::uint64_t merged_delta_entries,
			std::size_t admitted_rows,
			std::size_t admitted_values) noexcept;

		/** Stops future batch admission. */
		void cancel() noexcept { cancelled_ = true; }

		/** Reports whether the internal deadline has passed. */
		[[nodiscard]] bool deadline_reached() const noexcept;
		[[nodiscard]] std::chrono::steady_clock::time_point deadline() const noexcept
		{
			return internal_deadline_;
		}
		/** Reports whether a prior checkpoint observed deadline expiry. */
		[[nodiscard]] bool deadline_expired() const noexcept { return deadline_expired_; }
		/** Reports whether cancellation was requested. */
		[[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
		/** Returns the number of accepted clean batches. */
		[[nodiscard]] std::uint64_t clean_batches() const noexcept { return clean_batches_; }

	private:
		std::chrono::steady_clock::time_point internal_deadline_{};
		std::uint32_t max_batches_ = 0U;
		std::uint64_t max_trajectories_ = 0U;
		std::size_t max_sparse_rows_ = 0U;
		std::size_t max_sparse_values_ = 0U;
		std::uint64_t clean_batches_ = 0U;
		bool cancelled_ = false;
		bool deadline_expired_ = false;
	};

} // namespace texas::solver::multiway
