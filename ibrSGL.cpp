#include <iostream>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <list>
#include <mutex>

class IBRManager {
public:
    struct Node {
        int key;
        int value;
        std::atomic<int> birth_epoch;
        std::atomic<int> retire_epoch;

        Node(int k, int v) : key(k), value(v), birth_epoch(0), retire_epoch(-1) {}
    };

    static std::atomic<int> global_epoch;
    thread_local static int local_epoch;
    thread_local static std::list<Node*> retired_nodes;

    static void start_op() {
        local_epoch = global_epoch.load();
    }

    static void end_op() {
        local_epoch = -1;
    }

    static void retire_node(Node* node) {
        node->retire_epoch = global_epoch.load();
        retired_nodes.push_back(node);
        clean_up();
    }

    static void clean_up() {
        for (auto it = retired_nodes.begin(); it != retired_nodes.end();) {
            Node* node = *it;
            if (node->retire_epoch < get_min_active_epoch()) {
                delete node;
                it = retired_nodes.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    static int get_min_active_epoch() {
        // Placeholder for finding the minimum active epoch.
        return global_epoch.load() - 2; // Example heuristic
    }
};

std::atomic<int> IBRManager::global_epoch{0};
thread_local int IBRManager::local_epoch = -1;
thread_local std::list<IBRManager::Node*> IBRManager::retired_nodes;

class SGLUnorderedMap {
private:
    std::unordered_map<int, IBRManager::Node*> map;
    std::mutex global_lock;

public:
    void insert(int key, int value) {
        IBRManager::start_op();
        std::lock_guard<std::mutex> lock(global_lock);

        auto it = map.find(key);
        if (it != map.end()) {
            IBRManager::retire_node(it->second);
        }

        map[key] = new IBRManager::Node(key, value);
        map[key]->birth_epoch = IBRManager::global_epoch.load();
        IBRManager::end_op();
    }

    bool remove(int key) {
        IBRManager::start_op();
        std::lock_guard<std::mutex> lock(global_lock);

        auto it = map.find(key);
        if (it != map.end()) {
            IBRManager::retire_node(it->second);
            map.erase(it);
            IBRManager::end_op();
            return true;
        }

        IBRManager::end_op();
        return false;
    }

    bool find(int key) {
        IBRManager::start_op();
        std::lock_guard<std::mutex> lock(global_lock);

        bool found = map.find(key) != map.end();
        IBRManager::end_op();
        return found;
    }
};

// Benchmarking
void benchmark(int thread_count, int total_operations) {
    SGLUnorderedMap sgl_map;
    std::atomic<int> operation_count{0};
    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 1000);

            while (operation_count.load() < total_operations) {
                int key = dist(rng);
                sgl_map.insert(key, dist(rng));
                sgl_map.remove(key);
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

int main(int argc, char* argv[]) {
    int thread_count;
    if (argc == 2) {
        thread_count = std::stoi(argv[1]);
    }
    else {
        thread_count = 4;
    }
    std::cout << "The thread count is: " << thread_count << std::endl;
    int total_operations = 10000; // Define total number of operations

    benchmark(thread_count, total_operations);
    return 0;
}
