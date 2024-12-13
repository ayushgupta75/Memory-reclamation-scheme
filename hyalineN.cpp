#include <iostream>
#include <atomic>
#include <vector>
#include <thread>
#include <shared_mutex>
#include <unordered_map>
#include <chrono>

// Simplified Node structure for NatarajanTree
struct Node {
    int key;
    Node* left;
    Node* right;
    std::atomic<int> ref_count; // Used for reference counting

    Node(int k) : key(k), left(nullptr), right(nullptr), ref_count(0) {}
};

// Simplified Hyaline structure
class Hyaline {
    std::unordered_map<std::thread::id, Node*> retired_nodes;
    std::shared_mutex mutex;

public:
    void retire(Node* node) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        retired_nodes[std::this_thread::get_id()] = node;
    }

    void reclaim() {
        std::unique_lock<std::shared_mutex> lock(mutex);
        for (auto it = retired_nodes.begin(); it != retired_nodes.end();) {
            Node* node = it->second;
            if (node->ref_count == 0) {
                delete node;
                it = retired_nodes.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// Simplified NatarajanTree
class NatarajanTree {
    Node* root;
    Hyaline hyaline;

public:
    NatarajanTree() : root(nullptr) {}

    void insert(int key) {
        Node* new_node = new Node(key);
        if (!root) {
            root = new_node;
            return;
        }

        Node* current = root;
        while (true) {
            if (key < current->key) {
                if (!current->left) {
                    current->left = new_node;
                    return;
                }
                current = current->left;
            } else {
                if (!current->right) {
                    current->right = new_node;
                    return;
                }
                current = current->right;
            }
        }
    }

    void remove(int key) {
        Node* parent = nullptr;
        Node* current = root;

        while (current && current->key != key) {
            parent = current;
            if (key < current->key) {
                current = current->left;
            } else {
                current = current->right;
            }
        }

        if (!current) return; // Key not found

        if (!current->left && !current->right) {
            if (parent) {
                if (parent->left == current) parent->left = nullptr;
                if (parent->right == current) parent->right = nullptr;
            } else {
                root = nullptr;
            }
        } else if (current->left && current->right) {
            Node* successor = current->right;
            Node* successor_parent = current;

            while (successor->left) {
                successor_parent = successor;
                successor = successor->left;
            }

            current->key = successor->key;
            if (successor_parent->left == successor) {
                successor_parent->left = successor->right;
            } else {
                successor_parent->right = successor->right;
            }

            current = successor;
        } else {
            Node* child = (current->left) ? current->left : current->right;
            if (parent) {
                if (parent->left == current) parent->left = child;
                if (parent->right == current) parent->right = child;
            } else {
                root = child;
            }
        }

        hyaline.retire(current);
    }

    bool find(int key) {
        Node* current = root;
        while (current) {
            if (key == current->key) return true;
            current = (key < current->key) ? current->left : current->right;
        }
        return false;
    }

    void reclaim_memory() {
        hyaline.reclaim();
    }
};

// Benchmark function
void benchmark(NatarajanTree& tree, int thread_count, int operations) {
    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&tree, operations]() {
            for (int j = 0; j < operations; ++j) {
                tree.insert(j);
                tree.remove(j);
            }
        });
    }

    for (auto& t : threads) t.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    double throughput = (thread_count * operations) / elapsed.count();
    std::cout << "Throughput: " << throughput << " operations/second\n";

    tree.reclaim_memory();
}

int main() {
    NatarajanTree tree;
    int t = 4;
//    for (int t = 1; t <= 16; t *= 2) {
    std::cout << "Running benchmark with " << t << " threads..." << std::endl;
    benchmark(tree, t, 1000);
    std::cout << "Benchmark completed for " << t << " threads." << std::endl;
//    }

    return 0;
}
