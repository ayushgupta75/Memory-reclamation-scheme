#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <mutex>

// Node for Bonsai Tree
struct Node {
    int key;
    std::atomic<Node*> left;
    std::atomic<Node*> right;

    Node(int k) : key(k), left(nullptr), right(nullptr) {}
};

// Lock-free Bonsai Tree
class BonsaiTree {
public:
    BonsaiTree() : root(nullptr) {}

    void insert(int key) {
        Node* new_node = new Node(key);
        while (true) {
            Node* expected = root.load();
            if (expected == nullptr) {
                if (root.compare_exchange_strong(expected, new_node)) {
                    return;
                }
            } else {
                Node* parent = nullptr;
                Node* current = expected;
                while (current != nullptr) {
                    parent = current;
                    if (key < current->key) {
                        current = current->left.load();
                    } else {
                        current = current->right.load();
                    }
                }
                if (key < parent->key) {
                    if (parent->left.compare_exchange_strong(current, new_node)) {
                        return;
                    }
                } else {
                    if (parent->right.compare_exchange_strong(current, new_node)) {
                        return;
                    }
                }
            }
        }
    }

    bool search(int key) {
        Node* current = root.load();
        while (current != nullptr) {
            if (current->key == key) {
                return true;
            } else if (key < current->key) {
                current = current->left.load();
            } else {
                current = current->right.load();
            }
        }
        return false;
    }

private:
    std::atomic<Node*> root;
};

// Hyaline-S Schema: Key-Value store
struct HyalineSData {
    int key;
    std::string value;
};

// Benchmarking Hyaline-S with Bonsai Tree
void benchmark(int thread_count, int operation_count) {
    BonsaiTree tree;

    // Random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000000);

    std::atomic<int> completed_operations(0);
    std::mutex print_mutex;

    auto worker = [&](int ops) {
        for (int i = 0; i < ops; ++i) {
            int key = dis(gen);
            tree.insert(key);
            completed_operations.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker, operation_count / thread_count);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    double throughput = completed_operations.load() / elapsed.count();

    std::lock_guard<std::mutex> lock(print_mutex);
    std::cout << "Threads: " << thread_count
              << ", Operations: " << operation_count
              << ", Time: " << elapsed.count() << " seconds"
              << ", Throughput: " << throughput << " ops/sec" << std::endl;
}

int main() {
    int thread_count = 4;
    int operation_count = 100000;

    std::cout << "Starting benchmark for Hyaline-S with Bonsai Tree..." << std::endl;
    benchmark(thread_count, operation_count);

    return 0;
}
