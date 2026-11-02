#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("lifecycle assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject rect(QRectF r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
QString id(int n){return QString("dddddddd-dddd-4ddd-8ddd-%1").arg(n,12,16,QChar('0'));}
int main(){try{
 const QString a="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",b="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",c="cccccccc-cccc-4ccc-8ccc-cccccccccccc",instance=id(100),context=id(101);
 const QRectF originalA(100,100,600,500),originalB(800,200,700,600),originalC(300,400,800,700);
 QMap<QString,QRectF> frames{{a,originalA},{b,originalB}};bool failC=false,failRelease=false;int writes=0;const QRectF movedA(300,300,600,500);
 GeometryTransactions engine([&](const QString &key)->std::optional<QRectF>{if(!frames.contains(key))return {};return frames[key];},[&](const QString &key,QRectF target){++writes;if(failRelease&&key==b&&target.x()==10)return false;if(key==c&&failC&&target.y()==10)return false;frames[key]=target;return true;});
 auto snapshot=[&]{QJsonArray ws;for(auto it=frames.begin();it!=frames.end();++it)ws.append(QJsonObject{{"uuid",it.key()},{"frame",rect(it.value())},{"candidate",true},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"minimum",QJsonObject{{"w",10},{"h",10}}}});return QJsonObject{{"instance",instance},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(QRectF(0,0,2560,1440))}}}},{"windows",ws}};};
 auto request=[&](int n,QStringList members,int prior,bool lifecycle=true){QJsonArray ps;for(int i=0;i<members.size();++i)ps.append(QJsonObject{{"uuid",members[i]},{"expected-frame",rect(frames[members[i]])},{"frame",rect(QRectF(10+i*800,10,780,1400))}});QJsonObject r{{"protocol",1},{"id",id(n)},{"request-sequence",QString::number(n)},{"bridge-instance",instance},{"context-id",context},{"context-generation",QString::number(n)},{"context",QJsonObject{{"output-id","output"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",rect(QRectF(0,0,2560,1440))},{"placements",ps}};if(prior)r["supersedes"]=id(prior);if(lifecycle)r["lifecycle"]=true;return r;};
 auto confirm=[&]{engine.tick(40);engine.tick(80);engine.tick(120);};
 auto release=[&](int n,QStringList members,int prior,QString uuid){auto r=request(n,members,prior);r["release"]=QJsonArray{QJsonObject{{"uuid",uuid},{"expected-frame",rect(frames.value(uuid))}}};return r;};
 engine.apply("owner",request(1,{a,b},0),snapshot(),0);confirm();
 auto before=writes;frames[c]=originalC;auto invalid=release(2,{b},1,c);rejects([&]{engine.apply("owner",invalid,snapshot(),0);});check(writes==before);
 auto duplicate=release(2,{a,b},1,a);rejects([&]{engine.apply("owner",duplicate,snapshot(),0);});check(writes==before);
 auto stale=release(2,{b},1,a);auto entries=stale["release"].toArray();auto entry=entries[0].toObject();entry["expected-frame"]=rect(originalA);entries[0]=entry;stale["release"]=entries;rejects([&]{engine.apply("owner",stale,snapshot(),0);});check(writes==before);
 auto constrained=snapshot();auto ws=constrained["windows"].toArray();auto changed=ws[0].toObject();changed["minimum"]=QJsonObject{{"w",700},{"h",10}};ws[0]=changed;constrained["windows"]=ws;check(writes==before);
 // The released target may overlap tiled frames; it is ordinary floating geometry.
 auto released=release(2,{b},1,a);const auto held=frames[a];engine.apply("owner",released,constrained,0);check(frames[a]==held);confirm();check(engine.status("owner",id(2))["managed-windows"].toArray()==QJsonArray{b});
 frames[a]=movedA;before=writes;engine.apply("owner",released,snapshot(),0);check(writes==before&&frames[a]==movedA);engine.apply("owner",request(3,{a,b},2),snapshot(),0);confirm();const auto tiledA=frames[a],tiledB=frames[b];
 // A failed release restores the previous complete layout and its ownership.
 failRelease=true;engine.apply("owner",release(4,{b},3,a),snapshot(),0);confirm();check(engine.status("owner",id(4))["state"]==QJsonValue("rolled_back"));check(engine.status("owner",id(3))["state"]==QJsonValue("applied"));check(frames[a]==tiledA&&frames[b]==tiledB);failRelease=false;
 engine.forget("owner",id(3));confirm();check(frames[a]==tiledA&&frames[b]==tiledB);
 engine.apply("owner",request(5,{b},0),snapshot(),0);confirm();engine.apply("owner",release(6,{},5,b),snapshot(),0);confirm();check(frames[b]==QRectF(10,10,780,1400));check(engine.status("owner",id(6))["managed-windows"].toArray().isEmpty());
 // Completed release drops ownership: shutdown/owner loss must preserve later user movement.
 frames[b].translate(50,50);auto manual=frames[b];engine.release("owner",0);confirm();engine.teardown();check(frames[b]==manual);
 // A replacement builder derives releases only from explicit floating markers and owned UUIDs.
 QJsonObject plan{{"bridge-instance",instance},{"floating",QJsonArray{a}},{"placements",QJsonArray{}},{"context",QJsonObject{}},{"work-area",QJsonObject{}}};
 auto built=geometryRequest(plan,snapshot(),id(7),QJsonObject{{"managed-windows",QJsonArray{a,b}}});check(built["release"].toArray().size()==1);check(built["release"].toArray()[0].toObject()["uuid"]==QJsonValue(a));
 std::cout<<"PASS explicit release validation, current-frame preservation, failed-release rollback, readmission and dropped ownership\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
