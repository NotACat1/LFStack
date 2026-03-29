# Lock-Free Stack: Memory Reclamation Strategies

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![Tests](https://img.shields.io/badge/tests-GTest-orange.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## Project Overview

This project is a study and reference implementation of lock-free stacks in C++. The primary focus is on solving the classical ABA problem and ensuring safe memory reclamation in a concurrent environment.

The repository provides a “golden set” of classical algorithms implemented in modern C++, along with a comparative performance analysis using Google Benchmark.

## 🛠 Implemented Strategies

### 1. `LFStack_ThreadCounter` (Thread Counting)

- **Method:** Counting active threads within the `pop()` operation.
- **Features:** The simplest and fastest implementation.
- **Limitations:** Prone to memory bloat. If one of the reading threads is preempted or goes to sleep, it blocks node reclamation for all other threads.

### 2. `LFStack_HazardPtr` (Hazard Pointers)

- **Method:** Using Hazard Pointers.
- **Features:** Each thread publishes a globally visible “announcement” (pointer) to the node it is currently working with. A node is guaranteed not to be deleted while at least one hazard pointer references it. Provides strong safety guarantees and strict memory management.

### 3. `LFStack_SplitRefCount` (Split Reference Counting)

- **Method:** Internal and external counters.
- **Features:** Each node maintains two counters. The external counter (as part of the atomic `head` pointer) is incremented on every access, while the internal counter is decremented when work is completed. Effectively resolves the “sleeping thread” issue present in the first approach.

### 4. `LFStack_AtomicSharedPtr` (C++20)

- **Method:** Using `std::atomic<std::shared_ptr<T>>`.
- **Features:** The most modern approach. All complex logic related to memory management, memory barriers, and ABA prevention is delegated to the standard library. A great balance between safety and maintainability.

---

## 📊 Performance Results

Benchmarks are implemented using **Google Benchmark**.

**Test Environment:**
16 Cores, 1900 MHz | 8 Threads

| Implementation              | Payload | Time (ns) | Throughput (ops/sec) |
| :-------------------------- | :------ | :-------- | :------------------- |
| **LFStack_ThreadCounter**   | Light   | 5591      | **1.44 M**           |
| **LFStack_SplitRefCount**   | Light   | 6424      | 1.36 M               |
| **LFStack_AtomicSharedPtr** | Light   | 6851      | 1.25 M               |
| **LFStack_HazardPointer**   | Light   | 8391      | 925.4 k              |
| ---                         | ---     | ---       | ---                  |
| **LFStack_SplitRefCount**   | Heavy   | 8394      | **948.8 k**          |
| **LFStack_ThreadCounter**   | Heavy   | 10050     | 819.4 k              |
| **LFStack_AtomicSharedPtr** | Heavy   | 10736     | 674.6 k              |
| **LFStack_HazardPointer**   | Heavy   | 13317     | 689.8 k              |

---

## 🧐 Lock-Free vs Mutex: Performance Analysis

Many developers notice that a classic `std::mutex` can outperform lock-free implementations by a large margin (sometimes 10–20x) when using a small number of threads.

**Why does this happen?**

1. **Cache Contention:** Lock-free algorithms generate heavy cache traffic due to frequent `CAS` operations on a single shared location (`head`).
2. **Instruction Overhead:** Proper memory management in lock-free environments requires multiple atomic operations per `pop`, whereas a mutex minimizes this overhead within a critical section.
3. **Memory Reclamation:** The main overhead in these benchmarks comes not from the stack itself, but from the logic of **safe node reclamation**.

**When does lock-free win?**
Lock-free solutions begin to outperform mutex-based approaches in **high-contention scenarios** (hundreds of threads), real-time systems (RTOS) where blocking high-priority threads is unacceptable, or under specific workloads where mutex wait time becomes a critical bottleneck.
