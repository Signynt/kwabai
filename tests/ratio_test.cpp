#include "tree.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("parent ratio assertion failed");}
bool near(double a,double b){return std::abs(a-b)<1e-10;}
void same(const Snapshot &a,const Snapshot &b){check(a.root==b.root&&a.nextNode==b.nextNode&&a.nodes.size()==b.nodes.size());for(std::size_t i=0;i<a.nodes.size();++i){auto x=a.nodes[i],y=b.nodes[i];check(x.id==y.id&&x.first==y.first&&x.second==y.second&&x.axis==y.axis&&x.ratio==y.ratio&&x.windows==y.windows&&x.active==y.active);}}
template<class F>void rejects(Tree &t,F f){auto before=t.snapshot();bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}check(rejected);same(before,t.snapshot());}
int main(){try{
 Tree t;t.add(1);rejects(t,[&]{t.adjustParentRatio(1,0.6,false);});t.add(2,1,Direction::East);
 check(t.adjustParentRatio(2,0.1,true));check(near(t.root()->ratio,0.6)); // Always grows first child, including from second.
 check(t.adjustParentRatio(1,0.3,false));check(near(t.root()->ratio,0.3));
 check(t.adjustParentRatio(1,100,true));check(t.root()->ratio==0.9);auto before=t.snapshot();check(!t.adjustParentRatio(2,100,true));same(before,t.snapshot());
 check(t.adjustParentRatio(2,-100,false));check(t.root()->ratio==0.1);check(!t.adjustParentRatio(1,0,true));
 rejects(t,[&]{t.adjustParentRatio(99,0.5,false);});rejects(t,[&]{t.adjustParentRatio(1,std::numeric_limits<double>::infinity(),true);});rejects(t,[&]{t.adjustParentRatio(1,std::nan(""),false);});
 // Immediate parent selection must not reach a higher split, even on another axis.
 t.add(3,1,Direction::South,0.4);auto outer=t.root()->ratio;check(t.adjustParentRatio(3,0.2,true));check(near(t.root()->first->ratio,0.6));check(t.root()->ratio==outer);
 t.add(4,1,Direction::East,0.5);auto middle=t.root()->first->ratio;
 check(t.adjustParentRatio(4,0.7,false));check(near(t.root()->first->first->ratio,0.7));check(t.root()->first->ratio==middle&&t.root()->ratio==outer);
 t.add(5,2,Direction::East);t.stack(5,4);check(t.adjustParentRatio(5,0.8,false));check(near(t.root()->first->first->ratio,0.8));t.validate();
 auto restored=Tree::restore(t.snapshot());check(restored.adjustParentRatio(4,-0.1,true));check(near(restored.root()->first->first->ratio,0.7));
 Tree singleton;singleton.add(1);singleton.add(2);singleton.stack(2,1);rejects(singleton,[&]{singleton.adjustParentRatio(2,0.4,false);});
 std::cout<<"PASS immediate parent ratio, child-independent sign, mixed axes, stacks, clamping, no-ops, persistence and atomic rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
