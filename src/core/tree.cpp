#include "tree.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace kwabai {
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::invalid_argument(message); }
void ratioCheck(double r) { require(std::isfinite(r) && r >= 0.1 && r <= 0.9, "split ratio must be between 0.1 and 0.9"); }
void axisCheck(Axis a) { require(a == Axis::X || a == Axis::Y, "invalid split axis"); }
void directionCheck(Direction d) { require(d == Direction::North || d == Direction::South || d == Direction::East || d == Direction::West, "invalid insertion direction"); }
std::unique_ptr<Node> clone(const Node *n) {
    if (!n) return {};
    auto out = std::make_unique<Node>();
    out->id=n->id; out->axis=n->axis; out->ratio=n->ratio;
    out->windows=n->windows; out->active=n->active;
    out->first=clone(n->first.get()); out->second=clone(n->second.get());
    return out;
}
bool contains(const Node *n, WindowId w) {
    if (!n) return false;
    if (n->leaf()) return std::find(n->windows.begin(),n->windows.end(),w)!=n->windows.end();
    return contains(n->first.get(),w)||contains(n->second.get(),w);
}
}
Tree::Tree(const Tree &other):root_(clone(other.root())),nextNode_(other.nextNode_) {}
Tree &Tree::operator=(const Tree &other) { if (this != &other) { Tree copy(other); *this=std::move(copy); } return *this; }
Node *Tree::find(WindowId window) const {
    std::function<Node *(Node *)> visit = [&](Node *n)->Node * {
        if (!n) return nullptr;
        if (n->leaf()) return contains(n,window)?n:nullptr;
        if (auto *found=visit(n->first.get())) return found;
        return visit(n->second.get());
    };
    return visit(root_.get());
}
std::unique_ptr<Node> &Tree::slot(Node *node) {
    std::function<std::unique_ptr<Node> *(std::unique_ptr<Node> &)> visit = [&](auto &n)->std::unique_ptr<Node> * {
        if (n.get()==node) return &n;
        if (!n || n->leaf()) return nullptr;
        if (auto *found=visit(n->first)) return found;
        return visit(n->second);
    };
    auto *found=visit(root_); require(found,"node not in tree"); return *found;
}
void Tree::add(WindowId window, WindowId target, Direction direction, double ratio) {
    require(window && !find(window),"window ID must be unique and nonzero");
    ratioCheck(ratio); directionCheck(direction);
    require(nextNode_ < std::numeric_limits<NodeId>::max()-1,"node ID capacity exhausted");
    if (!root_) {
        require(!target,"target window not found");
        auto n=std::make_unique<Node>(); n->id=nextNode_++; n->windows={window}; n->active=window; root_=std::move(n); return;
    }
    auto *targetNode=target?find(target):root_.get();
    while (targetNode && !targetNode->leaf()) targetNode=targetNode->first.get();
    require(targetNode,"target window not found");
    std::size_t count=0; unsigned targetDepth=0;
    std::function<void(const Node *,unsigned)> inspect=[&](const Node *n,unsigned depth) {
        ++count; if(n==targetNode) targetDepth=depth;
        if(!n->leaf()) {inspect(n->first.get(),depth+1);inspect(n->second.get(),depth+1);}
    };
    inspect(root(),0);
    require(count+2<=4095 && targetDepth<128,"tree capacity or depth limit exceeded");
    auto leaf=std::make_unique<Node>(); leaf->id=nextNode_; leaf->windows={window}; leaf->active=window;
    auto split=std::make_unique<Node>(); split->id=nextNode_+1; split->ratio=ratio;
    split->axis=(direction==Direction::East || direction==Direction::West)?Axis::X:Axis::Y;
    auto &location=slot(targetNode);
    bool before=direction==Direction::North || direction==Direction::West;
    split->first=before?std::move(leaf):std::move(location);
    split->second=before?std::move(location):std::move(leaf);
    location=std::move(split); nextNode_+=2;
}
void Tree::remove(WindowId window) {
    auto *n=find(window); require(n,"window not found");
    std::erase(n->windows,window);
    if (!n->windows.empty()) { if(n->active==window) n->active=n->windows.front(); return; }
    std::function<void(std::unique_ptr<Node> &)> prune = [&](auto &p) {
        if (p->leaf()) { if(p->windows.empty()) p.reset(); return; }
        prune(p->first); prune(p->second);
        if (!p->first) { auto sibling=std::move(p->second); p=std::move(sibling); }
        else if (!p->second) { auto sibling=std::move(p->first); p=std::move(sibling); }
    };
    prune(root_);
}
void Tree::swap(WindowId a, WindowId b) {
    auto *na=find(a), *nb=find(b); require(na && nb,"window not found");
    if (a==b) return;
    auto ia=std::find(na->windows.begin(),na->windows.end(),a), ib=std::find(nb->windows.begin(),nb->windows.end(),b);
    std::iter_swap(ia,ib);
    if (na==nb) { if(na->active==a) na->active=b; else if(na->active==b) na->active=a; }
    else { if(na->active==a) na->active=b; if(nb->active==b) nb->active=a; }
}
void Tree::stack(WindowId source, WindowId target) {
    require(source!=target && find(source) && find(target),"stack requires two different existing windows");
    Tree candidate(*this); candidate.remove(source);
    auto *n=candidate.find(target); n->windows.push_back(source); n->active=source;
    *this=std::move(candidate);
}
void Tree::warp(WindowId source, WindowId target, Direction direction, double ratio) {
    require(source!=target && find(source) && find(target),"warp requires two different existing windows");
    Tree candidate(*this); candidate.remove(source); candidate.add(source,target,direction,ratio);
    *this=std::move(candidate);
}
void Tree::activate(WindowId window) { auto *n=find(window); require(n,"window not found"); n->active=window; }
void Tree::balance(std::optional<Axis> axis) {
    if(axis)axisCheck(*axis);
    // A stack occupies one tile regardless of how many windows it contains.
    std::function<std::size_t(Node *)> visit=[&](Node *n)->std::size_t {
        if(!n)return 0;
        if(n->leaf())return 1;
        auto first=visit(n->first.get()),second=visit(n->second.get());
        if(!axis||n->axis==*axis)n->ratio=std::clamp(double(first)/double(first+second),0.1,0.9);
        return first+second;
    };
    visit(root_.get());
}
void Tree::rotate(int clockwiseDegrees) {
    require(clockwiseDegrees==90||clockwiseDegrees==180||clockwiseDegrees==270,"rotation must be 90, 180 or 270 clockwise degrees");
    std::function<void(Node *)> quarter=[&](Node *n) {
        if(!n||n->leaf()) return;
        if(n->axis==Axis::Y) {std::swap(n->first,n->second);n->ratio=std::clamp(1.0-n->ratio,0.1,0.9);}
        n->axis=n->axis==Axis::X?Axis::Y:Axis::X;
        quarter(n->first.get());quarter(n->second.get());
    };
    for(int i=0;i<clockwiseDegrees/90;++i)quarter(root_.get());
}
void Tree::mirror(Axis axis) {
    axisCheck(axis);
    std::function<void(Node *)> visit=[&](Node *n) {
        if(!n||n->leaf()) return;
        if(n->axis==axis) {std::swap(n->first,n->second);n->ratio=std::clamp(1.0-n->ratio,0.1,0.9);}
        visit(n->first.get());visit(n->second.get());
    };
    visit(root_.get());
}
bool Tree::mirrorSlots(Axis axis) {
    axisCheck(axis);bool changed=false;
    std::function<void(Node *)> visit=[&](Node *node){
        if(!node||node->leaf())return;
        if(node->axis==axis){std::swap(node->first,node->second);changed=true;}
        visit(node->first.get());visit(node->second.get());
    };
    visit(root_.get());return changed;
}
bool Tree::equalize(std::optional<Axis> axis) {
    if(axis)axisCheck(*axis);
    bool changed=false;
    std::function<void(Node *)> visit=[&](Node *node){
        if(!node||node->leaf())return;
        if((!axis||node->axis==*axis)&&node->ratio!=0.5){node->ratio=0.5;changed=true;}
        visit(node->first.get());visit(node->second.get());
    };
    visit(root_.get());return changed;
}
Node *Tree::nearestSplit(WindowId window, Axis axis) const {
    axisCheck(axis); require(find(window),"window not found");
    Node *found=nullptr;
    std::function<void(Node *)> visit=[&](Node *n) {
        if (n->leaf()) return;
        if (n->axis==axis) found=n;
        if (contains(n->first.get(),window)) visit(n->first.get()); else visit(n->second.get());
    };
    visit(root_.get()); require(found,"no ancestor split on requested axis"); return found;
}
void Tree::setRatio(WindowId window, Axis axis, double ratio) {
    ratioCheck(ratio);nearestSplit(window,axis)->ratio=ratio;
}
bool Tree::adjustParentRatio(WindowId window, double value, bool relative) {
    require(std::isfinite(value),"ratio value must be finite");
    require(find(window),"window not found");
    Node *parent=nullptr,*node=root_.get();
    while(!node->leaf()){parent=node;node=contains(node->first.get(),window)?node->first.get():node->second.get();}
    require(parent,"cannot adjust ratio of a root node");
    const double target=std::clamp(relative?parent->ratio+value:value,0.1,0.9);
    if(target==parent->ratio)return false;
    parent->ratio=target;return true;
}
bool Tree::toggleParentSplit(WindowId window) {
    require(find(window),"window not found");Node *parent=nullptr,*node=root_.get();
    while(!node->leaf()){parent=node;node=contains(node->first.get(),window)?node->first.get():node->second.get();}
    if(!parent)return false;
    parent->axis=parent->axis==Axis::X?Axis::Y:Axis::X;return true;
}
void Tree::resize(WindowId window, Axis axis, double delta) {
    require(std::isfinite(delta)&&delta!=0,"resize delta must be finite and nonzero");
    auto *split=nearestSplit(window,axis);
    auto target=split->ratio+(contains(split->first.get(),window)?delta:-delta);
    require(target!=split->ratio,"resize delta is too small to change the ratio");
    // Repeated decimal steps can overshoot a valid endpoint by a few floating-point bits.
    for(double bound:{0.1,0.9})if(std::abs(target-bound)<1e-12)target=bound;
    ratioCheck(target);require(target!=split->ratio,"resize delta is too small to change the ratio");
    split->ratio=target;
}
bool Tree::resizeEdges(WindowId window, ResizeHandle handle, double dx, double dy, Rect area,
    Insets padding, double gap, const std::map<WindowId, Size> &minimums) {
    require(std::isfinite(dx)&&std::isfinite(dy),"resize deltas must be finite");
    bool left=false,right=false,top=false,bottom=false;
    switch(handle){
        case ResizeHandle::Left:left=true;break;case ResizeHandle::Right:right=true;break;
        case ResizeHandle::Top:top=true;break;case ResizeHandle::Bottom:bottom=true;break;
        case ResizeHandle::TopLeft:top=left=true;break;case ResizeHandle::TopRight:top=right=true;break;
        case ResizeHandle::BottomLeft:bottom=left=true;break;case ResizeHandle::BottomRight:bottom=right=true;break;
        default:throw std::invalid_argument("invalid resize handle");
    }
    require(find(window),"window not found");
    auto placements=layout(area,padding,gap,minimums);std::map<NodeId,Rect> bounds;bool visible=false;
    for(auto placement:placements){bounds[placement.leaf]=placement.frame;if(placement.window==window)visible=placement.visible;}
    require(visible,"resize source must be visible");
    // Unions recover each parent region, including internal gaps and minimum-size adjustments.
    std::function<Rect(const Node *)> measure=[&](const Node *node)->Rect {
        if(node->leaf())return bounds.at(node->id);
        auto a=measure(node->first.get()),b=measure(node->second.get());
        Rect result{std::min(a.x,b.x),std::min(a.y,b.y),0,0};
        result.width=std::max(a.x+a.width,b.x+b.width)-result.x;
        result.height=std::max(a.y+a.height,b.y+b.height)-result.y;
        bounds[node->id]=result;return result;
    };
    measure(root_.get());const Node *horizontal=nullptr,*vertical=nullptr,*node=root_.get();
    while(!node->leaf()) {
        bool first=contains(node->first.get(),window);
        if(node->axis==Axis::X&&((left&&!first)||(right&&first)))horizontal=node;
        if(node->axis==Axis::Y&&((top&&!first)||(bottom&&first)))vertical=node;
        node=first?node->first.get():node->second.get();
    }
    require(horizontal||vertical,"no internal split boundary for this resize handle");
    std::map<NodeId,double> changes;
    auto adjust=[&](const Node *split,double delta,double extent){
        require(std::isfinite(extent)&&extent>0,"invalid resize boundary extent");
        auto ratio=std::clamp(split->ratio+delta/extent,0.1,0.9);
        if(ratio!=split->ratio)changes[split->id]=ratio;
    };
    if(horizontal)adjust(horizontal,dx,bounds.at(horizontal->id).width);
    if(vertical)adjust(vertical,dy,bounds.at(vertical->id).height);
    if(changes.empty())return false;
    Tree candidate(*this);
    std::function<void(Node *)> apply=[&](Node *current){if(!current)return;if(changes.contains(current->id))current->ratio=changes.at(current->id);apply(current->first.get());apply(current->second.get());};
    apply(candidate.root_.get());(void)candidate.layout(area,padding,gap,minimums);candidate.validate();
    *this=std::move(candidate);return true;
}
std::vector<Placement> Tree::layout(Rect area, Insets pad, double gap, const std::map<WindowId,Size> &minimums) const {
    auto nonnegative=[](double v){return std::isfinite(v)&&v>=0;};
    require(std::isfinite(area.x)&&std::isfinite(area.y)&&nonnegative(area.width)&&nonnegative(area.height),"invalid work area");
    require(std::isfinite(area.x+area.width) && std::isfinite(area.y+area.height),"work area bounds overflow");
    require(nonnegative(gap)&&nonnegative(pad.top)&&nonnegative(pad.right)&&nonnegative(pad.bottom)&&nonnegative(pad.left),"invalid gap or padding");
    for (auto [id,s]:minimums) { (void)id; require(nonnegative(s.width)&&nonnegative(s.height),"invalid minimum size"); }
    area={area.x+pad.left,area.y+pad.top,area.width-pad.left-pad.right,area.height-pad.top-pad.bottom};
    require(nonnegative(area.width)&&nonnegative(area.height),"padding exceeds work area");
    std::map<NodeId,Size> sizes;
    std::function<Size(const Node *)> measure=[&](const Node *n)->Size {
        Size s;
        if(n->leaf()) { for(auto w:n->windows) if(auto it=minimums.find(w);it!=minimums.end()) {s.width=std::max(s.width,it->second.width);s.height=std::max(s.height,it->second.height);} }
        else {auto a=measure(n->first.get()),b=measure(n->second.get()); s=n->axis==Axis::X?Size{a.width+b.width+gap,std::max(a.height,b.height)}:Size{std::max(a.width,b.width),a.height+b.height+gap};}
        sizes[n->id]=s; return s;
    };
    if (!root_) return {};
    auto minimum=measure(root());
    require(minimum.width<=area.width && minimum.height<=area.height,"minimum sizes and gaps exceed work area");
    std::vector<Placement> out;
    std::function<void(const Node *,Rect)> place=[&](const Node *n, Rect r) {
        if(n->leaf()) { for(auto w:n->windows) out.push_back({w,n->id,r,w==n->active}); return; }
        auto a=sizes.at(n->first->id), b=sizes.at(n->second->id);
        bool x=n->axis==Axis::X; double available=(x?r.width:r.height)-gap;
        double extent=std::clamp(available*n->ratio,x?a.width:a.height,available-(x?b.width:b.height));
        Rect ra=r,rb=r;
        if(x) {ra.width=extent;rb.x=r.x+extent+gap;rb.width=available-extent;}
        else {ra.height=extent;rb.y=r.y+extent+gap;rb.height=available-extent;}
        place(n->first.get(),ra);place(n->second.get(),rb);
    };
    place(root(),area); return out;
}
Snapshot Tree::snapshot() const {
    Snapshot s; s.nextNode=nextNode_; s.root=root_?root_->id:0;
    std::function<void(const Node *)> visit=[&](const Node *n) {
        if(!n) return;
        s.nodes.push_back({n->id,n->first?n->first->id:0,n->second?n->second->id:0,n->axis,n->ratio,n->windows,n->active});
        visit(n->first.get());visit(n->second.get());
    }; visit(root());return s;
}
Tree Tree::restore(const Snapshot &s) {
    require(s.version==1,"unsupported snapshot version");
    require(s.nodes.size()<=4095,"snapshot exceeds 4095 nodes");
    Tree tree; tree.nextNode_=s.nextNode;
    std::map<NodeId,const Record *> records;
    for(const auto &r:s.nodes) {require(r.id && r.id<s.nextNode,"invalid node identity");require(records.emplace(r.id,&r).second,"duplicate node identity");}
    std::set<NodeId> visited; std::set<WindowId> windows;
    std::function<std::unique_ptr<Node>(NodeId,unsigned)> load=[&](NodeId id,unsigned depth) {
        require(depth<=128,"snapshot exceeds maximum depth");
        require(records.contains(id) && visited.insert(id).second,"missing, cyclic or shared node");
        const auto &r=*records.at(id); ratioCheck(r.ratio);axisCheck(r.axis);
        auto n=std::make_unique<Node>();n->id=id;n->axis=r.axis;n->ratio=r.ratio;n->windows=r.windows;n->active=r.active;
        if(r.first || r.second) {
            require(r.first && r.second && r.windows.empty() && !r.active,"invalid split");
            n->first=load(r.first,depth+1);n->second=load(r.second,depth+1);
        } else {
            require(!r.windows.empty() && std::find(r.windows.begin(),r.windows.end(),r.active)!=r.windows.end(),"invalid stack");
            for(auto w:r.windows) require(w && windows.insert(w).second,"duplicate or zero window identity");
        }
        return n;
    };
    require(s.nextNode>0,"invalid next node identity");
    if(s.root) tree.root_=load(s.root,0);
    require(visited.size()==records.size(),"unreachable nodes"); return tree;
}
void Tree::validate() const { (void)restore(snapshot()); }
}
