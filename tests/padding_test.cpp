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
 auto legacy=original;legacy["version"]=1;auto contexts=legacy["contexts"].toArray();auto c=contexts[0].toObject();c.remove("padding");contexts[0]=c;legacy["contexts"]=contexts;check(ContextStore::restore(legacy).state()==original);
 auto frame=[&]{return store.plan(b,"DP-1")["placements"].toArray()[0].toObject()["frame"].toObject();};
 store.edit(b,{"padding","DP-1","50"});check(frame()==rect(-50,80,1100,700));
 store.edit(b,{"padding","DP-1","10","20","30","40"});check(frame()==rect(-60,40,1140,760));auto tree=store.show(b,"DP-1")["tree"].toObject();
 store.edit(b,{"padding","DP-1","left","70"});check(frame()==rect(-30,40,1110,760));check(store.show(b,"DP-1")["tree"].toObject()==tree);check(store.show(b,"DP-1")["gap"]==QJsonValue(10));
 auto saved=store.state();for(auto args:std::vector<QStringList>{{"padding","DP-1","-1"},{"padding","DP-1","NaN"},{"padding","DP-1","top","-1"},{"padding","DP-1","unknown","10"},{"padding","DP-1","600"},{"padding","DP-1","10","20","-1","40"},{"padding","DP-1","1","2","3"}}){rejects([&]{store.edit(b,args);});check(store.state()==saved);}
 check(ContextStore::restore(saved).state()==saved);
 auto v2=saved;v2["version"]=2;v2.remove("rules");check(ContextStore::restore(v2).state()==saved);
 for(auto bad:std::vector<QJsonValue>{QJsonValue(),QJsonArray{1,2,3},QJsonArray{1,2,3,-1},QJsonArray{1,2,3,"4"},QJsonObject{}}){auto corrupt=saved;auto list=corrupt["contexts"].toArray();auto item=list[0].toObject();item["padding"]=bad;list[0]=item;corrupt["contexts"]=list;rejects([&]{ContextStore::restore(corrupt);});}
 auto unsupported=saved;unsupported["version"]=9;rejects([&]{ContextStore::restore(unsupported);});
 // Padding participates in insertion, neighbor planning and requests on a negative-origin portrait area.
 auto outputs=s["outputs"].toArray();auto o=outputs[0].toObject();o["geometry"]=rect(-1080,0,1080,1920);o["work-area"]=o["geometry"];outputs[0]=o;s["outputs"]=outputs;s["sequence"]="2";b.accept(QJsonDocument(s).toJson());store.edit(b,{"add","DP-1","2"});auto plan=store.plan(b,"DP-1");check(plan["padding"]==QJsonValue(QJsonArray{10,20,30,70}));
 for(auto v:plan["placements"].toArray()){auto r=v.toObject()["frame"].toObject();check(r["x"].toDouble()>=-1010&&r["y"].toDouble()>=10);check(r["x"].toDouble()+r["w"].toDouble()<=-20.0+0.001);check(r["y"].toDouble()+r["h"].toDouble()<=1890.0+0.001);}
 check(store.neighbor(b,"DP-1","1","south")["id"]==QJsonValue("2"));validateGeometryRequest(geometryRequest(plan,b.windows(),"44444444-4444-4444-8444-444444444444"),s);
 store.edit(b,{"padding","DP-1","0"});check(store.show(b,"DP-1")["padding"]==QJsonValue(QJsonArray{0,0,0,0}));
 std::cout<<"PASS uniform/asymmetric/edge padding, v1/v2 migration, v8 validation, atomic rejection and portrait request geometry\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
