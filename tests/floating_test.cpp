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
 const QString desktop="11111111-1111-4111-8111-111111111111";
 QJsonArray windows{window(1),window(2)};
 QJsonObject s{{"protocol",1},{"instance","33333333-3333-4333-8333-333333333333"},{"sequence","1"},{"kwin-version","6.8.1"},{"capabilities",QJsonArray{"snapshot","contexts","geometry"}},{"geometry-window-limit",20},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"scale",1},{"geometry",rect(-100,30,1200,800)},{"work-area",rect(-100,30,1200,800)},{"current-desktop",desktop},{"current-activity","work"}}}},{"windows",windows}};
 BridgeState b;b.accept(QJsonDocument(s).toJson());ContextStore store;store.edit(b,{"add","DP-1","1"});auto original=store.state();check(original["version"]==QJsonValue(8));check(store.show(b,"DP-1")["padding"]==QJsonValue(QJsonArray{10,10,10,10}));
 store.edit(b,{"add","DP-1","2"});store.edit(b,{"padding","DP-1","20"});check(store.show(b,"DP-1")["history"].toObject()["undo"].toInt()>0);
 const auto uuid=window(1)["uuid"].toString();store.edit(b,{"float","DP-1","1","on"});auto floated=store.show(b,"DP-1");check(floated["members"].toArray().size()==1&&floated["floating"].toArray()==QJsonArray{uuid});check(floated["history"].toObject()["undo"].toInt()==0);
 auto preview=store.reconcile(b,"DP-1");check(preview["add"].toArray().isEmpty());check(preview["excluded"].toArray()[0].toObject()["reason"]==QJsonValue("explicit-float"));
 auto before=store.state();store.edit(b,{"float","DP-1","1","on"});check(store.state()==before);rejects([&]{store.edit(b,{"add","DP-1","1"});});check(store.state()==before);check(store.testRule(b,"1")["explicit-floating"].toBool());
 check(ContextStore::restore(before).state()==before);auto broken=before;auto cs=broken["contexts"].toArray();auto c=cs[0].toObject();c["floating"]=QJsonArray{window(2)["uuid"]};cs[0]=c;broken["contexts"]=cs;rejects([&]{ContextStore::restore(broken);});
 store.editRules({"add","rule","float","--app","test"});store.edit(b,{"float","DP-1",uuid,"off"});check(store.show(b,"DP-1")["members"].toArray().size()==2);check(store.show(b,"DP-1")["floating"].toArray().isEmpty());
 before=store.state();store.edit(b,{"float","DP-1","1","off"});check(store.state()==before);
 auto v3=before;v3["version"]=3;cs=v3["contexts"].toArray();c=cs[0].toObject();c.remove("floating");cs[0]=c;v3["contexts"]=cs;check(ContextStore::restore(v3).state()==before);
 store.edit(b,{"float","DP-1","1","on"});windows.removeFirst();s["windows"]=windows;s["sequence"]="2";b.accept(QJsonDocument(s).toJson());check(store.reconcile(b,"DP-1")["forget-floating"].toArray()==QJsonArray{uuid});store.edit(b,{"reconcile","DP-1"});check(store.show(b,"DP-1")["floating"].toArray().isEmpty());
 std::cout<<"PASS explicit floating membership, exclusions, idempotence, rule override, cleanup and state migration\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
