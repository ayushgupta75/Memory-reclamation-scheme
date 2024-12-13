#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <limits>
#include <list>

class IBRManager {
public:
    struct Node {
        int key;
        std::atomic<Node*> left;
        std::atomic<Node*> right;
        std::atomic<int> birth_epoch;
        std::atomic<int> retire_epoch;

        Node(int k) : key(k), left(nullptr), right(nullptr), birth_epoch(0), retire_epoch(-1) {}
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

class NatarajanTree {
private:
    using Node = IBRManager::Node;

    Node* root;

    static bool compare_and_set(std::atomic<Node*>& ref, Node* expected, Node* desired) {
        return ref.compare_exchange_weak(expected, desired, std::memory_order_release, std::memory_order_relaxed);
    }

public:
    NatarajanTree() {
        root = new Node(std::numeric_limits<int>::max()); // Sentinel root node
    }

    ~NatarajanTree() {
        clear(root);
    }

    void insert(int key) {
        while (true) {
            IBRManager::start_op();
            Node* parent = nullptr;
            Node* current = root;

            while (current != nullptr) {
                parent = current;
                if (key < current->key) {
                    current = current->left.load();
                } else if (key > current->key) {
                    current = current->right.load();
                } else {
                    IBRManager::end_op();
                    return; // Key already exists
                }
            }

            Node* new_node = new Node(key);
            new_node->birth_epoch = IBRManager::global_epoch.load();
            if (key < parent->key) {
                if (compare_and_set(parent->left, nullptr, new_node)) {
                    IBRManager::end_op();
                    return;
                }
            } else {
                if (compare_and_set(parent->right, nullptr, new_node)) {
                    IBRManager::end_op();
                    return;
                }
            }

            delete new_node; // CAS failed, retry
            IBRManager::end_op();
        }
    }

    bool remove(int key) {
        while (true) {
            IBRManager::start_op();
            Node* parent = nullptr;
            Node* current = root;
            Node* target = nullptr;

            while (current != nullptr) {
                if (key < current->key) {
                    parent = current;
                    current = current->left.load();
                } else if (key > current->key) {
                    parent = current;
                    current = current->right.load();
                } else {
                    target = current;
                    break;
                }
            }

            if (!target) {
                IBRManager::end_op();
                return false; // Key not found
            }

            Node* left = target->left.load();
            Node* right = target->right.load();

            if (left && right) {
                // Two children: find the in-order successor
                Node* successor_parent = target;
                Node* successor = target->right.load();
                while (successor->left.load()) {
                    successor_parent = successor;
                    successor = successor->left.load();
                }
                target->key = successor->key; // Copy successor's key
                parent = successor_parent;
                target = successor;
            }

            Node* child = target->left ? target->left.load() : target->right.load();
            if (parent->left.load() == target) {
                if (compare_and_set(parent->left, target, child)) {
                    IBRManager::retire_node(target);
                    IBRManager::end_op();
                    return true;
                }
            } else {
                if (compare_and_set(parent->right, target, child)) {
                    IBRManager::retire_node(target);
                    IBRManager::end_op();
                    return true;
                }
            }
        }
    }

    bool find(int key) {
        IBRManager::start_op();
        Node* current = root;
        while (current != nullptr) {
            if (key < current->key) {
                current = current->left.load();
            } else if (key > current->key) {
                current = current->right.load();
            } else {
                IBRManager::end_op();
                return true; // Key found
            }
        }
        IBRManager::end_op();
        return false; // Key not found
    }

private:
    void clear(Node* node) {
        if (node == nullptr) return;
        clear(node->left.load());
        clear(node->right.load());
        delete node;
    }
};

// Benchmarking
void benchmark(int thread_count, int total_operations) {
    NatarajanTree tree;
    std::atomic<int> operation_count{0};
    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 1000);

            while (operation_count.load() < total_operations) {
                int key = dist(rng);
                tree.insert(key);
                tree.remove(key);
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
    int total_operations = 10000; // Define total number of operations

    for (int thread_count : thread_counts) {
        benchmark(thread_count, total_operations);
    }
    return 0;
}