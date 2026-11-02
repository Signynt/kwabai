#include "json.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok) {if(!ok)throw std::runtime_error("session assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
int main() {
 try {
    Tree tree;tree.add(18446744073709551615ULL);tree.add(2,18446744073709551615ULL);tree.add(3,2);tree.stack(3,2);
    auto bytes=encodeSnapshot(tree);auto copy=decodeSnapshot(bytes);check(encodeSnapshot(copy)==bytes);
    auto empty=decodeSnapshot(encodeSnapshot(Tree{}));check(!empty.root());
    rejects([]{decodeSnapshot("not json");});rejects([]{decodeSnapshot("{}");});
    rejects([]{decodeSnapshot(QByteArray(1024*1024+1,' '));});
    auto obj=QJsonDocument::fromJson(bytes).object();obj["root"]=1;rejects([&]{decodeSnapshot(QJsonDocument(obj).toJson());});
    obj=QJsonDocument::fromJson(bytes).object();obj["version"]=1.5;rejects([&]{decodeSnapshot(QJsonDocument(obj).toJson());});
    obj=QJsonDocument::fromJson(bytes).object();auto nodes=obj["nodes"].toArray();auto n=nodes[0].toObject();n["ratio"]="0.5";nodes[0]=n;obj["nodes"]=nodes;rejects([&]{decodeSnapshot(QJsonDocument(obj).toJson());});
    std::cout<<"PASS exact JSON round-trip including 64-bit IDs, empty trees and corrupt input rejection\n";
 } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
