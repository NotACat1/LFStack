# AppFastClusterCPP

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![Tests](https://img.shields.io/badge/tests-GTest-orange.svg)
![Benchmark](https://img.shields.io/badge/benchmark-Google%20Benchmark-blue)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## Project Description

This research project focuses on low-level optimization of classical machine learning algorithms (DBSCAN, K-Means, radius search) in C++ for large-scale data processing.

The main goal is to analyze the impact of modern architectural patterns (Data-Oriented Design), vectorization (SIMD), and multithreading (OpenMP) on computational throughput. The project demonstrates how proper memory organization and the choice of data structures (KD-Tree) can significantly outperform baseline implementations in terms of performance.

## Implemented Functionality

### Machine Learning Algorithms

- **Radius Search:** Brute-force and KD-Tree (single and batch modes).
- **DBSCAN:** Clustering based on full search and spatial partitioning (KD-Tree).
- **K-Means:** Clustering with dynamic adjustment of the number of points (N), clusters (K), and dimensionality (D).

### Distance Metrics

- Euclidean distance and squared Euclidean distance.
- Manhattan distance.
- Chebyshev distance.
- Cosine distance.

### Memory Layout Patterns

- **AoS (Array of Structures):** Traditional object-oriented approach.
- **SoA (Structure of Arrays):** Separation of data by components for efficient SIMD vectorization.
- **AoSoA (Array of Structures of Arrays):** A hybrid approach balancing cache locality and vectorization efficiency.

---

## Architectural Insights & Benchmarks

_Test setup: 16 × 1900 MHz CPU, L1 Data/Inst 32 KiB, L2 512 KiB, L3 16384 KiB._

### 1. Impact of Memory Layout on Auto-Vectorization (SIMD)

The **SoA** layout is the clear leader for compute-intensive tasks. When calculating distances in 3D space, SoA achieves nearly **2.8× higher throughput** than the classic AoS due to ideal compiler auto-vectorization.

| Memory Layout | Benchmark (Distance Calc, Float, 3D) | Items / Second   | Bytes / Second   |
| :------------ | :----------------------------------- | :--------------- | :--------------- |
| **AoS**       | `BM_AoS_Distance<SquaredEuclidean>`  | ~ 67.21 M/s      | ~ 2.00 GiB/s     |
| **SoA**       | `BM_SoA_Distance<SquaredEuclidean>`  | **~ 187.90 M/s** | **~ 2.10 GiB/s** |

### 2. Cache Locality in Reduction (Memory Bound)

For aggregation tasks (e.g., iterating and summing fields of a single entity), the **AoS** layout is more than **5.7× faster** than SoA. The CPU hardware prefetcher efficiently utilizes the L1 cache when sequentially reading contiguous memory of a single structure.

| Memory Layout | Benchmark      | Time (ns)   |
| :------------ | :------------- | :---------- |
| **AoS**       | `BM_AoS_Sum`   | **250,156** |
| **SoA**       | `BM_SoA_Sum`   | 1,433,131   |
| **AoSoA**     | `BM_AoSoA_Sum` | 1,584,360   |

### 3. Superiority of Algorithmic Complexity

Memory optimization has its limits. Transitioning from linear scan **O(N²)** to spatial partitioning structures (KD-Tree) with **O(N log N)** complexity provides the most significant performance gains on large datasets.

| Algorithm / Search      | N (Points) | Time (ns)      | Complexity |
| :---------------------- | :--------- | :------------- | :--------- |
| DBSCAN (BruteForce AoS) | 2,048      | 25,310,636     | O(N²)      |
| DBSCAN (KD-Tree Batch)  | 2,048      | **22,460,938** | O(N log N) |
| KD-Tree Search (Single) | 100,000    | **0.047**      | O(log N)   |

_Note: KD-Tree achieves a throughput of over 21–22 million search queries per second for a tree with 100,000 elements._

### 4. Memory Bandwidth Limitations

Scaling brute-force algorithms with OpenMP (from 1 to 8 threads) showed minimal performance improvement in some cases. The bottleneck becomes memory bandwidth:

- `BM_RadiusSearch_AoSoA_BruteForce (1 Thread)`: 40.14 M/s (612 MiB/s)
- `BM_RadiusSearch_AoSoA_BruteForce (8 Threads)`: 43.15 M/s (658 MiB/s)

CPU cores stall waiting for data from RAM, confirming the workload is **memory bandwidth-bound**.
