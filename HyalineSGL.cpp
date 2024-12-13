#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
#include <random>
#include <unordered_map>
#include <optional>
#include <cassert>

// Slot structure for maintaining retirement lists
struct Slot {
    std::atomic<int> refCount; // Global reference counter for this slot
    std::atomic<void*> head;  // Head of the retired list

    Slot() : refCount(0), head(nullptr) {}
};

class Hyaline {
public:
    Hyaline(int numSlots) : slots(numSlots), slotCount(numSlots) {}

    // Enter the critical section
    void* enter(int slotId) {
        Slot& slot = slots[slotId];
        slot.refCount.fetch_add(1, std::memory_order_relaxed);
        return slot.head.load(std::memory_order_acquire);
    }

    // Leave the critical section
    void leave(int slotId, void* handle) {
        Slot& slot = slots[slotId];
        auto head = slot.head.load(std::memory_order_acquire);
        auto ref = slot.refCount.fetch_sub(1, std::memory_order_release);

        if (ref == 1 && head) { // This thread may need to handle reclamation
            traverseAndReclaim(slot, head);
        }
    }

    // Retire a batch of objects
    void retire(void* batchHead, int slotId) {
        Slot& slot = slots[slotId];

        void* prevHead = nullptr;
        do {
            prevHead = slot.head.load(std::memory_order_acquire);
            *reinterpret_cast<void**>(batchHead) = prevHead;
        } while (!slot.head.compare_exchange_weak(prevHead, batchHead, std::memory_order_release));
    }

private:
    std::vector<Slot> slots;
    const int slotCount;

    // Traverse and reclaim retired objects
    void traverseAndReclaim(Slot& slot, void* handle) {
        void* current = slot.head.load(std::memory_order_acquire);

        while (current && current != handle) {
            void* next = *reinterpret_cast<void**>(current);
            delete reinterpret_cast<char*>(current); // Assuming objects are dynamically allocated
            current = next;
        }

        // Reset the head to indicate completion
        if (slot.refCount.load(std::memory_order_acquire) == 0) {
            slot.head.store(nullptr, std::memory_order_release);
        }
    }
};

// SGLUnorderedMap Implementation

template <class K, class V>
class SGLUnorderedMap {
private:
    // Simple test and set lock
    inline void lockAcquire(int tid) {
        int unlk = -1;
        while (!lk.compare_exchange_strong(unlk, tid, std::memory_order_acq_rel)) {
            unlk = -1; // compare_exchange puts the old value into unlk, so set it back
        }
        assert(lk.load() == tid);
    }

    inline void lockRelease(int tid) {
        assert(lk == tid);
        int unlk = -1;
        lk.store(unlk, std::memory_order_release);
    }

    std::unordered_map<K, V>* m = nullptr;
    std::atomic<int> lk;

public:
    SGLUnorderedMap() {
        m = new std::unordered_map<K, V>();
        lk.store(-1, std::memory_order_release);
    }

    ~SGLUnorderedMap() {
        delete m;
    }

    bool insert(K key, V val, int tid) {
        lockAcquire(tid);
        auto v = m->emplace(key, val);
        lockRelease(tid);
        return v.second;
    }

    std::optional<V> put(K key, V val, int tid) {
        std::optional<V> res = {};
        lockAcquire(tid);
        auto it = m->find(key);
        if (it != m->end()) {
            res = it->second;
        }
        (*m)[key] = val;
        lockRelease(tid);
        return res;
    }

    std::optional<V> replace(K key, V val, int tid) {
        std::optional<V> res = {};
        lockAcquire(tid);
        auto v = m->find(key);
        if (v != m->end()) {
            res = v->second;
            (*m)[key] = val;
        }
        lockRelease(tid);
        return res;
    }

    std::optional<V> remove(K key, int tid) {
        std::optional<V> res = {};
        lockAcquire(tid);
        auto v = m->find(key);
        if (v != m->end()) {
            res = v->second;
            m->erase(key);
        }
        lockRelease(tid);
        return res;
    }

    std::optional<V> get(K key, int tid) {
        std::optional<V> res = {};
        lockAcquire(tid);
        auto v = m->find(key);
        if (v != m->end()) {
            res = v->second;
        }
        lockRelease(tid);
        return res;
    }
};

// Example usage of SGLUnorderedMap with Hyaline
int main(int argc, char* argv[]) {
    int threads;
    if (argc == 2) {
        threads = std::stoi(argv[1]);
    }
    else {
        threads = 4;
    }
    std::cout << "The thread count is: " << threads << std::endl;
    const int objects = 10000; // Number of objects to operate on
    auto start_time = std::chrono::high_resolution_clock::now();
    Hyaline hyaline(threads);
    SGLUnorderedMap<int, int> map;

    std::vector<std::thread> workers;

    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&map, &hyaline, i, objects, threads]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, objects);

            for (int j = 0; j < objects / threads; ++j) {
                int key = dis(gen);
                int value = dis(gen);

                if (j % 2 == 0) {
                    map.insert(key, value, i);
                } else {
                    map.remove(key, i);
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    double throughput = static_cast<double>(objects) / elapsed.count();
    std::cout << "Threads: " << threads << " | Throughput: " << throughput << " ops/sec" << std::endl;
    return 0;
}
