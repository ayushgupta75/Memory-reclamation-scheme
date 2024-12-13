#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <thread>
#include <chrono>
#include <list>
#include <shared_mutex>
#include <optional>
#include <functional>

// A thread-safe implementation of an unordered map
template <typename Key, typename Value>
class SGLUnorderedMap {
private:
    struct Bucket {
        std::list<std::pair<Key, Value>> data;
        mutable std::shared_mutex mutex;

        std::optional<Value> find(const Key& key) const {
            std::shared_lock lock(mutex);
            for (const auto& kv : data) {
                if (kv.first == key) {
                    return kv.second;
                }
            }
            return std::nullopt;
        }

        void insert_or_assign(const Key& key, const Value& value) {
            std::unique_lock lock(mutex);
            for (auto& kv : data) {
                if (kv.first == key) {
                    kv.second = value;
                    return;
                }
            }
            data.emplace_back(key, value);
        }

        bool erase(const Key& key) {
            std::unique_lock lock(mutex);
            for (auto it = data.begin(); it != data.end(); ++it) {
                if (it->first == key) {
                    data.erase(it);
                    return true;
                }
            }
            return false;
        }
    };

    std::vector<Bucket> buckets;
    std::hash<Key> hasher;

    Bucket& get_bucket(const Key& key) {
        size_t index = hasher(key) % buckets.size();
        return buckets[index];
    }

    const Bucket& get_bucket(const Key& key) const {
        size_t index = hasher(key) % buckets.size();
        return buckets[index];
    }

public:
    explicit SGLUnorderedMap(size_t bucket_count = 16) : buckets(bucket_count) {}

    std::optional<Value> find(const Key& key) const {
        return get_bucket(key).find(key);
    }

    void insert_or_assign(const Key& key, const Value& value) {
        get_bucket(key).insert_or_assign(key, value);
    }

    bool erase(const Key& key) {
        return get_bucket(key).erase(key);
    }
};

// Hyaline Node Structure
struct Node {
    std::atomic<int> ref_count; // Reference counter
    Node* next;                // Pointer to the next node
    void* data;                // Node data

    Node(void* data) : ref_count(0), next(nullptr), data(data) {}
};

// Hyaline Global State
constexpr int MAX_SLOTS = 128;
std::atomic<int> global_refs[MAX_SLOTS];
std::atomic<Node*> head[MAX_SLOTS];

void initialize_hyaline() {
    for (int i = 0; i < MAX_SLOTS; ++i) {
        global_refs[i].store(0);
        head[i].store(nullptr);
    }
}

// Enter function to mark a thread's activity in a slot
Node* enter(int slot) {
    global_refs[slot].fetch_add(1, std::memory_order_relaxed);
    return head[slot].load(std::memory_order_acquire);
}

// Leave function to update references and perform reclamation
void leave(int slot, Node* handle) {
    Node* current = head[slot].load(std::memory_order_acquire);
    global_refs[slot].fetch_sub(1, std::memory_order_relaxed);

    while (current && current != handle) {
        Node* next = current->next;
        if (current->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete current; // Free memory when no references exist
        }
        current = next;
    }
}

// Retire a node
void retire(int slot, Node* node) {
    node->ref_count.store(global_refs[slot].load(std::memory_order_acquire), std::memory_order_relaxed);
    Node* old_head = head[slot].load(std::memory_order_relaxed);
    do {
        node->next = old_head;
    } while (!head[slot].compare_exchange_weak(old_head, node, std::memory_order_release, std::memory_order_relaxed));
}

// Helper function to perform traversal and clean up
void traverse(Node* start) {
    while (start) {
        Node* next = start->next;
        if (start->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete start; // Free memory
        }
        start = next;
    }
}

// Test with SGLUnorderedMap
void benchmark_thread(int slot, int num_operations, SGLUnorderedMap<int, int>& map) {
    Node* handle = enter(slot);

    for (int i = 0; i < num_operations; ++i) {
        int key = i;
        int value = i * 10;
        map.insert_or_assign(key, value); // Simulate insert or update operation

        if (i % 2 == 0) {
            map.erase(key); // Simulate delete operation for even keys
        }
    }

    leave(slot, handle);
}

int main() {
    initialize_hyaline();

    constexpr int num_threads = 8;
    constexpr int num_operations = 1000;
    std::vector<std::thread> threads;
    SGLUnorderedMap<int, int> map; // Initialize SGLUnorderedMap

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(benchmark_thread, i % MAX_SLOTS, num_operations, std::ref(map));
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;

    double throughput = (num_threads * num_operations) / duration.count();
    std::cout << "Hyaline Benchmark with SGLUnorderedMap complete. Throughput: " << throughput << " operations per second." << std::endl;

    return 0;
}
