#include "store.h"
#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("context assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
QJsonObject window(int n){return {{"uuid",QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'))},{"app","test"},{"title","test"},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"frame",rect(100,100,800,600)},{"minimum",QJsonObject{{"w",100},{"h",100}}},{"candidate",true},{"minimized",false},{"fullscreen",false},{"focused",false}};}
int main(){try{
 const QString desktop="11111111-1111-4111-8111-111111111111";QJsonArray windows{window(1),window(2),window(3)};
 QJsonObject s{{"protocol",1},{"instance","33333333-3333-4333-8333-333333333333"},{"sequence","1"},{"kwin-version","6.8.1"},{"capabilities",QJsonArray{"snapshot","contexts"}},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"scale",1},{"geometry",rect(0,0,2560,1440)},{"work-area",rect(0,0,2560,1440)},{"current-desktop",desktop},{"current-activity","work"}}}},{"windows",windows}};
 BridgeState b;b.accept(QJsonDocument(s).toJson());ContextStore store;store.edit(b,{"add","DP-1","1"});store.edit(b,{"add","DP-1","2"});auto initial=store.show(b,"DP-1");check(initial["history"].toObject()["undo"]==QJsonValue(0));rejects([&]{store.edit(b,{"undo","DP-1"});});
 store.edit(b,{"gap","DP-1","20"});store.edit(b,{"padding","DP-1","30"});store.edit(b,{"rotate","DP-1","90"});auto rotated=store.show(b,"DP-1");check(rotated["history"].toObject()["undo"]==QJsonValue(3));auto generation=rotated["generation"].toString().toULongLong();
 store.edit(b,{"undo","DP-1"});auto undone=store.show(b,"DP-1");check(undone["tree"]==initial["tree"]);check(undone["generation"].toString().toULongLong()==generation+1);check(undone["padding"]==QJsonValue(QJsonArray{30,30,30,30}));
 store.edit(b,{"undo","DP-1"});check(store.show(b,"DP-1")["padding"]==initial["padding"]);store.edit(b,{"undo","DP-1"});check(store.show(b,"DP-1")["gap"]==initial["gap"]);
 for(int i=0;i<3;++i)store.edit(b,{"redo","DP-1"});check(store.show(b,"DP-1")["tree"]==rotated["tree"]);check(store.show(b,"DP-1")["generation"].toString().toULongLong()==generation+6);
 store.edit(b,{"undo","DP-1"});auto history=store.show(b,"DP-1")["history"].toObject();auto saved=store.state();rejects([&]{store.edit(b,{"padding","DP-1","-1"});});check(store.state()==saved);check(store.show(b,"DP-1")["history"].toObject()==history);
 // No-op content edits preserve redo even though the command advances its generation.
 store.edit(b,{"gap","DP-1","20"});check(store.show(b,"DP-1")["history"].toObject()==history);store.edit(b,{"gap","DP-1","50"});check(store.show(b,"DP-1")["history"].toObject()["redo"]==QJsonValue(0));rejects([&]{store.edit(b,{"redo","DP-1"});});
 store.edit(b,{"add","DP-1","3"});check(store.show(b,"DP-1")["history"].toObject()["undo"]==QJsonValue(0));rejects([&]{store.edit(b,{"undo","DP-1"});});
 for(int i=0;i<40;++i)store.edit(b,{"gap","DP-1",QString::number(100+i)});check(store.show(b,"DP-1")["history"].toObject()["undo"]==QJsonValue(32));for(int i=0;i<32;++i)store.edit(b,{"undo","DP-1"});check(store.show(b,"DP-1")["gap"]==QJsonValue(107));rejects([&]{store.edit(b,{"undo","DP-1"});});store.edit(b,{"redo","DP-1"});check(store.show(b,"DP-1")["gap"]==QJsonValue(108));
 auto restart=ContextStore::restore(store.state());check(restart.state()==store.state());check(restart.show(b,"DP-1")["history"].toObject()["undo"]==QJsonValue(0));rejects([&]{restart.edit(b,{"redo","DP-1"});});
 // Undo must preserve the allocation high-water mark across warps and discarded redo branches.
 ContextStore warp;for(int i=1;i<=3;++i)warp.edit(b,{"add","DP-1",QString::number(i)});warp.edit(b,{"warp","DP-1","1","3","west"});auto high=warp.show(b,"DP-1")["tree"].toObject()["next-node"].toString().toULongLong();warp.edit(b,{"undo","DP-1"});check(warp.show(b,"DP-1")["tree"].toObject()["next-node"].toString().toULongLong()==high);warp.edit(b,{"warp","DP-1","2","3","south"});check(warp.show(b,"DP-1")["tree"].toObject()["next-node"].toString().toULongLong()>high);rejects([&]{warp.edit(b,{"redo","DP-1"});});
 // Compatible parent ratios retain edit history; clamped and zero deltas are true no-ops.
 ContextStore ratios;ratios.edit(b,{"add","DP-1","1"});ratios.edit(b,{"add","DP-1","2"});
 ratios.edit(b,{"parent-ratio","DP-1","2","abs","0.6"});auto firstRatio=ratios.show(b,"DP-1");check(firstRatio["history"].toObject()["undo"]==QJsonValue(1));
 ratios.edit(b,{"parent-ratio","DP-1","1","rel","0"});check(ratios.show(b,"DP-1")==firstRatio);
 ratios.edit(b,{"parent-ratio","DP-1","1","abs","50"});auto limitRatio=ratios.show(b,"DP-1");ratios.edit(b,{"parent-ratio","DP-1","2","rel","1"});check(ratios.show(b,"DP-1")==limitRatio);
 ratios.edit(b,{"undo","DP-1"});check(ratios.show(b,"DP-1")["tree"]==firstRatio["tree"]);ratios.edit(b,{"redo","DP-1"});check(ratios.show(b,"DP-1")["tree"]==limitRatio["tree"]);
 auto ratioRestart=ContextStore::restore(ratios.state());check(ratioRestart.show(b,"DP-1")["tree"]==limitRatio["tree"]);
 // Whole-layout compatibility edits share history, while equalization no-ops preserve it.
 auto beforeEqualize=ratios.show(b,"DP-1");ratios.edit(b,{"equalize","DP-1"});auto equalized=ratios.show(b,"DP-1");
 check(equalized["tree"]!=beforeEqualize["tree"]);ratios.edit(b,{"equalize","DP-1"});check(ratios.show(b,"DP-1")==equalized);
 ratios.edit(b,{"undo","DP-1"});check(ratios.show(b,"DP-1")["tree"]==beforeEqualize["tree"]);ratios.edit(b,{"redo","DP-1"});check(ratios.show(b,"DP-1")["tree"]==equalized["tree"]);
 ratios.edit(b,{"mirror-slots","DP-1","x"});check(ratios.show(b,"DP-1")["tree"]!=equalized["tree"]);ratios.edit(b,{"undo","DP-1"});check(ratios.show(b,"DP-1")["tree"]==equalized["tree"]);
 // An undo that cannot fit a changed output is atomic and does not consume its revision.
 ContextStore fit;fit.edit(b,{"add","DP-1","1"});fit.edit(b,{"padding","DP-1","200"});fit.edit(b,{"padding","DP-1","0"});auto original=fit.state();auto originalHistory=fit.show(b,"DP-1")["history"].toObject();auto outputs=s["outputs"].toArray();auto output=outputs[0].toObject();output["geometry"]=rect(0,0,100,100);output["work-area"]=output["geometry"];outputs[0]=output;s["outputs"]=outputs;s["sequence"]="2";b.accept(QJsonDocument(s).toJson());rejects([&]{fit.edit(b,{"undo","DP-1"});});check(fit.state()==original);check(fit.show(b,"DP-1")["history"].toObject()==originalHistory);
 std::cout<<"PASS undo/redo, forward generations, no-op/failed edit preservation, membership barriers, bounded history and node identities\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
