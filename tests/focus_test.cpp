#include "focus.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("focus assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QString id(int n){return QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'));}
QJsonObject rect(int x){return {{"x",x},{"y",10},{"w",780},{"h",1400}};}
int main(){try{
 const auto a=id(1),b=id(2),instance=id(3);auto context=QJsonObject{{"output-id","screen"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}};
 auto window=[&](QString uuid,bool focused,int x){return QJsonObject{{"uuid",uuid},{"candidate",true},{"focused",focused},{"frame",rect(x)},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}}};};
 QJsonObject snapshot{{"instance",instance},{"outputs",QJsonArray{QJsonObject{{"id","screen"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"}}}},{"windows",QJsonArray{window(a,true,10),window(b,false,800)}}};
 QJsonObject base{{"protocol",1},{"id",id(10)},{"request-sequence","1"},{"bridge-instance",instance},{"context",context},{"source",a},{"target",b},{"source-frame",rect(10)},{"target-frame",rect(800)}};
 validateFocusRequest(base,snapshot);auto bad=base;bad["target"]=a;rejects([&]{validateFocusRequest(bad,snapshot);});bad=base;bad["bridge-instance"]=id(90);rejects([&]{validateFocusRequest(bad,snapshot);});bad=base;bad["target-frame"]=rect(850);rejects([&]{validateFocusRequest(bad,snapshot);});
 for(auto field:{"minimized","fullscreen","focused"}){auto changed=snapshot;auto ws=changed["windows"].toArray();auto w=ws[1].toObject();w[field]=true;ws[1]=w;changed["windows"]=ws;rejects([&]{validateFocusRequest(base,changed);});}
 auto changed=snapshot;auto ws=changed["windows"].toArray();auto target=ws[1].toObject();target["desktops"]=QJsonArray{"elsewhere"};ws[1]=target;changed["windows"]=ws;rejects([&]{validateFocusRequest(base,changed);});
 QString active=a;int writes=0;bool accept=true;FocusRequests engine([&]{return active;},[&](const QString &uuid){check(uuid==b);++writes;return accept;});
 check(engine.submit("owner",base,snapshot,0)["state"]==QJsonValue("pending"));check(!engine.tick(40));active=b;check(engine.tick(80));check(engine.status("owner",id(10))["success"].toBool());engine.submit("owner",base,snapshot,100);check(writes==1);rejects([&]{engine.status("other",id(10));});rejects([&]{engine.submit("other",base,snapshot,100);});
 auto request=[&](int n){auto r=base;r["id"]=id(n+10);r["request-sequence"]=QString::number(n+1);return r;};
 active=a;accept=false;engine.submit("owner",request(1),snapshot,200);check(engine.status("owner",id(11))["state"]==QJsonValue("failed"));accept=true;
 engine.submit("owner",request(2),snapshot,300);auto before=writes;rejects([&]{engine.submit("owner",request(3),snapshot,310);});check(writes==before);engine.tick(900);check(engine.status("owner",id(12))["state"]==QJsonValue("timed_out"));
 engine.submit("owner",request(3),snapshot,1000);active=id(99);engine.tick(1040);check(engine.status("owner",id(13))["state"]==QJsonValue("failed"));
 active=a;engine.submit("owner",request(4),snapshot,1100);engine.release("owner");check(engine.status("owner",id(14))["state"]==QJsonValue("cancelled"));
 // A new owner epoch may restart its sequence; retained ID replay stays protected.
 auto fresh=request(5);fresh["request-sequence"]="1";engine.submit("owner",fresh,snapshot,1200);active=b;engine.tick(1240);
 for(int n=6;n<90;++n){active=a;engine.submit("owner",request(n),snapshot,n*100);active=b;engine.tick(n*100+40);}
 before=writes;rejects([&]{engine.status("owner",id(10));});rejects([&]{engine.submit("owner",base,snapshot,10000);});check(writes==before);
 // A mismatched native active source rejects before calling activation.
 active=b;rejects([&]{engine.submit("owner",request(90),snapshot,10000);});check(writes==before);
 QJsonObject neighbor{{"uuid",b},{"frame",rect(800)},{"source",QJsonObject{{"uuid",a},{"frame",rect(10)}}}};QJsonObject plan{{"context",context},{"bridge-instance",instance}};
 auto built=focusRequest(neighbor,plan,snapshot,id(200));built["request-sequence"]="1";validateFocusRequest(built,snapshot);neighbor["frame"]=rect(900);rejects([&]{focusRequest(neighbor,plan,snapshot,id(201));});
 std::cout<<"PASS focus validation, observed acknowledgement, timeout, interference, ownership, replay expiry and bounded history\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
