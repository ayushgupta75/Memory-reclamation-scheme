#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <queue>

// Define a basic structure for bounding boxes
struct BoundingBox {
    double x_min, y_min, z_min;
    double x_max, y_max, z_max;

    BoundingBox(double xmin, double ymin, double zmin, double xmax, double ymax, double zmax)
        : x_min(xmin), y_min(ymin), z_min(zmin), x_max(xmax), y_max(ymax), z_max(zmax) {}

    // Check if two bounding boxes intersect
    bool intersects(const BoundingBox& other) const {
        return !(x_max < other.x_min || x_min > other.x_max ||
                 y_max < other.y_min || y_min > other.y_max ||
                 z_max < other.z_min || z_min > other.z_max);
    }
};

// BonsaiTree Node
struct BonsaiNode {
    BoundingBox box;
    std::vector<BonsaiNode*> children;

    BonsaiNode(const BoundingBox& b) : box(b) {}

    ~BonsaiNode() {
        for (auto child : children) {
            delete child;
        }
    }

    void addChild(BonsaiNode* child) {
        children.push_back(child);
    }
};

// BonsaiTree Class
class BonsaiTree {
private:
    BonsaiNode* root;

public:
    BonsaiTree(const BoundingBox& rootBox) {
        root = new BonsaiNode(rootBox);
    }

    ~BonsaiTree() {
        delete root;
    }

    void insert(const BoundingBox& box) {
        BonsaiNode* node = root;
        std::queue<BonsaiNode*> queue;
        queue.push(node);

        while (!queue.empty()) {
            BonsaiNode* current = queue.front();
            queue.pop();

            if (current->box.intersects(box)) {
                // Check if children exist, if not add directly
                if (current->children.empty()) {
                    current->addChild(new BonsaiNode(box));
                } else {
                    for (auto child : current->children) {
                        queue.push(child);
                    }
                }
            }
        }
    }

    void printTree() const {
        printNode(root, 0);
    }

private:
    void printNode(BonsaiNode* node, int depth) const {
        if (!node) return;
        for (int i = 0; i < depth; ++i) std::cout << "  ";
        std::cout << "BoundingBox: [" << node->box.x_min << ", " << node->box.y_min << ", " << node->box.z_min << "] - ["
                  << node->box.x_max << ", " << node->box.y_max << ", " << node->box.z_max << "]\n";
        for (auto child : node->children) {
            printNode(child, depth + 1);
        }
    }
};

// Benchmark Function
void benchmarkIBR(int numBoxes) {
    BoundingBox rootBox(0, 0, 0, 100, 100, 100);
    BonsaiTree tree(rootBox);

    // Generate random bounding boxes
    std::vector<BoundingBox> boxes;
    for (int i = 0; i < numBoxes; ++i) {
        double x_min = rand() % 100;
        double y_min = rand() % 100;
        double z_min = rand() % 100;
        double x_max = x_min + (rand() % 10 + 1);
        double y_max = y_min + (rand() % 10 + 1);
        double z_max = z_min + (rand() % 10 + 1);
        boxes.emplace_back(x_min, y_min, z_min, x_max, y_max, z_max);
    }

    // Measure insertion time
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& box : boxes) {
        tree.insert(box);
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Inserted " << numBoxes << " boxes in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms\n";

    // Optionally print the tree structure
    // tree.printTree();
}

int main() {
    int numBoxes = 1000; // Number of bounding boxes for the benchmark
    benchmarkIBR(numBoxes);
    return 0;
}