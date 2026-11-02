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
 QJsonArray windows;for(int n=1;n<=4;++n){auto w=window(n);w["focused"]=n==1;windows.append(w);}
 QJsonObject s{{"protocol",1},{"instance","33333333-3333-4333-8333-333333333333"},{"sequence","1"},{"kwin-version","6.8.1"},{"capabilities",QJsonArray{"snapshot","contexts"}},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"scale",1},{"geometry",rect(-2560,-100,2560,1440)},{"work-area",rect(-2560,-60,2560,1400)},{"current-desktop",desktop},{"current-activity","work"}}}},{"windows",windows}};
 BridgeState bridge;bridge.accept(QJsonDocument(s).toJson());ContextStore contexts;
 contexts.edit(bridge,{"add","focused","focused"});contexts.edit(bridge,{"add","focused","2","focused","east","0.6"});contexts.edit(bridge,{"add","DP-1","3","2","south"});contexts.edit(bridge,{"add","DP-1","4","1","east"});
 check(contexts.show(bridge,"focused")["members"].toArray().size()==4);
 check(contexts.neighbor(bridge,"focused","focused","east")["id"]==QJsonValue("4"));check(contexts.neighbor(bridge,"DP-1","2","south")["id"]==QJsonValue("3"));check(contexts.neighbor(bridge,"DP-1","3","north")["id"]==QJsonValue("2"));check(contexts.neighbor(bridge,"DP-1","4","west")["id"]==QJsonValue("1"));
 check(contexts.neighbor(bridge,"DP-1","4","east")["id"]==QJsonValue("2")); // aligned tie resolved by UUID
 auto saved=contexts.state();rejects([&]{contexts.neighbor(bridge,"DP-1","focused","west");});rejects([&]{contexts.neighbor(bridge,"DP-1","east","north");});rejects([&]{contexts.neighbor(bridge,"DP-1","focused","diagonal");});check(contexts.state()==saved);
 auto first=contexts.plan(bridge,"DP-1")["placements"].toArray()[0].toObject();contexts.edit(bridge,{"swap","focused","focused","east"});auto after=contexts.plan(bridge,"DP-1")["placements"].toArray()[0].toObject();check(first["frame"]==after["frame"]);check(after["id"]==QJsonValue("4"));contexts.edit(bridge,{"swap","DP-1","1","4"});
 // Hidden stack members must not win directional selection, or be accepted as a visible source.
 contexts.edit(bridge,{"stack","DP-1","3","2"});check(contexts.neighbor(bridge,"DP-1","4","east")["id"]==QJsonValue("3"));rejects([&]{contexts.neighbor(bridge,"DP-1","2","west");});
 // Missing or ambiguous focus fails atomically rather than choosing an arbitrary member.
 auto noFocus=windows;auto w=noFocus[0].toObject();w["focused"]=false;noFocus[0]=w;s["windows"]=noFocus;s["sequence"]="2";bridge.accept(QJsonDocument(s).toJson());saved=contexts.state();rejects([&]{contexts.edit(bridge,{"ratio","DP-1","focused","x","0.7"});});rejects([&]{contexts.show(bridge,"focused");});check(contexts.state()==saved);
 auto ambiguous=windows;w=ambiguous[1].toObject();w["focused"]=true;ambiguous[1]=w;s["windows"]=ambiguous;s["sequence"]="3";bridge.accept(QJsonDocument(s).toJson());rejects([&]{contexts.edit(bridge,{"swap","DP-1","focused","east"});});check(contexts.state()==saved);
 // Geometry and selection stay valid on a portrait output with a negative origin.
 auto outputs=s["outputs"].toArray();auto output=outputs[0].toObject();output["geometry"]=rect(-1080,0,1080,1920);output["work-area"]=rect(-1080,0,1080,1920);outputs[0]=output;s["outputs"]=outputs;s["windows"]=windows;s["sequence"]="4";bridge.accept(QJsonDocument(s).toJson());check(contexts.neighbor(bridge,"DP-1","4","west")["id"]==QJsonValue("1"));
 std::cout<<"PASS focused admission/output/member selection, directional ranking, hidden stacks, negative origins and atomic focus rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
