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
 QRectF actual(100,100,800,600),original=actual;bool reject=false;int writes=0;
 GeometryTransactions transactions([&](const QString &)->std::optional<QRectF>{return actual;},[&](const QString &,const QRectF &target){++writes;if(reject&&target.width()==2200)return false;actual=target;return true;});
 QJsonObject o{{"id","output"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(QRectF(0,0,2560,1440))}};
 auto snapshot=[&]{return QJsonObject{{"instance",instance},{"outputs",QJsonArray{o}},{"windows",QJsonArray{QJsonObject{{"uuid",uuid},{"candidate",true},{"minimized",false},{"fullscreen",false},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"minimum",QJsonObject{{"w",100},{"h",100}}},{"frame",rect(actual)}}}}};};
 auto request=[&](int n,int generation,double width,int prior){QJsonObject r{{"protocol",1},{"request-sequence",QString::number(n)},{"id",id(n)},{"bridge-instance",instance},{"context-id",context},{"context-generation",QString::number(generation)},{"context",QJsonObject{{"output-id","output"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",o["work-area"]},{"placements",QJsonArray{QJsonObject{{"uuid",uuid},{"expected-frame",rect(actual)},{"frame",rect(QRectF(10,10,width,1400))}}}}};if(prior)r["supersedes"]=id(prior);return r;};
 auto confirm=[&](int now){transactions.tick(now);transactions.tick(now+40);transactions.tick(now+80);};

 QJsonObject first;
 for(int n=1;n<=160;++n){auto r=request(n,n,n%2?1800:2000,n-1);if(n==1)first=r;transactions.apply("owner",r,snapshot(),n*200);confirm(n*200+40);auto status=transactions.status("owner",id(n));check(status["state"]==QJsonValue("applied"));check(status["retained-records"].toInt()<=32);}
 rejects([&]{transactions.status("owner",id(1));});check(transactions.status("owner",id(140))["active-transaction"]==QJsonValue(id(160)));
 auto before=writes;rejects([&]{transactions.apply("owner",first,snapshot(),33000);});check(writes==before);
 // A failed update must retain its live predecessor even after the root record has expired.
 reject=true;transactions.apply("owner",request(161,161,2200,160),snapshot(),33000);confirm(33040);check(transactions.status("owner",id(161))["state"]==QJsonValue("rolled_back"));check(transactions.status("owner",id(160))["state"]==QJsonValue("applied"));check(actual.width()==2000);reject=false;
 transactions.apply("owner",request(162,162,1900,160),snapshot(),33200);confirm(33240);transactions.forget("owner",id(162));confirm(33440);check(actual.width()==1900);
 // Expired replay must still reject after frames match its original expected source again.
 before=writes;rejects([&]{transactions.apply("owner",first,snapshot(),33600);});check(writes==before);
 auto malformed=request(163,163,1800,0);malformed["request-sequence"]="0163";rejects([&]{transactions.apply("owner",malformed,snapshot(),33800);});check(writes==before);
 // Repeated independent apply/restore cycles also reuse bounded history.
 for(int n=163;n<=240;++n){transactions.apply("owner",request(n,n,1800,0),snapshot(),n*400);confirm(n*400+40);transactions.forget("owner",id(n));confirm(n*400+200);check(actual.width()==1800);check(transactions.status("owner",id(n))["retained-records"].toInt()<=32);}
 // Owner loss after compaction still restores the root baseline across a replacement.
 transactions.apply("owner",request(241,241,1800,0),snapshot(),100000);confirm(100040);transactions.apply("owner",request(242,242,2000,241),snapshot(),100200);transactions.release("owner",100210);confirm(100240);check(actual.width()==2000);
 // Capacity filled entirely by active backups rejects without writes; freeing one permits progress.
 QMap<QString,QRectF> many;int count=0;
 GeometryTransactions capacity([&](const QString &key)->std::optional<QRectF>{if(!many.contains(key))return {};return many[key];},[&](const QString &key,const QRectF &target){++count;many[key]=target;return true;});
 auto make=[&](int n){auto key=QString("eeeeeeee-eeee-4eee-8eee-%1").arg(n,12,16,QChar('0'));many[key]=original;auto snap=snapshot();auto ws=snap["windows"].toArray();auto w=ws[0].toObject();w["uuid"]=key;w["frame"]=rect(original);ws[0]=w;snap["windows"]=ws;auto req=request(300+n,300+n,1800,0);auto ps=req["placements"].toArray();auto p=ps[0].toObject();p["uuid"]=key;p["expected-frame"]=rect(original);ps[0]=p;req["placements"]=ps;return qMakePair(req,snap);};
 for(int n=1;n<=32;++n){auto pair=make(n);capacity.apply("owner",pair.first,pair.second,n*200);capacity.tick(n*200+40);capacity.tick(n*200+80);capacity.tick(n*200+120);}
 auto overflow=make(33);before=count;rejects([&]{capacity.apply("owner",overflow.first,overflow.second,7000);});check(count==before);capacity.forget("owner",id(301));capacity.tick(7140);capacity.tick(7180);capacity.tick(7220);capacity.apply("owner",overflow.first,overflow.second,7300);capacity.tick(7340);capacity.tick(7380);capacity.tick(7420);check(capacity.status("owner",id(333))["retained-records"].toInt()==32);check(capacity.status("owner",id(302))["state"]==QJsonValue("applied"));
 std::cout<<"PASS hundreds of bounded updates/cycles, expired replay rejection, protected baselines, rollback and owner loss\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
