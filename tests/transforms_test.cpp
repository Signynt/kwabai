#include "tree.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("tree transform assertion failed");}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
void same(Rect a,Rect b){check(near(a.x,b.x)&&near(a.y,b.y)&&near(a.width,b.width)&&near(a.height,b.height));}
std::map<WindowId,Placement> placed(const Tree &tree,Rect area){std::map<WindowId,Placement> out;for(auto p:tree.layout(area))out[p.window]=p;return out;}
void identities(const Snapshot &a,const Snapshot &b){
 check(a.root==b.root&&a.nextNode==b.nextNode&&a.nodes.size()==b.nodes.size());
 for(const auto &r:a.nodes){auto it=std::find_if(b.nodes.begin(),b.nodes.end(),[&](const Record &x){return x.id==r.id;});check(it!=b.nodes.end());check(r.windows==it->windows&&r.active==it->active);}
}
int main(){try{
 Tree t;t.add(1);t.add(2,1,Direction::East,0.6);t.add(3,2,Direction::South,0.3);t.add(4,1,Direction::East,0.1);t.add(5);t.stack(5,3);
 auto original=t.snapshot();Rect area{-800,-300,1000,1000};auto before=placed(t,area);
 for(int angle:{90,180,270}) {
  auto rotated=t;rotated.rotate(angle);rotated.validate();identities(original,rotated.snapshot());auto after=placed(rotated,area);
  for(auto [id,p]:before){auto r=p.frame;for(int turn=0;turn<angle/90;++turn)r={area.x+area.width-(r.y-area.y+r.height),area.y+(r.x-area.x),r.height,r.width};same(after.at(id).frame,r);check(after.at(id).leaf==p.leaf&&after.at(id).visible==p.visible);}
  rotated.rotate(360-angle);auto restored=placed(rotated,area);for(auto [id,p]:before)same(restored.at(id).frame,p.frame);
 }
 for(auto axis:{Axis::X,Axis::Y}){
  auto reflected=t;reflected.mirror(axis);reflected.validate();identities(original,reflected.snapshot());auto after=placed(reflected,area);
  for(auto [id,p]:before){auto r=p.frame;if(axis==Axis::X)r.x=area.x+area.width-(r.x-area.x+r.width);else r.y=area.y+area.height-(r.y-area.y+r.height);same(after.at(id).frame,r);check(after.at(id).visible==p.visible);}
  reflected.mirror(axis);after=placed(reflected,area);for(auto [id,p]:before)same(after.at(id).frame,p.frame);
 }
 auto full=t;for(int i=0;i<4;++i)full.rotate(90);full.validate();for(auto [id,p]:before)same(placed(full,area).at(id).frame,p.frame);
 bool rejected=false;try{t.rotate(45);}catch(const std::invalid_argument &){rejected=true;}check(rejected);for(auto [id,p]:before)same(placed(t,area).at(id).frame,p.frame);
 rejected=false;try{t.mirror(static_cast<Axis>(17));}catch(const std::invalid_argument &){rejected=true;}check(rejected);
 // Toggling a parent is distinct from rotating a subtree: only its axis changes.
 Tree split;split.add(1);split.add(2,1,Direction::East,0.6);split.add(3,2,Direction::South,0.3);auto splitBefore=split.snapshot();
 check(split.toggleParentSplit(3));check(split.root()->axis==Axis::X&&split.root()->second->axis==Axis::X);check(split.root()->ratio==0.6&&split.root()->second->ratio==0.3);
 auto toggled=split.snapshot();int changedAxes=0;
 for(auto a:splitBefore.nodes){auto i=std::find_if(toggled.nodes.begin(),toggled.nodes.end(),[&](auto b){return a.id==b.id;});check(i!=toggled.nodes.end());check(a.first==i->first&&a.second==i->second&&a.ratio==i->ratio&&a.windows==i->windows&&a.active==i->active);changedAxes+=a.axis!=i->axis;}check(changedAxes==1);
 check(split.toggleParentSplit(2));auto splitAfter=split.snapshot();for(auto a:splitBefore.nodes){auto i=std::find_if(splitAfter.nodes.begin(),splitAfter.nodes.end(),[&](auto b){return a.id==b.id;});check(a.axis==i->axis&&a.ratio==i->ratio&&a.first==i->first&&a.second==i->second);}
 auto persisted=Tree::restore(toggled);check(persisted.root()->second->axis==Axis::X);bool missing=false;try{split.toggleParentSplit(99);}catch(const std::invalid_argument &){missing=true;}check(missing);
 Tree rootLeaf;rootLeaf.add(1);auto rootBefore=rootLeaf.snapshot();check(!rootLeaf.toggleParentSplit(1));check(rootLeaf.snapshot().root==rootBefore.root&&rootLeaf.snapshot().nodes.size()==1);
 // Boundary ratios remain valid after floating-point complementation and round-trip loading.
 for(double ratio:{0.1,0.9}){Tree edge;edge.add(1);edge.add(2,1,Direction::East,ratio);for(int i=0;i<8;++i){edge.rotate(90);edge.mirror(Axis::X);edge.validate();edge=Tree::restore(edge.snapshot());}}
 Tree empty;empty.rotate(90);empty.mirror(Axis::Y);empty.validate();Tree single;single.add(1);single.rotate(270);same(placed(single,area).at(1).frame,single.layout(area).at(0).frame);
 // A rotated landscape topology remains feasible in a portrait area with minimum sizes.
 auto portrait=t;portrait.rotate(90);auto layout=portrait.layout({-1080,0,1080,1920},{},10,{{1,{80,80}},{2,{80,80}},{3,{80,80}},{4,{80,80}},{5,{80,80}}});check(layout.size()==5);
 std::cout<<"PASS rotations, mirrors, geometry mapping, identities, stacks, ratio boundaries and portrait minimum constraints\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
