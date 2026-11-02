#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("drop preview assertion failed");}
template<class F>void rejects(F fn){bool fail=false;try{fn();}catch(const std::invalid_argument &){fail=true;}check(fail);}
QString id(int i){return QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(i,12,16,QChar('0'));}
QJsonObject rect(QRectF r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
int main(){try{
 auto a=id(1),b=id(2);QMap<QString,QRectF> frames{{a,{100,100,800,600}},{b,{1000,100,800,600}}};int writes=0;
 GeometryTransactions engine([&](const QString &uuid)->std::optional<QRectF>{if(!frames.contains(uuid))return {};return frames[uuid];},[&](const QString &uuid,QRectF r){++writes;frames[uuid]=r;return true;});
 QJsonObject output{{"id","screen"},{"name","DP-1"},{"current-desktop",id(9)},{"current-activity","work"},{"work-area",rect({-1080,0,3640,1920})}};
 QJsonObject context{{"output-id","screen"},{"output","DP-1"},{"desktop",id(9)},{"activity","work"}};
 auto snapshot=[&]{QJsonArray windows;for(auto uuid:{a,b})windows.append(QJsonObject{{"uuid",uuid},{"frame",rect(frames[uuid])},{"candidate",uuid!=a},{"minimized",false},{"fullscreen",false},{"output","DP-1"},{"desktops",QJsonArray{id(9)}},{"activities",QJsonArray{"work"}},{"minimum",QJsonObject{{"w",100},{"h",100}}}});return QJsonObject{{"instance",id(8)},{"outputs",QJsonArray{output}},{"windows",windows}};};
 auto initial=snapshot();auto windows=initial["windows"].toArray();auto w=windows[0].toObject();w["candidate"]=true;windows[0]=w;initial["windows"]=windows;
 QJsonArray placements;for(auto uuid:{a,b})placements.append(QJsonObject{{"uuid",uuid},{"expected-frame",rect(frames[uuid])},{"frame",rect(frames[uuid])}});
 QJsonObject request{{"protocol",1},{"id",id(10)},{"request-sequence","1"},{"bridge-instance",id(8)},{"context-id",id(11)},{"context-generation","1"},{"context",context},{"work-area",output["work-area"]},{"placements",placements}};
 engine.apply("owner",request,initial,0);engine.tick(40);engine.tick(80);engine.tick(120);engine.beginInteraction(a,"move",150);auto gesture=engine.interaction(a,150);frames[a].translate(50,50);int before=writes;
 QJsonObject preview{{"protocol",1},{"request-sequence","1"},{"bridge-instance",id(8)},{"context-id",id(11)},{"transaction",id(10)},{"window",a},{"interaction",gesture["id"]},{"context",context},{"work-area",output["work-area"]},{"frame",rect(frames[b])},{"operation","swap"},{"direction",""}};
 check(engine.setDropPreview("owner",preview,snapshot(),160)["visible"].toBool());check(writes==before);check(engine.dropPreview(snapshot(),759)["visible"].toBool());check(!engine.dropPreview(snapshot(),760)["visible"].toBool());
 auto bad=preview;bad["request-sequence"]="2";bad["interaction"]=id(99);rejects([&]{engine.setDropPreview("owner",bad,snapshot(),200);});rejects([&]{engine.setDropPreview("stranger",preview,snapshot(),200);});rejects([&]{engine.setDropPreview("owner",preview,snapshot(),200);});
 bad=preview;bad["request-sequence"]="2";bad["frame"]=rect({-2000,0,100,100});rejects([&]{engine.setDropPreview("owner",bad,snapshot(),200);});bad=preview;bad["request-sequence"]="2";bad["operation"]="insert";bad["direction"]="invalid";rejects([&]{engine.setDropPreview("owner",bad,snapshot(),200);});
 bad=preview;bad["request-sequence"]="2";bad["color"]="#ff0000";rejects([&]{engine.setDropPreview("owner",bad,snapshot(),200);});
 auto differentInstance=snapshot();differentInstance["instance"]=id(77);check(!engine.dropPreview(differentInstance,200)["visible"].toBool());
 auto changed=snapshot();auto outputs=changed["outputs"].toArray();auto other=output;other["current-desktop"]=id(7);outputs[0]=other;changed["outputs"]=outputs;check(!engine.dropPreview(changed,200)["visible"].toBool());
 frames[b].translate(2,0);check(!engine.dropPreview(snapshot(),200)["visible"].toBool());frames[b].translate(-2,0);
 QJsonObject clear{{"protocol",1},{"bridge-instance",id(8)},{"request-sequence","1"},{"clear",true},{"context-id",id(11)}};
 check(engine.setDropPreview("stranger",clear,snapshot(),200)["visible"].toBool());clear["request-sequence"]="2";clear["context-id"]=id(66);check(engine.setDropPreview("owner",clear,snapshot(),200)["visible"].toBool());clear["request-sequence"]="3";clear["context-id"]=id(11);check(!engine.setDropPreview("owner",clear,snapshot(),200)["visible"].toBool());
 preview["request-sequence"]="4";preview["color"]="0x80d75f5f";preview["operation"]="insert";preview["direction"]="west";check(engine.setDropPreview("owner",preview,snapshot(),210)["visible"].toBool());engine.finishInteraction(a,{1500,500},220);check(!engine.dropPreview(snapshot(),220)["visible"].toBool());preview["request-sequence"]="5";rejects([&]{engine.setDropPreview("owner",preview,snapshot(),230);});
 frames[a].translate(-50,-50);engine.beginInteraction(a,"move",240);preview["interaction"]=engine.interaction(a,240)["id"];check(engine.setDropPreview("owner",preview,snapshot(),250)["visible"].toBool());engine.release("owner",260);check(!engine.dropPreview(snapshot(),260)["visible"].toBool());
 check(writes==before);std::cout<<"PASS read-only preview ownership, sequence, scope, bounds, lease, context/peer changes, clear, finish and owner loss\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
