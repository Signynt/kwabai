#include "tree.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("balance assertion failed");}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
std::map<NodeId,Record> records(const Tree &t){std::map<NodeId,Record> out;for(auto r:t.snapshot().nodes)out[r.id]=r;return out;}
void identities(const Snapshot &a,const Tree &t){
 auto b=t.snapshot();auto byId=records(t);check(a.root==b.root&&a.nextNode==b.nextNode&&a.nodes.size()==b.nodes.size());
 for(auto r:a.nodes){auto s=byId.at(r.id);check(r.first==s.first&&r.second==s.second&&r.axis==s.axis&&r.windows==s.windows&&r.active==s.active);}
}
int main(){try{
 // Nested columns need a 2:1 root split, not equal halves, to yield equal tile widths.
 Tree columns;columns.add(1);columns.add(2,1,Direction::East,0.8);columns.add(3,1,Direction::East,0.2);auto before=columns.snapshot();columns.balance();columns.validate();identities(before,columns);
 auto layout=columns.layout({-100,0,1200,600},{0,0,0,0},0);check(layout.size()==3);for(auto p:layout)check(near(p.frame.width,400)&&near(p.frame.height,600));
 auto first=records(columns);columns.balance();auto repeated=records(columns);for(auto [id,r]:first)check(r.ratio==repeated.at(id).ratio);
 // Additional hidden windows in a stack must not take extra allocation from other tiles.
 columns.add(4,2,Direction::South);columns.stack(4,2);auto stacked=columns.snapshot();columns.balance();identities(stacked,columns);layout=columns.layout({0,0,1200,600},{0,0,0,0},0);for(auto p:layout)check(near(p.frame.width,400));check(std::count_if(layout.begin(),layout.end(),[](auto p){return p.visible;})==3);
 // Axis-filtered balance leaves all other ratios untouched while counting every leaf.
 Tree mixed;mixed.add(1);mixed.add(2,1,Direction::East,0.8);mixed.add(3,2,Direction::South,0.2);auto original=records(mixed);auto snapshot=mixed.snapshot();mixed.balance(Axis::X);identities(snapshot,mixed);auto horizontal=records(mixed);
 for(auto [id,r]:original)if(r.first){if(r.axis==Axis::X)check(near(horizontal.at(id).ratio,1.0/3));else check(horizontal.at(id).ratio==r.ratio);}
 mixed.balance(Axis::Y);auto vertical=records(mixed);for(auto [id,r]:horizontal)if(r.first){if(r.axis==Axis::Y)check(near(vertical.at(id).ratio,0.5));else check(vertical.at(id).ratio==r.ratio);}
 // Client minimums affect resulting geometry without rewriting balanced intent.
 auto balanced=records(columns);layout=columns.layout({0,0,1200,600},{0,0,0,0},0,{{1,{700,0}}});auto one=std::find_if(layout.begin(),layout.end(),[](auto p){return p.window==1;});check(one!=layout.end()&&one->frame.width>=700);for(auto [id,r]:balanced)check(records(columns).at(id).ratio==r.ratio);
 // Deeply skewed branches respect the existing ratio limits instead of creating invalid snapshots.
 Tree skewed;skewed.add(1);for(WindowId i=2;i<=20;++i)skewed.add(i,1,Direction::East,0.5);auto skewSnapshot=skewed.snapshot();skewed.balance();skewed.validate();identities(skewSnapshot,skewed);check(near(skewed.root()->ratio,0.9));auto loaded=Tree::restore(skewed.snapshot());loaded.validate();
 auto saved=records(mixed);bool rejected=false;try{mixed.balance(static_cast<Axis>(17));}catch(const std::invalid_argument &){rejected=true;}check(rejected);for(auto [id,r]:saved)check(records(mixed).at(id).ratio==r.ratio);
 Tree empty;empty.balance();empty.validate();Tree single;single.add(1);auto singleSnapshot=single.snapshot();single.balance();identities(singleSnapshot,single);
 std::cout<<"PASS weighted tile allocation, axis filtering, stack identity, idempotence, minimums and ratio limits\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
