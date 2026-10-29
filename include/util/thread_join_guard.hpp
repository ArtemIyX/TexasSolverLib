#pragma once

/** @file
 * Scope guard that requests worker shutdown and joins owned threads on unwind.
 */

#include <thread>
#include <utility>
#include <vector>

namespace texas::util::detail
{

	/** Joins all supplied threads after invoking a caller-owned stop callback. */
	template <class StopFunction>
	class ThreadJoinGuard
	{
	public:
		/** Borrows threads and stores the function that requests their shutdown. */
		ThreadJoinGuard(
			std::vector<std::thread>& threads,
			StopFunction stop_function)
			: threads_(threads), stop_function_(std::move(stop_function))
		{
		}

		ThreadJoinGuard(const ThreadJoinGuard&) = delete;
		ThreadJoinGuard& operator=(const ThreadJoinGuard&) = delete;

		/** Stops workers and joins every joinable thread while the guard is active. */
		~ThreadJoinGuard() noexcept
		{
			if (!active_)
			{
				return;
			}
			try
			{
				stop_function_();
			}
			catch (...)
			{
				// Joining already-created workers remains mandatory during unwind.
			}
			for (auto& thread : threads_)
			{
				if (!thread.joinable())
				{
					continue;
				}
				try
				{
					thread.join();
				}
				catch (...)
				{
					// Each joinable thread is owned and joined exactly once here.
				}
			}
		}

		/** Disables automatic stop and join behavior for this guard. */
		void release() noexcept
		{
			active_ = false;
		}

	private:
		std::vector<std::thread>& threads_;
		StopFunction stop_function_;
		bool active_ = true;
	};

	template <class StopFunction>
	/** Creates a join guard with a moved stop callback. */
	ThreadJoinGuard<StopFunction> make_thread_join_guard(
		std::vector<std::thread>& threads,
		StopFunction stop_function)
	{
		return ThreadJoinGuard<StopFunction>(
			threads,
			std::move(stop_function));
	}

} // namespace texas::util::detail
