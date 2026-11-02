#include "tree.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
using namespace kwabai;
void check(bool ok, const char *message) {if(!ok) throw std::runtime_error(message);}
void near(double a,double b) {check(std::abs(a-b)<1e-7,"geometry mismatch");}
template<class F> void rejects(F f) {bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught,"expected invalid_argument");}
std::string fingerprint(const Snapshot &s) {
    std::string out=std::to_string(s.root)+":"+std::to_string(s.nextNode);
    for(auto &r:s.nodes) {out+="|"+std::to_string(r.id)+":"+std::to_string(r.first)+":"+std::to_string(r.second)+":"+std::to_string(int(r.axis))+":"+std::to_string(r.ratio)+":"+std::to_string(r.active);for(auto w:r.windows)out+=","+std::to_string(w);}
    return out;
}
void same(const Tree &t,const Snapshot &s) {check(fingerprint(t.snapshot())==fingerprint(s),"tree changed unexpectedly");}
Placement placement(const Tree &t,WindowId id) {for(auto p:t.layout({0,0,2560,1440}))if(p.window==id)return p;throw std::runtime_error("window missing");}
void geometry() {
    for(int count:{1,2,3,5,10,20}) for(Rect area: {Rect{0,0,2560,1440},Rect{-1080,0,1080,1920},Rect{-865.5,-11.25,865.5,1536.25}}) {
        Tree t;t.add(1);
        for(int i=2;i<=count;i++)t.add(i,i-1,i%2?Direction::South:Direction::East,0.4);
        t.validate(); auto result=t.layout(area,{},2);
        check(result.size()==std::size_t(count),"wrong placement count");
        double total=0;
        for(auto p:result) {
            check(p.frame.width>=0&&p.frame.height>=0,"negative extent");
            check(p.frame.x>=area.x+10-1e-7&&p.frame.y>=area.y+10-1e-7,"outside padded origin");
            check(p.frame.x+p.frame.width<=area.x+area.width-10+1e-7,"outside right edge");
            check(p.frame.y+p.frame.height<=area.y+area.height-10+1e-7,"outside bottom edge");
            total+=p.frame.width*p.frame.height;
        }
        check(total>0,"empty layout");
        for(std::size_t i=0;i<result.size();i++) for(std::size_t j=i+1;j<result.size();j++) {
            auto a=result[i].frame,b=result[j].frame;
            check(a.x+a.width<=b.x+1e-7||b.x+b.width<=a.x+1e-7||a.y+a.height<=b.y+1e-7||b.y+b.height<=a.y+1e-7,"overlapping leaves");
        }
    }
    Tree t;t.add(1);t.add(2,1,Direction::East,0.6);
    auto p=t.layout({-1080,0,1080,1920}); near(p[0].frame.width,630);near(p[1].frame.x,-430);near(p[1].frame.width,420);
    near(p[0].frame.width+p[1].frame.width+10,1060);
    t.setRatio(1,Axis::X,0.8);auto before=t.snapshot();
    auto constrained=t.layout({0,0,1000,800},{},10,{{2,{400,100}}});near(constrained[1].frame.width,400);same(t,before);
    rejects([&]{t.layout({0,0,500,500},{},10,{{1,{400,0}},{2,{400,0}}});});same(t,before);
    rejects([&]{t.layout({0,0,10,10});});
    rejects([&]{t.layout({std::numeric_limits<double>::max(),0,std::numeric_limits<double>::max(),1000});});
    rejects([&]{t.layout({0,0,1000,800},{},-1);});
    rejects([&]{t.layout({0,0,1000,800},{},10,{{1,{NAN,0}}});});
}
void operations() {
    Tree t;t.add(1);t.add(2,1,Direction::East,0.6);t.add(3,1,Direction::South,0.3);
    auto original=t.snapshot();auto leftId=placement(t,1).leaf;
    t.swap(1,2);check(placement(t,2).leaf==leftId,"swap rebuilt leaf");near(t.root()->ratio,0.6);
    t.swap(1,2);same(t,original);
    t.setRatio(1,Axis::Y,0.7);near(t.root()->ratio,0.6);near(t.root()->first->ratio,0.7);
    t.stack(3,1);check(placement(t,3).leaf==placement(t,1).leaf,"stack split");check(!placement(t,1).visible&&placement(t,3).visible,"stack visibility");
    t.activate(1);check(placement(t,1).visible&&!placement(t,3).visible,"stack activation");
    t.swap(1,3);t.validate();check(placement(t,3).visible,"same stack swap active slot");
    t.warp(3,1,Direction::West,0.4);t.validate();check(placement(t,3).leaf!=placement(t,1).leaf,"warp did not unstack");
    auto before=t.snapshot();rejects([&]{t.warp(1,2,Direction::North,NAN);});same(t,before);
    rejects([&]{t.add(1);});same(t,before);rejects([&]{t.remove(999);});same(t,before);
    rejects([&]{t.stack(1,1);});same(t,before);rejects([&]{t.setRatio(1,Axis::X,1.0);});same(t,before);
    auto restored=Tree::restore(before);same(restored,before);
    for(auto id:{1,2,3})restored.remove(id);restored.validate();check(!restored.root(),"root not empty");
    restored.add(4);check(restored.root()->id>=before.nextNode,"reused node ID");
}
void limits() {
    Tree deep;deep.add(1);
    for(WindowId w=2;w<=129;w++) deep.add(w,w-1);
    deep.validate();auto s=deep.snapshot();
    rejects([&]{deep.add(130,129);});same(deep,s);
    Tree stack;stack.add(1);stack.add(2,1);stack.stack(2,1);
    auto result=stack.layout({0,0,1000,800},{},10,{{1,{100,100}},{2,{800,700}}});
    near(result[0].frame.width,980);check(result[1].visible,"stack active visibility lost");
    rejects([&]{stack.layout({0,0,700,600},{},10,{{2,{800,700}}});});
}
void invalidSnapshots() {
    Tree t;t.add(1);t.add(2,1);auto s=t.snapshot();
    auto bad=s;bad.nodes[0].first=bad.root;rejects([&]{Tree::restore(bad);});
    bad=s;bad.nodes[0].second=bad.nodes[0].first;rejects([&]{Tree::restore(bad);});
    bad=s;bad.nodes.back().windows={1};bad.nodes.back().active=1;rejects([&]{Tree::restore(bad);});
    bad=s;bad.version=2;rejects([&]{Tree::restore(bad);});
    bad=s;bad.nextNode=1;rejects([&]{Tree::restore(bad);});
    bad=s;bad.nodes.back().active=99;rejects([&]{Tree::restore(bad);});
    bad=s;bad.root=0;rejects([&]{Tree::restore(bad);});
    bad=s;bad.nodes[0].ratio=INFINITY;rejects([&]{Tree::restore(bad);});
}
void randomTransactions() {
    std::mt19937 rng(0xB5B);
    for(int run=0;run<25;run++) {
        Tree t; std::set<WindowId> ids;WindowId next=1;
        for(int step=0;step<400;step++) {
            auto pick=[&] {auto it=ids.begin();std::advance(it,rng()%ids.size());return *it;};
            auto op=rng()%5;
            if(ids.empty()||(op==0&&ids.size()<20)) {auto target=ids.empty()?0:pick();t.add(next,target,Direction(rng()%4),0.1+double(rng()%81)/100);ids.insert(next++);}
            else if(op==1) {auto w=pick();t.remove(w);ids.erase(w);}
            else if(ids.size()>1) {auto a=pick(),b=pick();if(a!=b){if(op==2)t.swap(a,b);else if(op==3)t.stack(a,b);else t.warp(a,b,Direction(rng()%4));}}
            t.validate();std::set<WindowId> actual;
            for(auto p:t.layout({-1080,0,2560,1920},{0,0,0,0},0))check(actual.insert(p.window).second,"duplicate window");
            check(actual==ids,"lost window");auto copy=Tree::restore(t.snapshot());same(copy,t.snapshot());
        }
    }
}
int main() {
    try {geometry();operations();invalidSnapshots();limits();randomTransactions();std::cout<<"PASS geometry, stacks, mutation atomicity, snapshot validation and 10,000 seeded transactions\n";}
    catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
