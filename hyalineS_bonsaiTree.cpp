#include <atomic>
#include <vector>
#include <thread>
#include <iostream>
#include <chrono>
#include <random>

// Define Node structure
struct Node {
    int key;
    std::atomic<Node*> next{nullptr}; // Pointer to the next node
    std::atomic<int> nRef{0};         // Reference counter
    int birthEra;                     // Birth era for robustness

    Node(int k) : key(k), birthEra(0) {}
};

// Define Batch structure for memory reclamation
struct Batch {
    std::vector<Node*> nodes;         // Nodes in the batch
    std::atomic<int> refCounter{0};   // Reference counter for the batch
    int minBirthEra;                  // Minimum birth era in the batch

    Batch() : minBirthEra(0) {}
};

// Global variables for Hyaline-S
constexpr int MAX_THREADS = 144;
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

// BonsaiTree class
class BonsaiTree {
    std::atomic<Node*> root;

public:
    BonsaiTree() : root(nullptr) {}

    void insert(int key, int slot) {
        enter(slot);
        Node* newNode = new Node(key);
        Node* current = root.load();

        while (true) {
            if (!current) {
                if (root.compare_exchange_weak(current, newNode)) {
                    break;
                }
            } else if (key < current->key) {
                Node* next = current->next.load();
                if (!next) {
                    if (current->next.compare_exchange_weak(next, newNode)) {
                        break;
                    }
                } else {
                    current = next;
                }
            } else {
                break; // Duplicate keys are not allowed
            }
        }
        leave(slot);
    }

    Node* find(int key, int slot) {
        enter(slot);
        Node* current = root.load();
        while (current) {
            if (current->key == key) {
                leave(slot);
                return current;
            }
            current = current->next.load();
        }
        leave(slot);
        return nullptr;
    }
};

// Benchmark function
void benchmark(int numThreads, BonsaiTree& tree) {
    auto start = std::chrono::high_resolution_clock::now();
    int totalOps = 10000 * numThreads;

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 10000; ++i) {
                if (i % 2 == 0) {
                    tree.insert(rand() % 1000, t);
                } else {
                    tree.find(rand() % 1000, t);
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
    BonsaiTree tree;

    benchmark(threads, tree);

    return 0;
}
