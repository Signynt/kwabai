#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("replacement transaction assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject rect(const QRectF &r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
const QString uuid="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",instance="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",context="cccccccc-cccc-4ccc-8ccc-cccccccccccc";
QString id(int i){return QString("dddddddd-dddd-4ddd-8ddd-%1").arg(i,12,16,QChar('0'));}
int main(){try{
 QRectF actual(100,100,800,600);bool reject=false;int writes=0;
 GeometryTransactions transactions([&](const QString &)->std::optional<QRectF>{return actual;},[&](const QString &,const QRectF &target){++writes;if(reject&&target.width()==2200)return false;actual=target;return true;});
 QJsonObject o{{"id","output"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(QRectF(0,0,2560,1440))}};
 auto snapshot=[&]{return QJsonObject{{"instance",instance},{"outputs",QJsonArray{o}},{"windows",QJsonArray{QJsonObject{{"uuid",uuid},{"candidate",true},{"minimized",false},{"fullscreen",false},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"minimum",QJsonObject{{"w",100},{"h",100}}},{"frame",rect(actual)}}}}};};
 auto request=[&](int n,int generation,double width,int prior){QJsonObject r{{"protocol",1},{"request-sequence",QString::number(n)},{"id",id(n)},{"bridge-instance",instance},{"context-id",context},{"context-generation",QString::number(generation)},{"context",QJsonObject{{"output-id","output"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",o["work-area"]},{"placements",QJsonArray{QJsonObject{{"uuid",uuid},{"expected-frame",rect(actual)},{"frame",rect(QRectF(10,10,width,1400))}}}}};if(prior)r["supersedes"]=id(prior);return r;};
 auto confirm=[&](int now){transactions.tick(now);transactions.tick(now+40);transactions.tick(now+80);};
 auto first=request(1,1,1800,0);transactions.apply("owner",first,snapshot(),0);confirm(40);check(transactions.status("owner",id(1))["state"]==QJsonValue("applied"));auto firstTarget=actual;
 auto bad=request(2,1,2000,1);auto before=writes;rejects([&]{transactions.apply("owner",bad,snapshot(),200);});check(writes==before);
 bad=request(2,2,2000,1);rejects([&]{transactions.apply("stranger",bad,snapshot(),200);});check(writes==before);
 auto foreign=request(7,2,2000,1);auto members=foreign["placements"].toArray();auto foreignPlacement=members[0].toObject();foreignPlacement["uuid"]="eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee";members[0]=foreignPlacement;foreign["placements"]=members;auto foreignSnapshot=snapshot();auto foreignWindows=foreignSnapshot["windows"].toArray();auto fw=foreignWindows[0].toObject();fw["uuid"]=foreignPlacement["uuid"];foreignWindows[0]=fw;foreignSnapshot["windows"]=foreignWindows;rejects([&]{transactions.apply("owner",foreign,foreignSnapshot,200);});check(writes==before);
 auto second=request(2,2,2000,1);transactions.apply("owner",second,snapshot(),200);check(transactions.status("owner",id(1))["state"]==QJsonValue("superseding"));rejects([&]{transactions.restore("owner",id(1),210);});confirm(240);check(transactions.status("owner",id(1))["state"]==QJsonValue("superseded"));check(actual.width()==2000);rejects([&]{transactions.restore("owner",id(1),350);});
 // Failed revision returns to the preceding applied layout, not to the original floating frame.
 auto third=request(3,3,2200,2);reject=true;transactions.apply("owner",third,snapshot(),400);check(actual.width()==2000);confirm(440);check(transactions.status("owner",id(3))["state"]==QJsonValue("rolled_back"));check(transactions.status("owner",id(2))["state"]==QJsonValue("applied"));check(transactions.status("owner",id(3))["active-transaction"]==QJsonValue(id(2)));check(transactions.status("owner",id(1))["active-transaction"]==QJsonValue(id(2)));
 reject=false;auto fourth=request(4,4,2300,2);transactions.apply("owner",fourth,snapshot(),600);confirm(640);check(transactions.status("owner",id(1))["active-transaction"]==QJsonValue(id(4)));rejects([&]{transactions.restore("owner",id(4),800);});transactions.forget("owner",id(4));confirm(840);check(actual.width()==2300);check(transactions.status("owner",id(1))["active-transaction"]==QJsonValue(""));
 // Reusing an active member without an explicit revision remains forbidden.
 auto fresh=request(5,5,1800,0);transactions.apply("owner",fresh,snapshot(),1000);confirm(1040);auto changed=request(6,6,1900,5);actual.translate(10,0);changed["placements"]=QJsonArray{QJsonObject{{"uuid",uuid},{"expected-frame",rect(actual)},{"frame",rect(QRectF(10,10,1900,1400))}}};before=writes;rejects([&]{transactions.apply("owner",changed,snapshot(),1200);});check(writes==before);
 auto userFrame=actual;transactions.teardown();check(actual==userFrame);actual=QRectF(10,10,1800,1400);transactions.teardown();check(actual==QRectF(10,10,1800,1400));
 // Losing the daemon during a replacement drops ownership without further writes.
 GeometryTransactions lost([&](const QString &)->std::optional<QRectF>{return actual;},[&](const QString &,const QRectF &target){actual=target;return true;});
 auto a=request(8,1,1800,0);lost.apply("owner",a,snapshot(),0);lost.tick(40);lost.tick(80);lost.tick(120);auto b=request(9,2,2000,8);lost.apply("owner",b,snapshot(),200);lost.release("owner",210);lost.tick(240);lost.tick(280);lost.tick(320);check(actual.width()==2000);rejects([&]{lost.status("owner",id(9));});rejects([&]{lost.status("owner",id(8));});
 std::cout<<"PASS confirmed replacement, current-frame release, failed-update rollback, ownership, generations and user-movement rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
