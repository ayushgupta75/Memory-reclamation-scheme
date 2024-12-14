# Memory-reclamation-scheme
Safe Memory Reclamation Schemes

This README provides an overview of the combined implementation of Interval-Based Reclamation (IBR)
and Hyaline for efficient memory management in lock-free, non-blocking concurrent data structures.
These approaches ensure safe and robust memory reclamation while optimizing throughput and memory usage.

Overview

Interval-Based Reclamation (IBR)

IBR introduces a lightweight and efficient method for reclaiming memory blocks in non-blocking 
data structures. By associating global and thread-local epochs with memory allocations, IBR 
allows threads to track memory usage within finite intervals, avoiding unbounded memory reservation.
Key advantages include:

Low runtime overhead.

Robustness against stalled threads.

Non-blocking, lock-free operations.

Easy integration with C++ data structures.

Hyaline

Hyaline complements IBR by providing flexible and scalable memory management strategies. It enables:

Optimized reclamation frequency through adaptive thresholds.

Reduced contention for memory reclamation operations.

Integration with diverse concurrent workloads, accommodating varied read-write ratios.

By combining these two approaches, this implementation achieves high performance and memory safety
across a wide range of concurrent applications.

Requirements

C++ Compiler: GCC or Clang with C++17 support or later.

Operating System: POSIX-compliant (e.g., Linux, macOS).

Dependencies: Standard C++ libraries; no additional external dependencies.

Running

Compile the code:
g++ -std=c++17 -O3 -pthread -o hyaline hyaline.cpp

Run the program:

	We have 4 different executables, one for each combination between benchmark and memory reclamation scheme. The four are:

	hyaline_bonsai
	hyaline_sgl
	ibr_bonsai
	ibr_sgl

To get results, we ran them this way:

./hyaline_bonsai #of_threads

Example: ./hyaline_bonsai 16
	This would run the benchmark for hyaline with a bonsai tree with 16 threads

This gives our throughput metric, but the following is how we got our metric on unreclaimed memory blocks.

For this we run our code using valgrind

Example: valgrind ./hyaline_bonsai 16
	This would run the benchmark for hyaline with a bonsai tree with 16 threads through valgrind, and would give the number of unreclaimed memory blocks