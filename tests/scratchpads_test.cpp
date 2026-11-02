#include "scratchpads.h"
#include <QJsonArray>
#include <QUuid>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool value){if(!value)throw std::runtime_error("scratchpad assertion failed");}
int main(){try{
 const QString a="aaaaaaaa-aaaa-4aaa-8aaa-000000000001",b="aaaaaaaa-aaaa-4aaa-8aaa-000000000002",instance="11111111-1111-4111-8111-111111111111";
 QMap<QString,QJsonObject> windows;for(auto id:{a,b})windows[id]={{"hidden",false},{"candidate",true},{"current",true}};
 bool busy=false,refuse=false;int writes=0;bool lastSummon=true;
 Scratchpads pads([&](const QString &id){return windows.value(id);},[&](const QString &id,bool hide,bool summon){++writes;lastSummon=summon;if(refuse)return false;windows[id]["hidden"]=hide;windows[id]["current"]=true;return true;},[&](const QString &){return busy;});
 quint64 sequence=0;QJsonObject snapshot{{"instance",instance}};
 auto request=[&](QString op,QString id,QString label){return QJsonObject{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"protocol",1},{"bridge-instance",instance},{"request-sequence",QString::number(++sequence)},{"operation",op},{"window",id},{"label",label}};};
 auto send=[&](QString op,QString id=QString{},QString label=QString{}){return pads.submit(":test",request(op,id,label),snapshot);};
 auto rejected=[&](auto fn){bool thrown=false;try{fn();}catch(const std::invalid_argument &){thrown=true;}check(thrown);};
 // The real native snapshot decorator must write metadata for untouched windows.
 QJsonArray raw{QJsonObject{{"uuid",a},{"title","untouched"}},QJsonObject{{"uuid",b}}};
 auto decorated=pads.decorate(raw,[](const QString &){return QJsonObject{};});
 for(auto value:decorated){auto w=value.toObject();check(w["scratchpad"].isString()&&w["scratchpad"]==""&&w["scratchpad-hidden"]==false&&!w.contains("interaction"));}
 check(decorated[0].toObject()["title"]=="untouched");
 auto gesture=QJsonObject{{"state","active"},{"kind","move"}};
 decorated=pads.decorate(raw,[&](const QString &uuid){return uuid==a?gesture:QJsonObject{};});
 check(decorated[0].toObject()["interaction"]==gesture&&!decorated[1].toObject().contains("interaction"));
 send("assign",a,"term");check(writes==0&&pads.describe(a)["scratchpad"]=="term");
 rejected([&]{send("assign",b,"term");});rejected([&]{send("assign",b,"float");});check(writes==0);
 auto hide=request("toggle",{},"term");check(pads.submit(":test",hide,snapshot)["success"].toBool());check(windows[a]["hidden"].toBool()&&writes==1);
 decorated=pads.decorate(raw);check(decorated[0].toObject()["scratchpad"]=="term"&&decorated[0].toObject()["scratchpad-hidden"]==true);
 check(pads.submit(":test",hide,snapshot)["success"].toBool()&&writes==1);rejected([&]{pads.submit(":other",hide,snapshot);});
 pads.release(":test");check(pads.describe(a)["scratchpad"]=="term");
 send("toggle",{},"term");check(!windows[a]["hidden"].toBool());
 windows[a]["current"]=false;send("toggle",{},"term");check(!windows[a]["hidden"].toBool());
 busy=true;auto before=writes;rejected([&]{send("toggle",{},"term");});check(writes==before);busy=false;
 send("assign",a,"notes");check(pads.describe(a)["scratchpad"]=="notes");send("assign",b,"term");
 send("toggle",{},"notes");send("toggle",{},"term");send("recover");check(!windows[a]["hidden"].toBool()&&!windows[b]["hidden"].toBool());
 refuse=true;check(!send("toggle",{},"term")["success"].toBool());check(pads.describe(b)["scratchpad"]=="term");refuse=false;
 send("clear",b);check(pads.describe(b)["scratchpad"]=="");send("toggle",{},"notes");pads.teardown();check(!windows[a]["hidden"].toBool()&&!lastSummon);
 send("assign",a,"notes");windows.remove(a);pads.prune();send("assign",b,"notes");
 auto uppercase=request("assign",b.toUpper(),"other");rejected([&]{pads.submit(":test",uppercase,snapshot);});
 auto stale=request("toggle",{},"notes");stale["bridge-instance"]="wrong";rejected([&]{pads.submit(":test",stale,snapshot);});
 std::cout<<"PASS scratchpad labels, show/hide, cross-context summon, replay, release guards, recovery and cleanup\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
