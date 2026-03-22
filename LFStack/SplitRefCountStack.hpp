#pragma once

#include "IStack.hpp"
#include <atomic>
#include <memory>

/**
 * @brief Lock-Free Stack using Split Reference Counting for Memory Reclamation.
 *
 * This implementation solves the "Memory Bloat" issue by maintaining two reference
 * counters: an external counter (packed with the head pointer) and an internal
 * counter (inside the node). A node is safely deleted when all threads have
 * released their references.
 *
 * @note This implementation relies on the hardware supporting Double-Word
 * Compare-And-Swap (DWCAS), such as cmpxchg16b on x86_64 architectures, to
 * atomically update the CountedNodePtr (16 bytes on 64-bit systems).
 *
 * @tparam T The type of elements stored in the stack.
 */
template <typename T>
class LFStack_SplitRefCount : public IStack<T> {
private:
    struct Node;

    /**
     * @brief A structure combining a pointer and a reference counter.
     * Designed to be exactly 16 bytes on 64-bit systems for DWCAS compatibility.
     */
    struct CountedNodePtr {
        int external_count;
        Node* ptr;
    };

    struct Node {
        std::shared_ptr<T> data;
        std::atomic<int> internal_count;
        CountedNodePtr next;

        explicit Node(T const& data_)
            : data(std::make_shared<T>(data_)),
            internal_count(0),
            next{ 0, nullptr } {
        }
    };

    std::atomic<CountedNodePtr> head;

    /**
     * @brief Safely increments the external reference count of the head.
     * This acts as an "acquire" ticket, preventing the node from being deleted
     * while the current thread is reading its contents.
     */
    void increase_head_count(CountedNodePtr& old_counter) {
        CountedNodePtr new_counter;
        do {
            new_counter = old_counter;
            ++new_counter.external_count;
        } while (!head.compare_exchange_strong(
            old_counter, new_counter,
            std::memory_order_acquire,
            std::memory_order_relaxed));

        old_counter.external_count = new_counter.external_count;
    }

public:
    LFStack_SplitRefCount() {
        CountedNodePtr initial_head{ 0, nullptr };
        head.store(initial_head, std::memory_order_relaxed);
    }

    /**
     * @brief Destructor ensures all remaining nodes are properly cleaned up.
     */
    ~LFStack_SplitRefCount() override {
        while (pop());
    }

    /**
     * @brief Pushes a new node onto the stack.
     */
    void push(T const& value) override {
        CountedNodePtr new_node;
        new_node.ptr = new Node(value);
        new_node.external_count = 1; // 1 represents the reference for being in the stack

        new_node.ptr->next = head.load(std::memory_order_relaxed);

        while (!head.compare_exchange_weak(
            new_node.ptr->next, new_node,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    /**
     * @brief Attempts to pop an element utilizing split reference counters.
     * @return std::shared_ptr<T> to the data, or nullptr if empty.
     */
    std::shared_ptr<T> pop() override {
        CountedNodePtr old_head = head.load(std::memory_order_relaxed);

        while (true) {
            // Step 1: Secure a reference to safely read the node
            increase_head_count(old_head);
            Node* const ptr = old_head.ptr;

            // If the stack is empty, release our reference and return
            if (!ptr) {
                return nullptr;
            }

            // Step 2: Try to physically remove the node from the stack
            if (head.compare_exchange_strong(
                old_head, ptr->next,
                std::memory_order_relaxed)) {

                // --- Success Path ---
                // We successfully unlinked the node. It now belongs to this thread.
                std::shared_ptr<T> res;
                res.swap(ptr->data);

                // Calculate how much we need to adjust the internal count.
                // We subtract 2: 
                // -1 for the node no longer being in the list.
                // -1 for our current thread's access ticket (which we are now discarding).
                int const count_increase = old_head.external_count - 2;

                // Transfer the remaining external references to the internal counter.
                // If it balances exactly to zero (meaning no other threads are currently 
                // looking at this node), we are the last one and must delete it.
                if (ptr->internal_count.fetch_add(count_increase, std::memory_order_release) == -count_increase) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    delete ptr;
                }

                return res;
            }
            else {
                // --- Failure Path ---
                // Another thread modified the head. Our CAS failed.
                // We must release our "access ticket" by decrementing the internal count.
                if (ptr->internal_count.fetch_sub(1, std::memory_order_release) == 1) {
                    // We were the last thread holding a reference to this detached node.
                    std::atomic_thread_fence(std::memory_order_acquire);
                    delete ptr;
                }
            }
        }
    }
};