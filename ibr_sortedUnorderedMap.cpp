#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <list>
#include <unordered_map>
#include <map>
#include <random>

// Memory management API for IBR
class IBRManager {
public:
    struct Node {
        int key;
        int value;
        std::atomic<int> birth_epoch{0};
        std::atomic<int> retire_epoch{-1};
    };

    static std::atomic<int> global_epoch;
    thread_local static int local_epoch;
    thread_local static std::list<Node*> retired_nodes;

    static void start_op() {
        local_epoch = global_epoch.load();
    }

    static void end_op() {
        local_epoch = -1;  // Reset epoch
    }

    static Node* allocate_node(int key, int value) {
        Node* node = new Node();
        node->key = key;
        node->value = value;
        node->birth_epoch = global_epoch.load();
        return node;
    }

    static void retire_node(Node* node) {
        node->retire_epoch = global_epoch.load();
        retired_nodes.push_back(node);
        clean_up();
    }

    static void clean_up() {
        for (auto it = retired_nodes.begin(); it != retired_nodes.end();) {
            Node* node = *it;
            if (node->retire_epoch.load() < get_min_active_epoch()) {
                delete node;
                it = retired_nodes.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    static int get_min_active_epoch() {
        // Placeholder for the actual implementation to find the minimum active epoch.
        return global_epoch.load() - 2;  // Example heuristic.
    }
};

std::atomic<int> IBRManager::global_epoch{0};
thread_local int IBRManager::local_epoch = -1;
thread_local std::list<IBRManager::Node*> IBRManager::retired_nodes;

// Lock-free SortedUnorderedMap Implementation
class SortedUnorderedMap {
public:
    void insert(int key, int value) {
        IBRManager::start_op();
        std::lock_guard<std::mutex> lock(map_mutex);
        map[key] = value;
        IBRManager::end_op();
    }

    void remove(int key) {
        IBRManager::start_op();
        std::lock_guard<std::mutex> lock(map_mutex);
        map.erase(key);
        IBRManager::end_op();
    }

    int find(int key) {
        IBRManager::start_op();
        std::lock_guard<std::mutex> lock(map_mutex);
        auto it = map.find(key);
        IBRManager::end_op();
        return (it != map.end()) ? it->second : -1;
    }

private:
    std::map<int, int> map;
    std::mutex map_mutex;
};

// Benchmarking
void benchmark(int thread_count, int total_operations) {
    SortedUnorderedMap sorted_map;
    std::atomic<int> operation_count{0};
    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 1000);

            while (operation_count.load() < total_operations) {
                int key = dist(rng);
                sorted_map.insert(key, dist(rng));
                sorted_map.remove(key);
                operation_count.fetch_add(2);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    double throughput = static_cast<double>(total_operations) / elapsed.count();
    std::cout << "Threads: " << thread_count << " | Throughput: " << throughput << " ops/sec" << std::endl;
}

int main() {
    int thread_counts[] = {1, 2, 4, 8, 16};
    int total_operations = 1000000; // Define total number of operations

    for (int thread_count : thread_counts) {
        benchmark(thread_count, total_operations);
    }
    return 0;
}