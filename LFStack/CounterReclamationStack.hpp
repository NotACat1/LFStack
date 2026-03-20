#pragma once

#include "IStack.hpp"
#include <atomic>
#include <memory>

/**
 * @brief Lock-Free Stack using Thread Counting for Deferred Reclamation.
 * * This implementation addresses the "Pop-While-Push" and "Use-After-Free" problems
 * by tracking the number of threads currently executing the pop() operation.
 * Memory reclamation is deferred until it is guaranteed that no other threads
 * are accessing the nodes marked for deletion.
 * * @note Limitation: If a thread enters pop() and stalls, the 'to_be_deleted'
 * list will grow indefinitely (Memory Bloat).
 */
template <typename T>
class LFStack_ThreadCounter : public IStack<T> {
private:
    struct Node {
        std::shared_ptr<T> data;
        Node* next;

        // Data is wrapped in shared_ptr to allow safe extraction during pop()
        explicit Node(T const& data_)
            : data(std::make_shared<T>(data_)), next(nullptr) {
        }
    };

    std::atomic<Node*> head{ nullptr };

    // Tracks threads currently inside pop() to ensure safe memory reclamation
    std::atomic<unsigned> threads_in_pop{ 0 };

    // Head of the retired nodes list waiting to be deleted
    std::atomic<Node*> to_be_deleted{ nullptr };

    /**
     * @brief Physically deletes a linked chain of nodes.
     */
    static void delete_nodes(Node* nodes) {
        while (nodes) {
            Node* next = nodes->next;
            delete nodes;
            nodes = next;
        }
    }

    /**
     * @brief Atomically prepends a chain of retired nodes to the 'to_be_deleted' list.
     */
    void chain_pending_nodes(Node* first, Node* last) {
        last->next = to_be_deleted.load(std::memory_order_relaxed);
        while (!to_be_deleted.compare_exchange_weak(
            last->next, first,
            std::memory_order_release, // Ensure node data is visible
            std::memory_order_relaxed));
    }

    void chain_pending_nodes(Node* nodes) {
        Node* last = nodes;
        while (Node* const next = last->next) {
            last = next;
        }
        chain_pending_nodes(nodes, last);
    }

    void chain_pending_node(Node* n) {
        chain_pending_nodes(n, n);
    }

    /**
     * @brief Attempts to reclaim retired nodes if no other threads are in pop().
     * * If the current thread is the only one in pop(), it claims the entire
     * 'to_be_deleted' list. Otherwise, the node is simply added to the list.
     */
    void try_reclaim(Node* old_head) {
        // seq_cst is used here to ensure total ordering of thread counting
        if (threads_in_pop.load(std::memory_order_seq_cst) == 1) {

            // Atomically seize the entire list of pending deletions
            Node* nodes_to_delete = to_be_deleted.exchange(nullptr, std::memory_order_acquire);

            // Re-check: are we still the only thread?
            if (--threads_in_pop == 0) {
                // Safe to wipe all retired nodes
                delete_nodes(nodes_to_delete);
            }
            else if (nodes_to_delete) {
                // Another thread entered pop(); put the nodes back
                chain_pending_nodes(nodes_to_delete);
            }
            // The node we just popped is unique to us; delete it immediately
            delete old_head;
        }
        else {
            // Other threads are present; defer deletion of our node
            chain_pending_node(old_head);
            --threads_in_pop;
        }
    }

public:
    LFStack_ThreadCounter() = default;

    /**
     * @brief Cleans up remaining nodes in the stack and the retirement list.
     */
    ~LFStack_ThreadCounter() override {
        // Clear active stack
        delete_nodes(head.exchange(nullptr));
        // Clear deferred nodes
        delete_nodes(to_be_deleted.load());
    }

    /**
     * @brief Pushes a value onto the stack using a standard CAS loop.
     */
    void push(T const& value) override {
        Node* const new_node = new Node(value);
        new_node->next = head.load(std::memory_order_relaxed);

        // Release ordering ensures the node's content is visible to any popping thread
        while (!head.compare_exchange_weak(
            new_node->next, new_node,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    /**
     * @brief Pops a value from the stack.
     * @return shared_ptr to the data, or nullptr if empty.
     */
    std::shared_ptr<T> pop() override {
        // Step 1: "Announce" entry to protect nodes from being deleted while we read them
        ++threads_in_pop;

        Node* old_head = head.load(std::memory_order_relaxed);

        // Step 2: Try to claim the current head
        // Acquire ordering ensures we see the data written by push()
        while (old_head && !head.compare_exchange_weak(
            old_head, old_head->next,
            std::memory_order_acquire,
            std::memory_order_relaxed));

        std::shared_ptr<T> res;
        if (old_head) {
            // Step 3: Move data out before the node is potentially reclaimed
            res.swap(old_head->data);
            try_reclaim(old_head);
        }
        else {
            // Stack was empty; decrement count and leave
            --threads_in_pop;
        }

        return res;
    }
};