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
 QMap<QString,QRectF> frames{{a,originalA},{b,originalB}};bool failC=false;int writes=0;
 GeometryTransactions engine([&](const QString &key)->std::optional<QRectF>{if(!frames.contains(key))return {};return frames[key];},[&](const QString &key,QRectF target){++writes;if(key==c&&failC&&target.y()==10)return false;frames[key]=target;return true;});
 auto snapshot=[&]{QJsonArray ws;for(auto it=frames.begin();it!=frames.end();++it)ws.append(QJsonObject{{"uuid",it.key()},{"frame",rect(it.value())},{"candidate",true},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"minimum",QJsonObject{{"w",10},{"h",10}}}});return QJsonObject{{"instance",instance},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(QRectF(0,0,2560,1440))}}}},{"windows",ws}};};
 auto request=[&](int n,QStringList members,int prior,bool lifecycle=true){QJsonArray ps;for(int i=0;i<members.size();++i)ps.append(QJsonObject{{"uuid",members[i]},{"expected-frame",rect(frames[members[i]])},{"frame",rect(QRectF(10+i*800,10,780,1400))}});QJsonObject r{{"protocol",1},{"id",id(n)},{"request-sequence",QString::number(n)},{"bridge-instance",instance},{"context-id",context},{"context-generation",QString::number(n)},{"context",QJsonObject{{"output-id","output"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",rect(QRectF(0,0,2560,1440))},{"placements",ps}};if(prior)r["supersedes"]=id(prior);if(lifecycle)r["lifecycle"]=true;return r;};
 auto confirm=[&]{engine.tick(40);engine.tick(80);engine.tick(120);};
 engine.apply("owner",request(1,{a},0),snapshot(),0);confirm();
 // Existing live members cannot silently leave the transaction.
 auto before=writes;rejects([&]{engine.apply("owner",request(2,{b},1),snapshot(),0);});check(before==writes);
 auto incomplete=snapshot();auto observed=incomplete["windows"].toArray();for(int i=observed.size()-1;i>=0;--i)if(observed[i].toObject()["uuid"]==QJsonValue(a))observed.removeAt(i);incomplete["windows"]=observed;
 rejects([&]{engine.apply("owner",request(2,{b},1),incomplete,0);});check(before==writes); // Reader still sees the supposedly closed member.
 rejects([&]{engine.apply("owner",request(2,{a,b},1,false),snapshot(),0);});check(before==writes);
 engine.apply("owner",request(2,{a,b},1),snapshot(),0);confirm();engine.forget("owner",id(2));confirm();check(frames[a]==QRectF(10,10,780,1400)&&frames[b]==QRectF(810,10,780,1400));
 engine.apply("owner",request(3,{a,b},0),snapshot(),0);confirm();frames.remove(a);
 engine.apply("owner",request(4,{b},3),snapshot(),0);confirm();check(engine.status("owner",id(4))["state"]==QJsonValue("applied"));auto preceding=frames[b];
 // Newly admitted windows roll back to their own floating frames on failure.
 frames[c]=originalC;failC=true;engine.apply("owner",request(5,{b,c},4),snapshot(),0);confirm();check(engine.status("owner",id(5))["state"]==QJsonValue("rolled_back"));check(frames[b]==preceding&&frames[c]==originalC);check(engine.status("owner",id(4))["state"]==QJsonValue("applied"));failC=false;
 engine.forget("owner",id(4));confirm();check(frames[b]==preceding);
 engine.apply("owner",request(6,{b},0),snapshot(),0);confirm();frames.remove(b);engine.apply("owner",request(7,{},6),snapshot(),0);confirm();check(engine.status("owner",id(7))["state"]==QJsonValue("applied"));
 engine.apply("owner",request(8,{c},7),snapshot(),0);confirm();engine.release("owner",0);confirm();check(frames[c]==QRectF(10,10,780,1400));
 std::cout<<"PASS lifecycle admission, closure, live-removal rejection, empty replacement, failure rollback and no-move ownership release\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
