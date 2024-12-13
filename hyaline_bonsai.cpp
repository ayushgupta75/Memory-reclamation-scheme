#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <iostream>
#include <random>

// Node structure with retirement handling
struct Node {
    int key;                   // Key for the Bonsai Tree
    std::atomic<int> refCount; // Reference counter
    Node* left;                // Left child
    Node* right;               // Right child
    Node* next;                // Pointer for Hyaline batch

    Node(int k) : key(k), refCount(0), left(nullptr), right(nullptr), next(nullptr) {}
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

class BonsaiTree {
public:
    BonsaiTree(Hyaline& hyaline, int numSlots) : root(nullptr), hyaline(hyaline), slotCount(numSlots) {}

    ~BonsaiTree() {
        deleteTree(root); // Automatically clean up the tree when the object is destroyed
    }

    void insert(int key, int slotId) {
        Node* handle = hyaline.enter(slotId);
        root = insertRec(root, key);
        hyaline.leave(slotId, handle);
    }

    void remove(int key, int slotId) {
        Node* handle = hyaline.enter(slotId);
        root = removeRec(root, key, slotId);
        hyaline.leave(slotId, handle);
    }

    void printInOrder() const {
        printRec(root);
        std::cout << std::endl;
    }

private:
    Node* root;
    Hyaline& hyaline;
    const int slotCount;

    void deleteTree(Node* node) {
        if (!node) return;
        deleteTree(node->left);
        deleteTree(node->right);
        delete node;
    }

    Node* insertRec(Node* node, int key) {
        if (!node) return new Node(key);
        if (key < node->key)
            node->left = insertRec(node->left, key);
        else if (key > node->key)
            node->right = insertRec(node->right, key);
        return node;
    }

    Node* removeRec(Node* node, int key, int slotId) {
        if (!node) return nullptr;

        if (key < node->key)
            node->left = removeRec(node->left, key, slotId);
        else if (key > node->key)
            node->right = removeRec(node->right, key, slotId);
        else {
            if (!node->left) {
                Node* rightChild = node->right;
                hyaline.retire(node, slotId);
                return rightChild;
            } else if (!node->right) {
                Node* leftChild = node->left;
                hyaline.retire(node, slotId);
                return leftChild;
            }

            Node* successor = minValueNode(node->right);
            node->key = successor->key;
            node->right = removeRec(node->right, successor->key, slotId);
        }
        return node;
    }

    Node* minValueNode(Node* node) const {
        Node* current = node;
        while (current && current->left)
            current = current->left;
        return current;
    }

    void printRec(Node* node) const {
        if (!node) return;
        printRec(node->left);
        std::cout << node->key << " ";
        printRec(node->right);
    }
};

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
    BonsaiTree tree(hyaline, threads);

    std::vector<std::thread> workers;

    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&tree, i, objects, threads]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, objects);

            for (int j = 0; j < objects / threads; ++j) {
                int key = dis(gen);
                tree.insert(key, i);
                if (j % 3 == 0) tree.remove(key, i);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    // Clean up Hyaline slots
    for (int i = 0; i < threads; ++i) {
        while (true) {
            Node* handle = hyaline.enter(i);
            if (!handle) break; // Slot fully cleaned
            hyaline.leave(i, handle);
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    double throughput = static_cast<double>(objects) / elapsed.count();
    std::cout << "Threads: " << threads << " | Throughput: " << throughput << " ops/sec" << std::endl;
    return 0;
}
