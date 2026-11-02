#include "store.h"
#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRectF>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <source_location>
using namespace kwabai;
void check(bool ok,std::source_location where=std::source_location::current()){if(!ok)throw std::runtime_error("pixel plan assertion failed at line "+std::to_string(where.line()));}
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
QString id(int n){return QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'));}
int main(){try{
 for(auto area:{rect(-1080,0,1081,1919),rect(1080,238,2559,1407)}) {
  QJsonArray windows;for(int n=1;n<=3;++n)windows.append(QJsonObject{{"uuid",id(n)},{"app",n==1?"firefox":"kitty"},{"title","test"},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"frame",rect(100,100,800,600)},{"minimum",QJsonObject{{"w",n==1?150:80},{"h",n==1?75:120}}},{"candidate",true},{"minimized",false},{"fullscreen",false},{"focused",false}});
  QJsonObject snapshot{{"protocol",1},{"instance",id(100)},{"sequence","1"},{"kwin-version","test"},{"capabilities",QJsonArray{"snapshot","contexts","geometry"}},{"geometry-window-limit",20},{"outputs",QJsonArray{QJsonObject{{"id","screen"},{"name","DP-1"},{"scale",1},{"geometry",area},{"work-area",area},{"current-desktop",id(99)},{"current-activity","work"}}}},{"windows",windows}};
  BridgeState bridge;bridge.accept(QJsonDocument(snapshot).toJson());ContextStore store;store.edit(bridge,{"add","DP-1","1"});store.edit(bridge,{"add","DP-1","2","1","east","0.63"});store.edit(bridge,{"add","DP-1","3","2","south","0.43"});
  auto tileFrames=[&]{QStringList result;auto plan=store.plan(bridge,"DP-1");check(plan["pixel-aligned"].toBool());validateGeometryRequest(geometryRequest(plan,bridge.windows(),id(101)),snapshot);for(auto v:plan["placements"].toArray()){auto f=v.toObject()["frame"].toObject();for(auto k:{"x","y","w","h"})check(f[k].toDouble()==std::floor(f[k].toDouble()));result.append(QString::fromUtf8(QJsonDocument(f).toJson(QJsonDocument::Compact)));}result.sort();return result;};
  auto initial=tileFrames();auto tree=store.show(bridge,"DP-1").value("tree");
  for(int i=0;i<20;++i){store.edit(bridge,{"swap","DP-1","1","2"});check(tileFrames()==initial);}
  check(store.show(bridge,"DP-1")["tree"]==tree);
  auto saved=store.state();(void)store.plan(bridge,"DP-1");check(store.state()==saved);
  auto restored=ContextStore::restore(saved);check(restored.plan(bridge,"DP-1")==store.plan(bridge,"DP-1"));
  // Fractional client minima and fractional output origins retain the precise layout.
  auto fractional=windows;auto first=fractional[0].toObject();first["minimum"]=QJsonObject{{"w",150.5},{"h",75}};fractional[0]=first;snapshot["windows"]=fractional;snapshot["sequence"]="2";bridge.accept(QJsonDocument(snapshot).toJson());check(!store.plan(bridge,"DP-1")["pixel-aligned"].toBool());
  snapshot["windows"]=windows;auto outputs=snapshot["outputs"].toArray();auto output=outputs[0].toObject();auto shifted=area;shifted["x"]=area["x"].toDouble()+0.25;output["geometry"]=shifted;output["work-area"]=shifted;outputs[0]=output;snapshot["outputs"]=outputs;snapshot["sequence"]="3";bridge.accept(QJsonDocument(snapshot).toJson());check(!store.plan(bridge,"DP-1")["pixel-aligned"].toBool());
 }
 std::cout<<"PASS shared pixel boundaries, negative origins, odd dimensions, min constraints, repeated swaps without drift and fractional-input fallback\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
