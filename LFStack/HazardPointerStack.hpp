#pragma once

#include "IStack.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <algorithm>
#include <stdexcept>

/**
 * @brief Lock-Free Stack using Hazard Pointers for Memory Reclamation.
 * * This implementation addresses the "Memory Bloat" issue found in simple thread-counting
 * reclamation. By "announcing" which node a thread is currently accessing via a
 * Hazard Pointer, we provide a fine-grained guarantee that a node will not be
 * deleted while in use.
 * * @tparam T The type of elements stored in the stack.
 */
template <typename T>
class LFStack_HazardPtr : public IStack<T> {
private:
    struct Node {
        std::shared_ptr<T> data;
        Node* next;

        explicit Node(T const& data_)
            : data(std::make_shared<T>(data_)), next(nullptr) {
        }
    };

    // --- Hazard Pointer Management ---

    /**
     * @brief Maximum number of concurrent threads supported by the HP array.
     */
    static constexpr unsigned MAX_HAZARD_POINTERS = 100;

    struct HazardPointer {
        std::atomic<bool> active{ false };
        std::atomic<void*> pointer{ nullptr };
    };

    /**
     * @brief Global array of Hazard Pointers for this stack type.
     * @note In a production environment, this might be managed by a global HP registry.
     */
    static inline HazardPointer hazard_pointers[MAX_HAZARD_POINTERS];

    /**
     * @brief RAII wrapper to manage the lifecycle of a Hazard Pointer slot for a thread.
     */
    class HPRecordOwner {
        HazardPointer* hp{ nullptr };
    public:
        HPRecordOwner() {
            for (unsigned i = 0; i < MAX_HAZARD_POINTERS; ++i) {
                bool expected = false;
                // Try to acquire an inactive HP slot
                if (hazard_pointers[i].active.compare_exchange_strong(expected, true)) {
                    hp = &hazard_pointers[i];
                    break;
                }
            }
            if (!hp) {
                throw std::runtime_error("No hazard pointers available. Increase MAX_HAZARD_POINTERS.");
            }
        }

        ~HPRecordOwner() {
            if (hp) {
                // Release the slot for other threads
                hp->pointer.store(nullptr, std::memory_order_release);
                hp->active.store(false, std::memory_order_release);
            }
        }

        HazardPointer* get() const { return hp; }
    };

    /**
     * @brief Retrieves the Hazard Pointer slot assigned to the calling thread.
     * Uses thread_local storage to ensure the lookup happens only once per thread.
     */
    static HazardPointer* get_hazard_pointer_for_current_thread() {
        thread_local static HPRecordOwner owner;
        return owner.get();
    }

    // --- Stack Members ---

    std::atomic<Node*> head{ nullptr };
    std::atomic<Node*> to_be_deleted{ nullptr };

    /**
     * @brief Helper to physically deallocate a linked list of nodes.
     */
    static void delete_nodes(Node* nodes) {
        while (nodes) {
            Node* next = nodes->next;
            delete nodes;
            nodes = next;
        }
    }

    /**
     * @brief Scans the retired nodes list and deletes those not protected by any Hazard Pointer.
     * * This is the "Garbage Collection" phase. It uses a Sort + Binary Search
     * approach to efficiently check retired nodes against active Hazard Pointers.
     */
    void scan_and_delete() {
        // Step 1: Claim the entire list of nodes pending deletion
        Node* current_list = to_be_deleted.exchange(nullptr, std::memory_order_acquire);
        if (!current_list) return;

        // Step 2: Collect all currently active Hazard Pointers
        std::vector<void*> active_hps;
        active_hps.reserve(MAX_HAZARD_POINTERS);
        for (unsigned i = 0; i < MAX_HAZARD_POINTERS; ++i) {
            void* ptr = hazard_pointers[i].pointer.load(std::memory_order_acquire);
            if (ptr) {
                active_hps.push_back(ptr);
            }
        }

        // Step 3: Sort for fast lookup via binary search
        std::sort(active_hps.begin(), active_hps.end());

        Node* nodes_to_keep = nullptr;
        Node* keep_tail = nullptr;

        // Step 4: Iterate through the retired list
        while (current_list) {
            Node* next = current_list->next;

            if (std::binary_search(active_hps.begin(), active_hps.end(), current_list)) {
                // Node is still "Hazardous" (referenced by a thread); keep it in the list
                current_list->next = nodes_to_keep;
                if (!nodes_to_keep) keep_tail = current_list;
                nodes_to_keep = current_list;
            }
            else {
                // Node is safe to delete
                delete current_list;
            }
            current_list = next;
        }

        // Step 5: Prepend nodes we couldn't delete back to the global 'to_be_deleted' list
        if (nodes_to_keep) {
            Node* old_to_be_deleted = to_be_deleted.load(std::memory_order_relaxed);
            do {
                keep_tail->next = old_to_be_deleted;
            } while (!to_be_deleted.compare_exchange_weak(
                old_to_be_deleted, nodes_to_keep,
                std::memory_order_release,
                std::memory_order_relaxed));
        }
    }

    /**
     * @brief Marks a node as retired and attempts a reclamation cycle.
     */
    void reclaim_node(Node* node) {
        // Prepend node to the retired list
        node->next = to_be_deleted.load(std::memory_order_relaxed);
        while (!to_be_deleted.compare_exchange_weak(
            node->next, node,
            std::memory_order_release,
            std::memory_order_relaxed));

        // Trigger the scan
        scan_and_delete();
    }

public:
    LFStack_HazardPtr() = default;

    /**
     * @brief Destructor cleans up the active stack and any remaining retired nodes.
     */
    ~LFStack_HazardPtr() override {
        delete_nodes(head.exchange(nullptr));
        delete_nodes(to_be_deleted.exchange(nullptr));
    }

    /**
     * @brief Standard lock-free push using a CAS loop.
     */
    void push(T const& value) override {
        Node* const new_node = new Node(value);
        new_node->next = head.load(std::memory_order_relaxed);

        while (!head.compare_exchange_weak(
            new_node->next, new_node,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    /**
     * @brief Pops an element using the Hazard Pointer "Advertise-and-Verify" pattern.
     * @return shared_ptr to the data, or nullptr if empty.
     */
    std::shared_ptr<T> pop() override {
        HazardPointer* hp = get_hazard_pointer_for_current_thread();
        Node* old_head = head.load(std::memory_order_relaxed);

        while (true) {
            Node* temp;
            // --- The "Advertise-and-Verify" Pattern ---
            // 1. Store the pointer we intend to read in our HP slot (Advertise).
            // 2. Re-read the head to ensure it hasn't changed in the meantime (Verify).
            // This prevents a race where the node is reclaimed between the load and the CAS.
            do {
                temp = old_head;
                hp->pointer.store(old_head, std::memory_order_release);
                old_head = head.load(std::memory_order_acquire);
            } while (old_head != temp);

            if (old_head == nullptr) {
                break; // Stack is empty
            }

            // Attempt to claim the node
            if (head.compare_exchange_strong(
                old_head, old_head->next,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
                break; // Successfully popped
            }
            // If CAS fails, old_head is updated with the new head; loop continues.
        }

        // Clear the HP advertisement: we either have the node or the stack is empty.
        hp->pointer.store(nullptr, std::memory_order_release);

        std::shared_ptr<T> res;
        if (old_head) {
            res.swap(old_head->data);
            reclaim_node(old_head);
        }

        return res;
    }
};