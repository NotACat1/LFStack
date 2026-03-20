#pragma once
#include <memory>

/**
 * @brief Pure virtual interface for Lock-Free Stack implementations.
 * * Provides a unified abstraction for various lock-free algorithms, allowing
 * for easy benchmarking, unit testing, and runtime implementation swapping.
 */
template <typename T>
class IStack {
public:
    virtual ~IStack() = default;

    /**
     * @brief Pushes a new element onto the top of the stack.
     * @param value The value to be copied and stored in the stack.
     * @note Implementation must ensure thread-safety and atomicity.
     */
    virtual void push(T const& value) = 0;

    /**
     * @brief Attempts to pop the top element from the stack.
     * @return std::shared_ptr<T> A shared pointer to the data if successful;
     * nullptr if the stack is empty.
     * @note This method typically handles the complexity of memory reclamation.
     */
    virtual std::shared_ptr<T> pop() = 0;
};