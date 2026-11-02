#include "tree.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("compatible space layout assertion failed");}
bool near(double a,double b){return std::abs(a-b)<1e-10;}
std::map<WindowId,Placement> frames(const Tree &tree){std::map<WindowId,Placement> out;for(auto p:tree.layout({-500,-200,1000,1000},{0,0,0,0},0))out[p.window]=p;return out;}
void identity(const Snapshot &before,const Tree &tree){auto after=tree.snapshot();check(before.root==after.root&&before.nextNode==after.nextNode&&before.nodes.size()==after.nodes.size());for(auto a:before.nodes){auto i=std::find_if(after.nodes.begin(),after.nodes.end(),[&](auto b){return a.id==b.id;});check(i!=after.nodes.end()&&a.windows==i->windows&&a.active==i->active&&a.axis==i->axis);}}
int main(){try{
 Tree t;t.add(1);t.add(2,1,Direction::East,0.7);auto original=frames(t);auto before=t.snapshot();
 check(!t.mirrorSlots(Axis::Y));check(t.mirrorSlots(Axis::X));auto mirrored=frames(t);identity(before,t);
 check(near(mirrored[2].frame.x,original[1].frame.x)&&near(mirrored[2].frame.width,700));check(t.root()->ratio==0.7);
 check(t.mirrorSlots(Axis::X));check(near(frames(t)[1].frame.x,original[1].frame.x));
 // Native 270 clockwise implements yabai's 90 counterclockwise on screen coordinates.
 auto rotated=t;rotated.rotate(270);auto quarter=frames(rotated);check(quarter[1].frame.y>quarter[2].frame.y&&near(quarter[1].frame.height,700));
 check(!t.equalize(Axis::Y));check(t.equalize(Axis::X));check(t.root()->ratio==0.5);check(!t.equalize());
 t.setRatio(1,Axis::X,0.6);t.add(3,2,Direction::South,0.3);auto topology=t.snapshot();check(t.equalize(Axis::Y));check(t.root()->ratio==0.6&&t.root()->second->ratio==0.5);identity(topology,t);
 t.setRatio(3,Axis::Y,0.2);check(t.equalize());for(auto r:t.snapshot().nodes)if(r.first)check(r.ratio==0.5);
 t.add(4,1);t.stack(4,3);auto stack=t.snapshot();check(t.mirrorSlots(Axis::Y));identity(stack,t);auto placements=frames(t);check(!placements[3].visible&&placements[4].visible);
 t.setRatio(1,Axis::X,0.6);auto loaded=Tree::restore(t.snapshot());check(loaded.equalize());loaded.validate();check(!loaded.equalize());
 bool rejected=false;try{loaded.mirrorSlots(static_cast<Axis>(99));}catch(const std::invalid_argument &){rejected=true;}check(rejected);
 rejected=false;try{loaded.equalize(static_cast<Axis>(99));}catch(const std::invalid_argument &){rejected=true;}check(rejected);
 Tree empty;check(!empty.equalize()&&!empty.mirrorSlots(Axis::X));Tree single;single.add(1);check(!single.equalize()&&!single.mirrorSlots(Axis::Y));
 std::cout<<"PASS slot mirrors retain ratios, counterclockwise mapping, axis equalization, no-ops, topology, stacks and persistence\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
