#include "tree.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("edge resize assertion failed");}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
void same(const Snapshot &a,const Snapshot &b){check(a.root==b.root&&a.nextNode==b.nextNode&&a.nodes.size()==b.nodes.size());for(std::size_t i=0;i<a.nodes.size();++i){auto x=a.nodes[i],y=b.nodes[i];check(x.id==y.id&&x.first==y.first&&x.second==y.second&&x.axis==y.axis&&x.ratio==y.ratio&&x.windows==y.windows&&x.active==y.active);}}
template<class F>void rejects(Tree &t,F f){auto before=t.snapshot();bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}check(rejected);same(before,t.snapshot());}
int main(){try{
 Rect area{-100,50,1000,800};Insets zero{0,0,0,0};Tree t;t.add(1);t.add(2,1,Direction::East);
 check(t.resizeEdges(1,ResizeHandle::Right,100,999,area,zero));check(near(t.root()->ratio,0.6));
 check(t.resizeEdges(2,ResizeHandle::Left,100,0,area,zero));check(near(t.root()->ratio,0.7));
 check(t.resizeEdges(2,ResizeHandle::Left,-100,0,area,zero));check(near(t.root()->ratio,0.6));
 check(!t.resizeEdges(1,ResizeHandle::Right,0,999,area,zero));
 rejects(t,[&]{t.resizeEdges(1,ResizeHandle::Left,10,0,area,zero);});rejects(t,[&]{t.resizeEdges(2,ResizeHandle::BottomRight,10,20,area,zero);});
 t.add(3,2,Direction::South);check(t.resizeEdges(2,ResizeHandle::BottomLeft,100,80,area,zero));check(near(t.root()->ratio,0.7)&&near(t.root()->second->ratio,0.6));
 check(t.resizeEdges(3,ResizeHandle::TopLeft,-100,-80,area,zero));check(near(t.root()->ratio,0.6)&&near(t.root()->second->ratio,0.5));
 check(t.resizeEdges(2,ResizeHandle::BottomRight,999,80,area,zero));check(near(t.root()->ratio,0.6)&&near(t.root()->second->ratio,0.6));
 // The nearest directional fence can be above the immediate parent.
 Tree nested;nested.add(1);nested.add(2,1,Direction::East);nested.add(3,1,Direction::East);
 check(nested.resizeEdges(3,ResizeHandle::Right,100,0,area,zero,0));check(near(nested.root()->ratio,0.6)&&near(nested.root()->first->ratio,0.5));
 check(nested.resizeEdges(3,ResizeHandle::Left,60,0,area,zero,0));check(near(nested.root()->ratio,0.6)&&near(nested.root()->first->ratio,0.6));
 auto before=t.snapshot();rejects(t,[&]{t.resizeEdges(2,ResizeHandle::BottomLeft,100,std::nan(""),area,zero);});same(before,t.snapshot());
 rejects(t,[&]{t.resizeEdges(99,ResizeHandle::Right,10,0,area,zero);});rejects(t,[&]{t.resizeEdges(1,static_cast<ResizeHandle>(99),10,0,area,zero);});
 check(t.resizeEdges(1,ResizeHandle::Right,1e300,0,area,zero));check(t.root()->ratio==0.9);check(!t.resizeEdges(1,ResizeHandle::Right,100,0,area,zero));
 check(t.resizeEdges(1,ResizeHandle::Right,-1e300,0,area,zero));check(t.root()->ratio==0.1);
 auto placed=t.layout(area,zero,10,{{1,{200,100}},{2,{80,100}},{3,{80,100}}});check(placed[0].frame.width>=200&&t.root()->ratio==0.1);
 rejects(t,[&]{t.resizeEdges(1,ResizeHandle::Right,10,0,area,zero,10,{{1,{950,0}},{2,{100,0}}});});
 Tree portrait;portrait.add(1);portrait.add(2,1,Direction::East);check(portrait.resizeEdges(1,ResizeHandle::Right,51,0,{-1080,0,1080,1920},{10,20,30,40},12));check(near(portrait.root()->ratio,0.55));
 portrait.add(3,2);portrait.stack(3,1);rejects(portrait,[&]{portrait.resizeEdges(1,ResizeHandle::Right,10,0,area,zero);});
 auto loaded=Tree::restore(t.snapshot());check(loaded.resizeEdges(1,ResizeHandle::Right,100,0,area,zero));loaded.validate();
 Tree single;single.add(1);rejects(single,[&]{single.resizeEdges(1,ResizeHandle::BottomRight,0,0,area,zero);});
 std::cout<<"PASS edge/corner fences, second-child signs, ancestor selection, limits, no-ops, minima, portrait padding, stacks and atomic failure\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
