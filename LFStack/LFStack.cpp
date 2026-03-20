#include <vector>
#include <thread>
#include <chrono>

void ProcessData(int id) {
    DEBUG_TIMER("ProcessData Thread");
    DEBUG_LOG("Thread starting: " + std::to_string(id));

    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(id * 100));

    if (id == 3) {
        DEBUG_ERR("Simulated failure in thread " + std::to_string(id));
    }
}

int main() {
    DEBUG_TIMER("Main function total execution");
    DEBUG_LOG("Application Started");

    // 1. Thread Safety Test
    std::vector<std::thread> threads;
    for (int i = 1; i <= 3; ++i) {
        threads.emplace_back(ProcessData, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // 2. Memory Leak Detection Test (Visible in MSVC Output)
    int* intentionalLeak = new int[500]; // We won't delete this!

    DEBUG_LOG("Application shutting down. Check for memory leaks!");
    return 0;
}