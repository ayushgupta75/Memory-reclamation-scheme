#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <iostream>

// Node structure with retirement handling
struct Node {
    std::atomic<int> refCount; // Reference counter
    Node* next;                // Pointer to the next node in the batch

    Node() : refCount(0), next(nullptr) {}
};

// Slot structure for maintaining retirement lists
struct Slot {
    std::atomic<int> refCount; // Global reference counter for this slot
    std::atomic<Node*> head;  // Head of the retired list

    Slot() : refCount(0), head(nullptr) {}
};

class Hyaline {
public:
    Hyaline(int numSlots) : slots(numSlots), slotCount(numSlots) {}

    // Enter the critical section
    Node* enter(int slotId) {
        Slot& slot = slots[slotId];
        slot.refCount.fetch_add(1, std::memory_order_relaxed);
        return slot.head.load(std::memory_order_acquire);
    }

    // Leave the critical section
    void leave(int slotId, Node* handle) {
        Slot& slot = slots[slotId];
        auto head = slot.head.load(std::memory_order_acquire);
        auto ref = slot.refCount.fetch_sub(1, std::memory_order_release);

        if (ref == 1 && head) { // This thread may need to handle reclamation
            traverseAndReclaim(slot, head);
        }
    }

    // Retire a batch of nodes
    void retire(Node* batchHead, int slotId) {
        Slot& slot = slots[slotId];

        Node* prevHead = nullptr;
        do {
            prevHead = slot.head.load(std::memory_order_acquire);
            batchHead->next = prevHead;
        } while (!slot.head.compare_exchange_weak(prevHead, batchHead, std::memory_order_release));
    }

private:
    std::vector<Slot> slots;
    const int slotCount;

    // Traverse and reclaim retired nodes
    void traverseAndReclaim(Slot& slot, Node* handle) {
        Node* current = slot.head.load(std::memory_order_acquire);

        while (current && current != handle) {
            Node* next = current->next;
            if (current->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete current;
            }
            current = next;
        }

        // Reset the head to indicate completion
        if (slot.refCount.load(std::memory_order_acquire) == 0) {
            slot.head.store(nullptr, std::memory_order_release);
        }
    }
};
