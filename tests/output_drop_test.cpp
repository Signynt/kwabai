#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("native output drop assertion failed");}
template<class F>void rejects(F fn){bool fail=false;try{fn();}catch(const std::invalid_argument &){fail=true;}check(fail);}
QString id(int n){return QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'));}
QJsonObject rect(QRectF r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
int main(){try{
 auto a=id(1),b=id(2);QMap<QString,QRectF> frames{{a,{100,100,800,600}},{b,{1000,100,800,600}}};int writes=0;bool dropped=false;
 GeometryTransactions engine([&](const QString &uuid)->std::optional<QRectF>{if(!frames.contains(uuid))return {};return frames[uuid];},[&](const QString &uuid,QRectF r){++writes;frames[uuid]=r;return true;});
 QJsonObject output{{"id","one"},{"name","DP-1"},{"current-desktop",id(9)},{"current-activity","work"},{"geometry",rect({0,0,2560,1440})},{"work-area",rect({0,0,2560,1440})}};
 auto destination=output;destination["id"]="two";destination["name"]="DP-2";destination["geometry"]=destination["work-area"]=rect({-1080,0,1080,1920});
 auto snapshot=[&]{QJsonArray windows;for(auto uuid:{a,b})windows.append(QJsonObject{{"uuid",uuid},{"frame",rect(frames[uuid])},{"candidate",true},{"minimized",false},{"fullscreen",false},{"output",dropped&&uuid==a?"DP-2":"DP-1"},{"desktops",QJsonArray{id(9)}},{"activities",QJsonArray{"work"}},{"minimum",QJsonObject{{"w",100},{"h",100}}}});return QJsonObject{{"instance",id(8)},{"outputs",QJsonArray{output,destination}},{"windows",windows}};};
 QJsonArray placements;for(auto uuid:{a,b})placements.append(QJsonObject{{"uuid",uuid},{"expected-frame",rect(frames[uuid])},{"frame",rect(uuid==a?QRectF(10,10,1265,1420):QRectF(1285,10,1265,1420))}});
 QJsonObject request{{"protocol",1},{"id",id(10)},{"request-sequence","1"},{"bridge-instance",id(8)},{"context-id",id(11)},{"context-generation","1"},{"context",QJsonObject{{"output-id","one"},{"output","DP-1"},{"desktop",id(9)},{"activity","work"}}},{"work-area",output["work-area"]},{"placements",placements}};
 engine.apply("owner",request,snapshot(),0);engine.tick(40);engine.tick(80);engine.tick(120);engine.beginInteraction(a,"move",150);auto gesture=engine.interaction(a,150);dropped=true;frames[a]={-1000,100,800,600};const auto endpoint=frames[a];engine.finishInteraction(a,{-500,400},160);
 auto release=request;release["id"]=id(12);release["request-sequence"]="2";release["context-generation"]="2";release["supersedes"]=id(10);release["lifecycle"]=true;release["interaction-release"]=true;release["interaction"]=QJsonObject{{"window",a},{"id",gesture["id"]}};release["drop-output"]=destination;release["release"]=QJsonArray{QJsonObject{{"uuid",a},{"expected-frame",rect(endpoint)}}};release["placements"]=QJsonArray{QJsonObject{{"uuid",b},{"expected-frame",rect(frames[b])},{"frame",rect({10,10,2540,1420})}}};
 auto before=writes;auto bad=release;bad.remove("interaction");rejects([&]{engine.apply("owner",bad,snapshot(),200);});bad=release;bad.remove("lifecycle");rejects([&]{engine.apply("owner",bad,snapshot(),200);});bad=release;bad["interaction"]=QJsonObject{{"window",b},{"id",gesture["id"]}};rejects([&]{engine.apply("owner",bad,snapshot(),200);});rejects([&]{engine.apply("stranger",release,snapshot(),200);});
 bad=release;auto changed=destination;changed["current-activity"]="other";bad["drop-output"]=changed;rejects([&]{engine.apply("owner",bad,snapshot(),200);});check(writes==before);
 // A resize gesture and a pointer outside the pinned output cannot release
 // source ownership, even if the caller owns the prior transaction.
 frames[a]=QRectF(10,10,1265,1420);engine.beginInteraction(a,"resize",205);frames[a]=endpoint;engine.finishInteraction(a,{-500,400},206);release["interaction"]=QJsonObject{{"window",a},{"id",engine.interaction(a,206)["id"]}};rejects([&]{engine.apply("owner",release,snapshot(),207);});
 frames[a]=QRectF(10,10,1265,1420);engine.beginInteraction(a,"move",208);frames[a]=endpoint;engine.finishInteraction(a,{100,400},209);release["interaction"]=QJsonObject{{"window",a},{"id",engine.interaction(a,209)["id"]}};rejects([&]{engine.apply("owner",release,snapshot(),210);});
 frames[a]=QRectF(10,10,1265,1420);engine.beginInteraction(a,"move",211);frames[a]=endpoint;engine.finishInteraction(a,{-500,400},212);release["interaction"]=QJsonObject{{"window",a},{"id",engine.interaction(a,212)["id"]}};check(writes==before);
 engine.apply("owner",release,snapshot(),213);engine.tick(250);engine.tick(290);engine.tick(330);check(engine.status("owner",id(12))["state"]=="applied");check(frames[a]==endpoint&&!engine.owns(a)&&engine.owns(b));check(engine.interaction(a,340).isEmpty());
 auto reused=release;reused["id"]=id(13);reused["request-sequence"]="3";rejects([&]{engine.apply("owner",reused,snapshot(),350);});
 engine.forget("owner",id(12));engine.tick(440);engine.tick(480);engine.tick(520);check(frames[a]==endpoint&&frames[b]==QRectF(10,10,2540,1420));
 // A controlled destination peer can be released at its current tile for a
 // swap, without restoring its original floating frame or moving its peers.
 auto own=request;own["id"]=id(15);own["request-sequence"]="3";own["context-id"]=id(16);own["context-generation"]="1";own["context"]=QJsonObject{{"output-id","two"},{"output","DP-2"},{"desktop",id(9)},{"activity","work"}};own["work-area"]=destination["work-area"];own["placements"]=QJsonArray{QJsonObject{{"uuid",a},{"expected-frame",rect(frames[a])},{"frame",rect({-1070,10,1060,1900})}}};engine.apply("owner",own,snapshot(),600);engine.tick(640);engine.tick(680);engine.tick(720);
 const auto tile=frames[a];auto stay=own;stay["id"]=id(17);stay["request-sequence"]="4";stay["context-generation"]="2";stay["supersedes"]=id(15);stay["lifecycle"]=true;stay["placements"]=QJsonArray{};stay["release"]=QJsonArray{QJsonObject{{"uuid",a},{"expected-frame",rect(tile)}}};stay["release-mode"]="unknown";before=writes;rejects([&]{engine.apply("owner",stay,snapshot(),740);});check(writes==before);stay["release-mode"]="keep-frame";auto released=engine.apply("owner",stay,snapshot(),750);check(released["state"]=="applied"&&released["acknowledged"].toBool()&&writes==before&&!engine.owns(a));check(engine.status("owner",id(15))["state"]=="superseded");check(frames[a]==tile);engine.release("owner",760);engine.tick(800);engine.tick(840);engine.tick(880);engine.teardown();check(frames[a]==tile);
 std::cout<<"PASS output drop ownership, completed gesture, pinned context, release endpoint, replay and restore isolation\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
