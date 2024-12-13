#include <atomic>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>

constexpr int MAX_THREADS = 144;

struct Node {
    int key;
    std::atomic<Node*> next{nullptr}; // Pointer to the next node
    Node(int k) : key(k) {}
};

class BonsaiTree {
    std::atomic<Node*> root{nullptr};

public:
    void insert(int key) {
        Node* newNode = new Node(key);
        Node* expected = nullptr;

        while (true) {
            Node* current = root.load();
            if (!current) {
                if (root.compare_exchange_weak(expected, newNode)) {
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
                delete newNode; // Duplicate keys are not allowed
                break;
            }
        }
    }

    Node* find(int key) {
        Node* current = root.load();
        while (current) {
            if (current->key == key) {
                return current;
            }
            current = current->next.load();
        }
        return nullptr;
    }

    void cleanup() {
        Node* current = root.load();
        while (current) {
            Node* next = current->next.load();
            delete current;
            current = next;
        }
    }
};

void benchmark(int numThreads, BonsaiTree& tree, int numOperations) {
    auto start = std::chrono::high_resolution_clock::now();
    int totalOps = 0;

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < numOperations; ++i) {
                int key = rand() % 1000;
                if (i % 2 == 0) {
                    tree.insert(key);
                } else {
                    tree.find(key);
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

    std::cout << "Threads: " << numThreads
              << ", Time: " << duration.count() << "s"
              << ", Throughput: " << throughput << " ops/s\n";
}

int main() {
    BonsaiTree tree;
    int numOperations = 10000; // Define number of operations

    for (int threads = 1; threads <= MAX_THREADS; threads *= 2) {
        benchmark(threads, tree, numOperations);
    }

    tree.cleanup(); // Free all remaining nodes

    return 0;
}