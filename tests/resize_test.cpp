#include "tree.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("relative resize assertion failed");}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
std::map<WindowId,Rect> frames(const Tree &t){std::map<WindowId,Rect> out;for(auto p:t.layout({-100,0,1000,800},{0,0,0,0},0))out[p.window]=p.frame;return out;}
void unchanged(const Snapshot &before,const Tree &after){auto current=after.snapshot();check(before.root==current.root&&before.nextNode==current.nextNode&&before.nodes.size()==current.nodes.size());for(std::size_t i=0;i<before.nodes.size();++i){const auto &a=before.nodes[i],&b=current.nodes[i];check(a.id==b.id&&a.first==b.first&&a.second==b.second&&a.axis==b.axis&&a.windows==b.windows&&a.active==b.active&&a.ratio==b.ratio);}}
template<class F>void rejects(Tree &t,F f){auto saved=t.snapshot();bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);unchanged(saved,t);}
int main(){try{
 Tree t;t.add(1);t.add(2,1,Direction::East);
 t.resize(1,Axis::X,0.05);check(near(frames(t).at(1).width,550));check(near(frames(t).at(2).width,450));
 t.resize(1,Axis::X,-0.05);check(near(frames(t).at(1).width,500));
 t.resize(2,Axis::X,0.05);check(near(frames(t).at(2).width,550));check(near(frames(t).at(1).width,450));
 t.resize(2,Axis::X,-0.05);check(near(frames(t).at(1).width,500));
 // Resize the nearest matching split, leaving the other-axis descendant and outer split alone.
 t.add(3,1,Direction::South,0.3);t.add(4,1,Direction::East,0.6);auto before=t.snapshot();auto rootRatio=t.root()->ratio;auto oldFrames=frames(t);
 t.resize(4,Axis::X,0.1);auto grown=frames(t);check(grown.at(4).width>oldFrames.at(4).width);check(near(grown.at(2).width,oldFrames.at(2).width));check(t.root()->ratio==rootRatio);
 auto resized=t.snapshot();check(before.nextNode==resized.nextNode&&before.root==resized.root);int changed=0;for(std::size_t i=0;i<before.nodes.size();++i){auto a=before.nodes[i],b=resized.nodes[i];check(a.id==b.id&&a.first==b.first&&a.second==b.second&&a.axis==b.axis&&a.windows==b.windows&&a.active==b.active);if(a.ratio!=b.ratio)++changed;}check(changed==1);
 auto height=frames(t).at(3).height;t.resize(3,Axis::Y,0.05);check(frames(t).at(3).height>height);check(t.root()->ratio==rootRatio);
 // A stack shares a leaf: its active and inactive members select the same branch.
 t.add(5);t.stack(5,4);auto stacked=t.snapshot();auto width=frames(t).at(5).width;t.resize(5,Axis::X,0.05);check(frames(t).at(4).width==frames(t).at(5).width);check(frames(t).at(5).width>width);auto members=t.snapshot();for(auto r:stacked.nodes)if(!r.windows.empty()){auto it=std::find_if(members.nodes.begin(),members.nodes.end(),[&](auto n){return n.id==r.id;});check(it!=members.nodes.end()&&it->windows==r.windows&&it->active==r.active);}
 rejects(t,[&]{t.resize(1,Axis::X,0);});rejects(t,[&]{t.resize(1,Axis::X,std::numeric_limits<double>::quiet_NaN());});rejects(t,[&]{t.resize(1,Axis::X,std::numeric_limits<double>::infinity());});rejects(t,[&]{t.resize(1,static_cast<Axis>(17),0.05);});rejects(t,[&]{t.resize(99,Axis::X,0.05);});rejects(t,[&]{t.resize(1,Axis::X,1);});rejects(t,[&]{t.resize(1,Axis::X,1e-100);});
 Tree limits;limits.add(1);limits.add(2,1,Direction::East);for(int i=0;i<8;++i)limits.resize(1,Axis::X,0.05);check(limits.root()->ratio==0.9);limits.validate();rejects(limits,[&]{limits.resize(1,Axis::X,0.05);});for(int i=0;i<16;++i)limits.resize(1,Axis::X,-0.05);check(limits.root()->ratio==0.1);limits=Tree::restore(limits.snapshot());rejects(limits,[&]{limits.resize(2,Axis::X,0.05);});
 Tree single;single.add(1);rejects(single,[&]{single.resize(1,Axis::Y,0.05);});rejects(limits,[&]{limits.resize(1,Axis::Y,0.05);});
 // Minimum sizes constrain geometry without rewriting the requested ratio.
 limits.resize(1,Axis::X,0.05);auto ratio=limits.root()->ratio;auto plan=limits.layout({0,0,1000,800},{0,0,0,0},0,{{1,{400,0}}});check(plan[0].frame.width>=400);check(limits.root()->ratio==ratio);
 std::cout<<"PASS positive/negative resizing from both sides, nearest-axis selection, stacks, endpoints and atomic rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
