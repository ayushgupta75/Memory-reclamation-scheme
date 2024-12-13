#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <shared_mutex>
#include <atomic>

// Bonsai Tree Node
struct BonsaiNode {
    int key;
    BonsaiNode* left;
    BonsaiNode* right;

    BonsaiNode(int k) : key(k), left(nullptr), right(nullptr) {}
};

// Bonsai Tree Class
class BonsaiTree {
private:
    BonsaiNode* root;
    std::shared_mutex tree_mutex;

    void insert(BonsaiNode*& node, int key) {
        if (!node) {
            node = new BonsaiNode(key);
            return;
        }
        if (key < node->key) {
            insert(node->left, key);
        } else {
            insert(node->right, key);
        }
    }

    bool find(BonsaiNode* node, int key) {
        if (!node) return false;
        if (node->key == key) return true;
        if (key < node->key) return find(node->left, key);
        return find(node->right, key);
    }

public:
    BonsaiTree() : root(nullptr) {}

    void insert(int key) {
        std::unique_lock lock(tree_mutex);
        insert(root, key);
    }

    bool find(int key) {
        std::shared_lock lock(tree_mutex);
        return find(root, key);
    }
};

// Hyaline Schema
class Hyaline {
private:
    BonsaiTree tree;
    std::vector<int> values;
    std::shared_mutex values_mutex;
    std::atomic<int> processed_count{0};

public:
    void addValue(int value) {
        std::unique_lock lock(values_mutex);
        values.push_back(value);
    }

    void processValues() {
        std::unique_lock lock(values_mutex);
        for (int value : values) {
            int transformed = value * 2; // Example transformation
            tree.insert(transformed);
            processed_count.fetch_add(1, std::memory_order_relaxed);
        }
        values.clear();
    }

    bool checkValue(int value) {
        return tree.find(value);
    }

    int getProcessedCount() const {
        return processed_count.load(std::memory_order_relaxed);
    }
};

// Benchmark Function
void benchmark(Hyaline& hyaline, int thread_count, int operations) {
    auto task = [&hyaline, operations](int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1, 1000);

        for (int i = 0; i < operations; ++i) {
            int value = dist(gen);
            hyaline.addValue(value);
        }

        hyaline.processValues();
    };

    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(task, i);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    int total_processed = hyaline.getProcessedCount();
    double throughput = total_processed / elapsed.count();

    std::cout << "Benchmark completed in " << elapsed.count() << " seconds with "
              << thread_count << " threads." << std::endl;
    std::cout << "Total processed: " << total_processed << ", Throughput: "
              << throughput << " operations/second." << std::endl;
}

int main() {
    Hyaline hyaline;

    int thread_count = 4;
    int operations_per_thread = 1000;

    benchmark(hyaline, thread_count, operations_per_thread);

    return 0;
}
