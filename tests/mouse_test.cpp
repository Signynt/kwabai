#include "geometry.h"
#include "../src/daemon/mouse_policy.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("mouse assertion failed");}
template<class F>void rejects(F fn){bool failed=false;try{fn();}catch(const std::invalid_argument &){failed=true;}check(failed);}
QJsonObject rect(QRectF r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
QString id(int i){return QString("dddddddd-dddd-4ddd-8ddd-%1").arg(i,12,16,QChar('0'));}
int main(){try{
 for(auto tile:{QRectF(0,0,1000,100),QRectF(-1080,0,100,1000)}) {
  auto point=[&](double x,double y){return QPointF(tile.x()+x*tile.width(),tile.y()+y*tile.height());};
  check(mouseDropDirection(tile,point(0.5,0.5)).isEmpty());check(mouseDropDirection(tile,point(0.25,0.25)).isEmpty());
  check(mouseDropDirection(tile,point(0.1,0.5))=="west");check(mouseDropDirection(tile,point(0.9,0.5))=="east");check(mouseDropDirection(tile,point(0.5,0.1))=="north");check(mouseDropDirection(tile,point(0.5,0.9))=="south");check(mouseDropDirection(tile,point(0.1,0.1))=="west");
 }
 QString a=id(101),b=id(102);QMap<QString,QRectF> frames{{a,{100,100,800,600}},{b,{1000,100,800,600}}};const auto originals=frames;bool refuse=false;int writes=0;
 GeometryTransactions engine([&](const QString &uuid)->std::optional<QRectF>{if(!frames.contains(uuid))return {};return frames[uuid];},[&](const QString &uuid,const QRectF &r){++writes;if(refuse&&r.width()==1300)return false;frames[uuid]=r;return true;});
 auto snapshot=[&]{QJsonArray windows;for(auto uuid:{a,b})windows.append(QJsonObject{{"uuid",uuid},{"candidate",true},{"minimized",false},{"fullscreen",false},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"minimum",QJsonObject{{"w",100},{"h",100}}},{"frame",rect(frames[uuid])}});return QJsonObject{{"instance",id(100)},{"outputs",QJsonArray{QJsonObject{{"id","out"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect({0,0,2560,1440})}}}},{"windows",windows}};};
 auto request=[&](int n,int prior,double width=1265){QJsonArray placements{QJsonObject{{"uuid",a},{"expected-frame",rect(frames[a])},{"frame",rect({10,10,width,1420})}},QJsonObject{{"uuid",b},{"expected-frame",rect(frames[b])},{"frame",rect({width+20,10,2530-width,1420})}}};QJsonObject r{{"protocol",1},{"id",id(n)},{"request-sequence",QString::number(n)},{"bridge-instance",id(100)},{"context-id",id(200)},{"context-generation",QString::number(n)},{"context",QJsonObject{{"output-id","out"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",rect({0,0,2560,1440})},{"placements",placements}};if(prior)r["supersedes"]=id(prior);return r;};
 auto confirm=[&](int now){engine.tick(now);engine.tick(now+40);engine.tick(now+80);};
 engine.apply("owner",request(1,0),snapshot(),0);confirm(40);const auto first=frames;
 engine.beginInteraction(a,"move",150);auto gesture=engine.interaction(a,150);check(gesture["state"]=="active");auto token=QJsonObject{{"id",gesture["id"]},{"window",a}};
 auto update=request(2,1);update["interaction"]=token;int before=writes;rejects([&]{engine.apply("owner",update,snapshot(),180);});check(writes==before);
 frames[a].translate(200,100);engine.finishInteraction(a,{2000,500},200);gesture=engine.interaction(a,200);check(gesture["state"]=="finished");check(gesture["pointer"].toObject()["x"]==2000);
 update=request(2,1,1400);before=writes;rejects([&]{engine.apply("owner",update,snapshot(),220);});update["interaction"]=QJsonObject{{"id",id(999)},{"window",a}};rejects([&]{engine.apply("owner",update,snapshot(),220);});update["interaction"]=token;rejects([&]{engine.apply("stranger",update,snapshot(),220);});
 frames[b].translate(10,0);update=request(2,1,1400);update["interaction"]=token;rejects([&]{engine.apply("owner",update,snapshot(),220);});check(writes==before);frames[b]=first[b];
 update=request(2,1,1400);update["interaction"]=token;engine.apply("owner",update,snapshot(),240);confirm(280);check(engine.status("owner",id(2))["state"]=="applied");check(engine.interaction(a,350).isEmpty());
 // Consumed authorization cannot permit a second manual movement or another generation.
 frames[a].translate(10,0);auto reuse=request(3,2);reuse["interaction"]=token;rejects([&]{engine.apply("owner",reuse,snapshot(),400);});frames[a]=QRectF(10,10,1400,1420);
 const auto kept=frames;engine.forget("owner",id(2));confirm(490);check(frames==kept);
 engine.apply("owner",request(4,0),snapshot(),600);confirm(640);engine.beginInteraction(a,"resize",750);frames[a].setWidth(1400);engine.finishInteraction(a,{1400,400},800);auto expired=request(5,4);expired["interaction"]=QJsonObject{{"id",engine.interaction(a,800)["id"]},{"window",a}};before=writes;rejects([&]{engine.apply("owner",expired,snapshot(),5900);});check(writes==before);
 // A new authorization is only issued if the gesture starts at the native target.
 engine.beginInteraction(a,"resize",6000);check(engine.interaction(a,6000).isEmpty());frames[a]=first[a];engine.beginInteraction(a,"resize",6100);frames[a].setWidth(1350);engine.finishInteraction(a,{1350,400},6150);auto rollback=frames[a];auto failed=request(6,4,1300);failed["interaction"]=QJsonObject{{"id",engine.interaction(a,6150)["id"]},{"window",a}};refuse=true;engine.apply("owner",failed,snapshot(),6200);confirm(6240);check(engine.status("owner",id(6))["state"]=="rolled_back");check(frames[a]==rollback);check(engine.interaction(a,6350).isEmpty());
 std::cout<<"PASS native gesture authorization, unchanged peers, single use, expiry, ownership, no-move release and rollback to the gesture endpoint\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
