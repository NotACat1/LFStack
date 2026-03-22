#pragma once

#include "IStack.hpp"
#include <atomic>
#include <memory>

/**
 * @brief Lock-Free Stack utilizing C++20 std::atomic<std::shared_ptr<T>>.
 *
 * This represents the "Holy Grail" of modern lock-free C++. By leveraging
 * the standard library's atomic shared pointers, all memory reclamation,
 * reference counting, and ABA prevention logic is handled natively.
 * * @note Requires a C++20 compliant compiler. Performance characteristics
 * highly depend on the standard library's internal implementation (e.g.,
 * whether it uses lock-free hardware instructions or falls back to spinlocks
 * under the hood for large atomic types).
 *
 * @tparam T The type of elements stored in the stack.
 */
template <typename T>
class LFStack_AtomicSharedPtr : public IStack<T> {
private:
    struct Node {
        std::shared_ptr<T> data;
        std::shared_ptr<Node> next;

        explicit Node(T const& data_)
            : data(std::make_shared<T>(data_)), next(nullptr) {
        }
    };

    // The head of the stack is now a fully atomic shared pointer.
    std::atomic<std::shared_ptr<Node>> head;

public:
    LFStack_AtomicSharedPtr() = default;

    /**
     * @brief Destructor safely drains the stack.
     * We actively pop elements rather than relying on default shared_ptr
     * destruction to prevent potential deep recursion/stack overflow
     * from a long chain of shared_ptr destructors.
     */
    ~LFStack_AtomicSharedPtr() override {
        while (pop()) {
            // Drain the stack completely
        }
    }

    /**
     * @brief Pushes a new element onto the stack using a simple CAS loop.
     */
    void push(T const& value) override {
        auto new_node = std::make_shared<Node>(value);
        new_node->next = head.load(std::memory_order_relaxed);

        // Compare-And-Swap loop updates the head automatically managing ref counts
        while (!head.compare_exchange_weak(
            new_node->next, new_node,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            // new_node->next is automatically updated on failure
        }
    }

    /**
     * @brief Pops an element from the stack safely.
     * @return std::shared_ptr<T> to the data, or nullptr if empty.
     */
    std::shared_ptr<T> pop() override {
        std::shared_ptr<Node> old_head = head.load(std::memory_order_relaxed);

        // Loop until successful CAS or the stack is empty (old_head == nullptr)
        while (old_head && !head.compare_exchange_weak(
            old_head, old_head->next,
            std::memory_order_acquire,
            std::memory_order_relaxed)) {
            // old_head is automatically updated on failure
        }

        // If old_head is valid, we successfully claimed the node.
        // Its memory will be automatically freed when all shared_ptrs go out of scope.
        return old_head ? old_head->data : nullptr;
    }
};