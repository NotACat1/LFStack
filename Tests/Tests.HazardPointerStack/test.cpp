#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>

// Include the Hazard Pointer implementation
#include "../../LFStack/HazardPointerStack.hpp" 

/**
 * @brief Test fixture for LFStack_HazardPtr.
 * * Provides a clean environment for testing the Hazard Pointer-based
 * lock-free stack implementation across various execution models.
 */
class LFStackHazardPtrTest : public ::testing::Test {
protected:
    LFStack_HazardPtr<int> stack;
};

// --- Single-Threaded Functional Tests ---

/**
 * @brief Verifies that an empty stack correctly returns nullptr.
 * * Ensures the initial state of the head pointer and the basic
 * logic of the pop() method handle the empty case gracefully.
 */
TEST_F(LFStackHazardPtrTest, EmptyPopReturnsNull) {
    auto result = stack.pop();
    EXPECT_EQ(result, nullptr);
}

/**
 * @brief Validates standard LIFO (Last-In-First-Out) semantics.
 * * Performs basic single-threaded operations to ensure that the
 * underlying linked list maintains correct ordering.
 */
TEST_F(LFStackHazardPtrTest, PushAndPopSingleThread) {
    stack.push(10);
    stack.push(20);
    stack.push(30);

    // Expected LIFO order: 30 -> 20 -> 10
    auto val1 = stack.pop();
    auto val2 = stack.pop();
    auto val3 = stack.pop();

    ASSERT_NE(val1, nullptr);
    ASSERT_NE(val2, nullptr);
    ASSERT_NE(val3, nullptr);

    EXPECT_EQ(*val1, 30);
    EXPECT_EQ(*val2, 20);
    EXPECT_EQ(*val3, 10);
    EXPECT_EQ(stack.pop(), nullptr);
}

// --- Multi-Threaded Concurrency Tests ---

/**
 * @brief Tests data integrity during high-contention concurrent pushes.
 * * Multiple threads attempt to push unique values simultaneously. This validates
 * the robustness of the Compare-And-Swap (CAS) loop and ensures no nodes
 * are dropped or lost due to race conditions.
 */
TEST_F(LFStackHazardPtrTest, ConcurrentPush) {
    const int num_threads = 10;
    const int pushes_per_thread = 1000;
    std::vector<std::thread> threads;

    // Launch concurrent writers
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

    // Validate the total number of elements pushed
    int count = 0;
    while (stack.pop() != nullptr) {
        count++;
    }

    EXPECT_EQ(count, num_threads * pushes_per_thread);
}

/**
 * @brief Tests thread-safety and ABA-prevention during concurrent pops.
 * * Pre-fills the stack and uses multiple threads to drain it. This ensures
 * that Hazard Pointers correctly protect nodes from premature deletion
 * and that each element is claimed exactly once.
 */
TEST_F(LFStackHazardPtrTest, ConcurrentPop) {
    const int total_elements = 10000;
    for (int i = 0; i < total_elements; ++i) {
        stack.push(i);
    }

    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> popped_count{ 0 };

    // Launch concurrent readers
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
 * @brief Comprehensive stress test with mixed workloads (Push/Pop).
 * * Simulates a heavy production-like workload where threads are simultaneously
 * adding and removing data. We use a checksum (sum of values) to verify
 * that no data is corrupted or leaked.
 * * This test specifically validates the Hazard Pointer 'scan and delete'
 * logic under intense contention where threads frequently acquire and release
 * the same pointers.
 */
TEST_F(LFStackHazardPtrTest, ConcurrentPushAndPopStressTest) {
    const int num_threads = 8;
    const int operations_per_thread = 5000;
    std::vector<std::thread> threads;

    // Checksums to ensure data consistency
    std::atomic<long long> push_sum{ 0 };
    std::atomic<long long> pop_sum{ 0 };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &push_sum, &pop_sum, operations_per_thread]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                int val = j + 1;

                // Push and track the value
                stack.push(val);
                push_sum.fetch_add(val, std::memory_order_relaxed);

                // Attempt to pop immediately to trigger contention 
                // between the push CAS and the reclamation logic
                if (auto popped = stack.pop()) {
                    pop_sum.fetch_add(*popped, std::memory_order_relaxed);
                }
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Final drain of the stack to collect any remaining elements
    while (auto popped = stack.pop()) {
        pop_sum.fetch_add(*popped, std::memory_order_relaxed);
    }

    // Verification: Data in == Data out
    EXPECT_EQ(push_sum.load(), pop_sum.load());
}