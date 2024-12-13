#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <thread>
#include <chrono>

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

// Test Node Allocation and Benchmark
void benchmark_thread(int slot, int num_operations) {
    Node* handle = enter(slot);

    for (int i = 0; i < num_operations; ++i) {
        // Allocate a new node
        Node* node = new Node(new int(i));

        // Retire the node (simulate deletion)
        retire(slot, node);
    }

    leave(slot, handle);
}

int main() {
    initialize_hyaline();

    constexpr int num_threads = 8;
    constexpr int num_operations = 1000;
    std::vector<std::thread> threads;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(benchmark_thread, i % MAX_SLOTS, num_operations);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;

    double throughput = (num_threads * num_operations) / duration.count();
    std::cout << "Benchmark complete. Throughput: " << throughput << " operations per second." << std::endl;

    return 0;
}
