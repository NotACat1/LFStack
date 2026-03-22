#include <benchmark/benchmark.h>
#include <memory>
#include <thread>
#include <vector>

/**
 * Include custom lock-free / concurrent stack implementations.
 * Each implementation uses a different memory reclamation strategy.
 */
#include "CounterReclamationStack.hpp"
#include "HazardPointerStack.hpp"
#include "SplitRefCountStack.hpp"
#include "AtomicSharedStack.hpp"

 // --- TEST DATA TYPES ---

 /**
  * Represents a relatively heavy payload (~1 KB).
  * Used to evaluate the impact of object size on throughput and memory behavior.
  */
struct HeavyPayload {
    int data[256];

    // Initialize memory to avoid undefined behavior and sanitizer warnings
    HeavyPayload() {
        std::fill(std::begin(data), std::end(data), 0);
    }
};

/**
 * Lightweight payload used to measure raw synchronization overhead.
 */
using LightPayload = int;


// --- GENERIC BENCHMARK TEMPLATE ---

/**
 * @brief Producer-Consumer benchmark for concurrent stack implementations.
 *
 * This benchmark models a typical multi-threaded workload where:
 *  - Half of the threads act as producers (push operations)
 *  - The other half act as consumers (pop operations)
 *
 * @tparam StackType  Concrete stack implementation
 * @tparam Payload    Type of stored data (lightweight or heavyweight)
 *
 * @param state       Google Benchmark state object
 */
template <typename StackType, typename Payload>
void BM_Stack_ProducerConsumer(benchmark::State& state) {
    /**
     * Function-local static ensures:
     *  - Thread-safe initialization (since C++11)
     *  - A single shared stack instance across all threads in the benchmark
     *
     * IMPORTANT:
     * This assumes a balanced workload (push == pop),
     * so the stack is expected to be empty after each full benchmark run.
     */
    static StackType shared_stack;

    /**
     * Thread role assignment:
     *  - Even thread indices → Producers
     *  - Odd thread indices  → Consumers
     */
    if (state.thread_index() % 2 == 0) {
        // --- PRODUCER PATH ---

        /**
         * Pre-constructed payload to avoid per-iteration allocation cost.
         * Value-initialization ensures deterministic content.
         */
        Payload payload{};

        for (auto _ : state) {
            shared_stack.push(payload);
        }
    }
    else {
        // --- CONSUMER PATH ---

        for (auto _ : state) {
            std::shared_ptr<Payload> result;

            /**
             * pop() returns nullptr if the stack is empty.
             * Use a spin-wait loop with cooperative yielding.
             *
             * NOTE:
             * - This introduces contention and scheduler interaction.
             * - Suitable for stress-testing lock-free behavior under load.
             */
            while ((result = shared_stack.pop()) == nullptr) {
                std::this_thread::yield();
            }

            /**
             * Prevent the compiler from optimizing away the result usage.
             * Ensures the pop operation is fully accounted for.
             */
            benchmark::DoNotOptimize(result);
        }
    }

    /**
     * Throughput metric: total number of processed operations.
     *
     * Only one thread updates the counter to avoid redundant writes.
     *
     * NOTE:
     * This currently counts all threads equally.
     * If precise accounting is required, consider counting only
     * producer or consumer operations separately.
     */
    if (state.thread_index() == 0) {
        state.SetItemsProcessed(state.iterations() * state.threads());
    }
}


// --- BENCHMARK REGISTRATION ---

/**
 * Fixed configuration:
 * 8 threads total → 4 producers + 4 consumers
 */
#define STACK_THREADS ->Threads(8)


 // --- LIGHT PAYLOAD (int) ---

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_ThreadCounter<int>, int)
->Name("Pop/CounterReclamationStack/Light") STACK_THREADS;

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_HazardPtr<int>, int)
->Name("Pop/HazardPointerStack/Light") STACK_THREADS;

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_SplitRefCount<int>, int)
->Name("Pop/SplitRefCountStack/Light") STACK_THREADS;

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_AtomicSharedPtr<int>, int)
->Name("Pop/AtomicSharedStack/Light") STACK_THREADS;


// --- HEAVY PAYLOAD (~1 KB) ---

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_ThreadCounter<HeavyPayload>, HeavyPayload)
->Name("Pop/CounterReclamationStack/Heavy") STACK_THREADS;

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_HazardPtr<HeavyPayload>, HeavyPayload)
->Name("Pop/HazardPointerStack/Heavy") STACK_THREADS;

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_SplitRefCount<HeavyPayload>, HeavyPayload)
->Name("Pop/SplitRefCountStack/Heavy") STACK_THREADS;

BENCHMARK_TEMPLATE(BM_Stack_ProducerConsumer, LFStack_AtomicSharedPtr<HeavyPayload>, HeavyPayload)
->Name("Pop/AtomicSharedStack/Heavy") STACK_THREADS;


// --- ENTRY POINT ---

/**
 * Main entry point for Google Benchmark.
 * Automatically registers and executes all defined benchmarks.
 */
BENCHMARK_MAIN();