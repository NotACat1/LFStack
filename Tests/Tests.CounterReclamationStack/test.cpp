#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "../../LFStack/CounterReclamationStack.hpp" 

/**
 * @brief Test fixture for LFStack_ThreadCounter.
 */
class LFStackThreadCounterTest : public ::testing::Test {
protected:
    LFStack_ThreadCounter<int> stack;
};

// --- Single-Threaded Functional Tests ---

/**
 * @brief Ensures that popping from a newly initialized stack returns nullptr.
 */
TEST_F(LFStackThreadCounterTest, EmptyPopReturnsNull) {
    auto result = stack.pop();
    EXPECT_EQ(result, nullptr);
}

/**
 * @brief Verifies standard LIFO (Last-In-First-Out) behavior in a single-threaded environment.
 */
TEST_F(LFStackThreadCounterTest, PushAndPopSingleThread) {
    stack.push(10);
    stack.push(20);
    stack.push(30);

    // Verify order: 30 -> 20 -> 10
    EXPECT_EQ(*stack.pop(), 30);
    EXPECT_EQ(*stack.pop(), 20);
    EXPECT_EQ(*stack.pop(), 10);
    EXPECT_EQ(stack.pop(), nullptr);
}

// --- Multi-Threaded Concurrency Tests ---

/**
 * @brief Verifies data integrity during concurrent push operations.
 * * Multiple threads attempt to push elements simultaneously to test the robustness
 * of the CAS (Compare-And-Swap) loop and ensure no nodes are lost under contention.
 */
TEST_F(LFStackThreadCounterTest, ConcurrentPush) {
    const int num_threads = 10;
    const int pushes_per_thread = 1000;
    std::vector<std::thread> threads;

    // Launch threads to perform concurrent pushes
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, pushes_per_thread]() {
            for (int j = 0; j < pushes_per_thread; ++j) {
                stack.push(i * pushes_per_thread + j);
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Drain the stack and verify the total count of elements
    int count = 0;
    while (stack.pop() != nullptr) {
        count++;
    }

    EXPECT_EQ(count, num_threads * pushes_per_thread);
}

/**
 * @brief Verifies data integrity during concurrent pop operations.
 * * Pre-fills the stack and then uses multiple threads to pop elements,
 * ensuring each element is popped exactly once and no double-free errors occur.
 */
TEST_F(LFStackThreadCounterTest, ConcurrentPop) {
    const int total_elements = 10000;
    for (int i = 0; i < total_elements; ++i) {
        stack.push(i);
    }

    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> popped_count{ 0 };

    // Launch threads to drain the stack concurrently
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &popped_count]() {
            while (auto val = stack.pop()) {
                popped_count.fetch_add(1, std::memory_order_relaxed);
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(popped_count.load(), total_elements);
    EXPECT_EQ(stack.pop(), nullptr);
}

/**
 * @brief High-contention stress test with mixed Push and Pop workloads.
 * * This test simulates real-world usage where threads are simultaneously adding and
 * removing data. It uses a checksum-based approach (sum of values) to ensure
 * that every value pushed is eventually popped, proving that the deferred
 * reclamation logic (threads_in_pop) is working correctly.
 */
TEST_F(LFStackThreadCounterTest, ConcurrentPushAndPopStressTest) {
    const int num_threads = 8;
    const int operations_per_thread = 5000;
    std::vector<std::thread> threads;
    std::atomic<long long> push_sum{ 0 };
    std::atomic<long long> pop_sum{ 0 };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &push_sum, &pop_sum, operations_per_thread]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                int val = j + 1;
                stack.push(val);
                push_sum.fetch_add(val, std::memory_order_relaxed);

                // Attempt an immediate pop to increase contention on reclamation logic
                if (auto popped = stack.pop()) {
                    pop_sum.fetch_add(*popped, std::memory_order_relaxed);
                }
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Perform final cleanup of any remaining elements in the stack
    while (auto popped = stack.pop()) {
        pop_sum.fetch_add(*popped, std::memory_order_relaxed);
    }

    // The sum of all pushed values must equal the sum of all popped values
    EXPECT_EQ(push_sum.load(), pop_sum.load());
}