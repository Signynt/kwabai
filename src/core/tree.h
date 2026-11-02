#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace kwabai {
using WindowId = std::uint64_t;
using NodeId = std::uint64_t;
enum class Axis { X, Y };
enum class Direction { North, East, South, West };
enum class ResizeHandle { Top, Left, Bottom, Right, TopLeft, TopRight, BottomRight, BottomLeft };
struct Rect { double x, y, width, height; };
struct Size { double width = 0, height = 0; };
struct Insets { double top = 10, right = 10, bottom = 10, left = 10; };
struct Node {
    NodeId id;
    Axis axis = Axis::X;
    double ratio = 0.5;
    std::unique_ptr<Node> first, second;
    std::vector<WindowId> windows;
    WindowId active = 0;
    bool leaf() const { return !first; }
};
struct Placement { WindowId window; NodeId leaf; Rect frame; bool visible; };
// Explicit records keep persistence separate from runtime compositor UUIDs.
struct Record {
    NodeId id, first = 0, second = 0;
    Axis axis = Axis::X;
    double ratio = 0.5;
    std::vector<WindowId> windows;
    WindowId active = 0;
};
struct Snapshot { unsigned version = 1; NodeId root = 0, nextNode = 1; std::vector<Record> nodes; };
class Tree {
public:
    Tree() = default;
    Tree(const Tree &);
    Tree &operator=(const Tree &);
    Tree(Tree &&) noexcept = default;
    Tree &operator=(Tree &&) noexcept = default;
    const Node *root() const { return root_.get(); }
    void add(WindowId window, WindowId target = 0, Direction direction = Direction::East, double ratio = 0.5);
    void remove(WindowId window);
    void swap(WindowId a, WindowId b);
    void stack(WindowId source, WindowId target);
    void warp(WindowId source, WindowId target, Direction direction, double ratio = 0.5);
    void activate(WindowId window);
    void balance(std::optional<Axis> axis=std::nullopt);
    void rotate(int clockwiseDegrees);
    void mirror(Axis axis);
    bool mirrorSlots(Axis axis);
    bool equalize(std::optional<Axis> axis=std::nullopt);
    void resize(WindowId window, Axis axis, double delta);
    bool resizeEdges(WindowId window, ResizeHandle handle, double dx, double dy, Rect area,
        Insets padding={}, double gap=10, const std::map<WindowId, Size> &minimums={});
    void setRatio(WindowId window, Axis axis, double ratio);
    bool adjustParentRatio(WindowId window, double value, bool relative);
    bool toggleParentSplit(WindowId window);
    std::vector<Placement> layout(Rect workArea, Insets padding = {}, double gap = 10,
        const std::map<WindowId, Size> &minimums = {}) const;
    Snapshot snapshot() const;
    static Tree restore(const Snapshot &);
    void validate() const;
private:
    std::unique_ptr<Node> root_;
    NodeId nextNode_ = 1;
    Node *find(WindowId window) const;
    Node *nearestSplit(WindowId window, Axis axis) const;
    std::unique_ptr<Node> &slot(Node *node);
};
}
