#include <atomic>
#include <unordered_map>
#include <vector>
#include <thread>
#include <iostream>
#include <chrono>
#include <mutex>
#include <random>

constexpr int MAX_THREADS = 144;
constexpr int NUM_OPERATIONS = 10000;

// Define Node structure
struct Node {
    int key;
    int value;
    std::atomic<int> nRef{0};         // Reference counter
    int birthEra;                     // Birth era for robustness

    Node(int k, int v) : key(k), value(v), birthEra(0) {}
};

// Define Batch structure for memory reclamation
struct Batch {
    std::vector<Node*> nodes;         // Nodes in the batch
    std::atomic<int> refCounter{0};   // Reference counter for the batch
    int minBirthEra;                  // Minimum birth era in the batch

    Batch() : minBirthEra(0) {}
};

// Global variables for Hyaline-S
std::atomic<int> globalEra{0};
std::vector<std::atomic<int>> slotRefs(MAX_THREADS);

// Hyaline-S API functions
void enter(int slot) {
    slotRefs[slot].fetch_add(1, std::memory_order_relaxed);
}

void leave(int slot) {
    slotRefs[slot].fetch_sub(1, std::memory_order_relaxed);
}

Node* deref(int slot, Node** ptr) {
    int era = globalEra.load(std::memory_order_acquire);
    Node* node = *ptr;
    return (slotRefs[slot].load(std::memory_order_relaxed) >= era) ? node : nullptr;
}

void retire(Batch& batch) {
    for (auto* node : batch.nodes) {
        int ref = batch.refCounter.fetch_sub(1, std::memory_order_acq_rel);
        if (ref == 0) {
            delete node; // Reclaim memory
        }
    }
}

// SGLUnorderedMap class
class SGLUnorderedMap {
    std::unordered_map<int, int> map;
    std::mutex mtx;

public:
    void insert(int key, int value, int slot) {
        enter(slot);
        {
            std::lock_guard<std::mutex> lock(mtx);
            map[key] = value;
        }
        leave(slot);
    }

    bool find(int key, int& value, int slot) {
        enter(slot);
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = map.find(key);
            if (it != map.end()) {
                value = it->second;
                leave(slot);
                return true;
            }
        }
        leave(slot);
        return false;
    }

    void erase(int key, int slot) {
        enter(slot);
        {
            std::lock_guard<std::mutex> lock(mtx);
            map.erase(key);
        }
        leave(slot);
    }
};

// Benchmark function
void benchmark(int numThreads, SGLUnorderedMap& map) {
    auto start = std::chrono::high_resolution_clock::now();
    int totalOps = 0;

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < NUM_OPERATIONS; ++i) {
                if (i % 3 == 0) {
                    map.insert(rand() % 1000, rand() % 1000, t);
                } else if (i % 3 == 1) {
                    int value;
                    map.find(rand() % 1000, value, t);
                } else {
                    map.erase(rand() % 1000, t);
                }
                totalOps++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double throughput = totalOps / duration.count();

    std::cout << "Throughput: " << throughput << " ops/s\n";
}

int main(int argc, char* argv[]) {
    int threads;
    if (argc == 2) {
        threads = std::stoi(argv[1]);
    }
    else {
        threads = 4;
    }
    std::cout << "The thread count is: " << threads << std::endl;
    SGLUnorderedMap map;

    benchmark(threads, map);

    return 0;
}
