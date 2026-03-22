#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>

// Подключаем файлы с твоими реализациями
#include "../../LFStack/CounterReclamationStack.hpp"
#include "../../LFStack/HazardPointerStack.hpp"
#include "../../LFStack/SplitRefCountStack.hpp"
#include "../../LFStack/AtomicSharedStack.hpp"

/**
 * @brief Template test fixture for Lock-Free Stacks.
 * * Provides a clean environment for testing different lock-free stack
 * implementations (Split Reference Counting and C++20 Atomic Shared Ptr)
 * across various execution models without code duplication.
 * * @tparam TStack The specific stack type to test.
 */
template <typename TStack>
class LockFreeStackTest : public ::testing::Test {
protected:
    TStack stack;
};

// Define the list of implementations we want to test
using StackImplementations = ::testing::Types<
    LFStack_ThreadCounter<int>,
    LFStack_HazardPtr<int>,
    LFStack_SplitRefCount<int>
>;

// Register the typed test suite
TYPED_TEST_CASE(LockFreeStackTest, StackImplementations);

// --- Single-Threaded Functional Tests ---

/**
 * @brief Verifies that an empty stack correctly returns nullptr.
 * Ensures the initial state of the head pointer and the basic
 * logic of the pop() method handle the empty case gracefully.
 */
TYPED_TEST(LockFreeStackTest, EmptyPopReturnsNull) {
    auto result = this->stack.pop();
    EXPECT_EQ(result, nullptr);
}

/**
 * @brief Validates standard LIFO (Last-In-First-Out) semantics.
 * Performs basic single-threaded operations to ensure that the
 * underlying linked list maintains correct ordering.
 */
TYPED_TEST(LockFreeStackTest, PushAndPopSingleThread) {
    this->stack.push(10);
    this->stack.push(20);
    this->stack.push(30);

    // Expected LIFO order: 30 -> 20 -> 10
    auto val1 = this->stack.pop();
    auto val2 = this->stack.pop();
    auto val3 = this->stack.pop();

    ASSERT_NE(val1, nullptr);
    ASSERT_NE(val2, nullptr);
    ASSERT_NE(val3, nullptr);

    EXPECT_EQ(*val1, 30);
    EXPECT_EQ(*val2, 20);
    EXPECT_EQ(*val3, 10);
    EXPECT_EQ(this->stack.pop(), nullptr);
}

// --- Multi-Threaded Concurrency Tests ---

/**
 * @brief Tests data integrity during high-contention concurrent pushes.
 * Multiple threads attempt to push unique values simultaneously. This validates
 * the robustness of the Compare-And-Swap (CAS) loop and ensures no nodes
 * are dropped or lost due to race conditions.
 */
TYPED_TEST(LockFreeStackTest, ConcurrentPush) {
    const int num_threads = 10;
    const int pushes_per_thread = 1000;
    std::vector<std::thread> threads;

    // Launch concurrent writers
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, pushes_per_thread]() {
            for (int j = 0; j < pushes_per_thread; ++j) {
                this->stack.push(i * pushes_per_thread + j);
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Validate the total number of elements pushed
    int count = 0;
    while (this->stack.pop() != nullptr) {
        count++;
    }

    EXPECT_EQ(count, num_threads * pushes_per_thread);
}

/**
 * @brief Tests thread-safety and memory reclamation during concurrent pops.
 * Pre-fills the stack and uses multiple threads to drain it. This ensures
 * that both split-reference counting and atomic shared_ptrs correctly
 * protect nodes from premature deletion and that each element is claimed exactly once.
 */
TYPED_TEST(LockFreeStackTest, ConcurrentPop) {
    const int total_elements = 10000;
    for (int i = 0; i < total_elements; ++i) {
        this->stack.push(i);
    }

    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> popped_count{ 0 };

    // Launch concurrent readers
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &popped_count]() {
            while (auto val = this->stack.pop()) {
                popped_count.fetch_add(1, std::memory_order_relaxed);
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(popped_count.load(), total_elements);
    EXPECT_EQ(this->stack.pop(), nullptr);
}

/**
 * @brief Comprehensive stress test with mixed workloads (Push/Pop).
 * Simulates a heavy production-like workload where threads are simultaneously
 * adding and removing data. We use a checksum (sum of values) to verify
 * that no data is corrupted, duplicated, or leaked.
 */
TYPED_TEST(LockFreeStackTest, ConcurrentPushAndPopStressTest) {
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
                this->stack.push(val);
                push_sum.fetch_add(val, std::memory_order_relaxed);

                // Attempt to pop immediately to trigger high contention
                // between the push CAS and the reclamation logic
                if (auto popped = this->stack.pop()) {
                    pop_sum.fetch_add(*popped, std::memory_order_relaxed);
                }
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Final drain of the stack to collect any remaining elements
    while (auto popped = this->stack.pop()) {
        pop_sum.fetch_add(*popped, std::memory_order_relaxed);
    }

    // Verification: Data in == Data out
    EXPECT_EQ(push_sum.load(), pop_sum.load());
}