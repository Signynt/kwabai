#include "desktops.h"
#include "state.h"
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool value){if(!value)throw std::runtime_error("Activity membership assertion failed");}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QString id(int n){return QString("dddddddd-dddd-4ddd-8ddd-%1").arg(n,12,16,QChar('0'));}
int main(){try{
 const auto source=id(1),target=id(2),desktop=id(3),window=id(4),instance=id(5);
 const QJsonObject frame{{"x",100},{"y",100},{"w",800},{"h",600}},area{{"x",0},{"y",0},{"w",2560},{"h",1440}};
 QJsonObject output{{"id","screen"},{"name","DP-1"},{"scale",1},{"geometry",area},{"work-area",area},{"current-desktop",desktop},{"current-activity",source}};
 QJsonObject actual{{"uuid",window},{"app","kitty"},{"title","test"},{"output","DP-1"},{"desktops",QJsonArray{desktop}},{"activities",QJsonArray{source}},{"candidate",true},{"minimized",false},{"fullscreen",false},{"focused",true},{"frame",frame},{"minimum",QJsonObject{{"w",10},{"h",10}}}};
 QJsonArray catalog{QJsonObject{{"id",source}},QJsonObject{{"id",target}}};
 auto snapshot=[&]{return QJsonObject{{"protocol",1},{"instance",instance},{"sequence","1"},{"event-sequence","0"},{"kwin-version","6.8.1"},{"geometry-window-limit",20},{"capabilities",QJsonArray{"snapshot","contexts","events","geometry","geometry-update","geometry-retention","geometry-lifecycle","geometry-release","activity-move"}},{"outputs",QJsonArray{output}},{"activities",catalog},{"windows",QJsonArray{actual}}};};
 auto request=[&](int n){return QJsonObject{{"protocol",1},{"id",id(n)},{"request-sequence",QString::number(n)},{"bridge-instance",instance},{"window",window},{"activity",target},{"source-output",output},{"expected-desktops",actual["desktops"]},{"expected-activities",actual["activities"]},{"expected-frame",actual["frame"]}};};
 check(activityDestination(catalog,target)["id"]==target);rejects([&]{activityDestination(catalog,"2");});rejects([&]{activityDestination(catalog,id(99));});
 BridgeState state;state.accept(QJsonDocument(snapshot()).toJson());check(state.hasActivityMove()&&state.activities()["activities"].toArray()==catalog);
 auto malformed=snapshot();malformed["activities"]=QJsonArray{catalog[0],catalog[0]};rejects([&]{BridgeState invalid;invalid.accept(QJsonDocument(malformed).toJson());});
 int writes=0;bool available=true,ignore=false;MembershipMoves moves([&](const QString &uuid){return uuid==window?actual:QJsonObject{};},[&](const QString &,const QJsonArray &ids){++writes;if(!ignore)actual["activities"]=ids;return true;},[&](const QString &){return available;},true);
 auto first=request(10);available=false;rejects([&]{moves.submit("owner",first,snapshot(),0);});check(writes==0);available=true;
 auto stale=first;stale["expected-desktops"]=QJsonArray{};rejects([&]{moves.submit("owner",stale,snapshot(),0);});check(writes==0);
 moves.submit("owner",first,snapshot(),0);check(writes==1&&moves.pending()&&actual["frame"]==frame&&actual["desktops"]==QJsonValue(QJsonArray{desktop}));moves.tick(16);moves.tick(32);check(moves.pending());moves.tick(48);check(moves.status("owner",id(10))["success"].toBool());check(moves.status("owner",id(10))["activity"]==target);rejects([&]{moves.status("stranger",id(10));});moves.submit("owner",first,snapshot(),60);check(writes==1);
 moves.release("owner",70);moves.teardown();check(writes==1&&actual["activities"]==QJsonValue(QJsonArray{target}));
 // A failed pending request only rolls back its own Activity write; other
 // membership changed by the user is never overwritten.
 actual["activities"]=QJsonArray{};ignore=true;moves.submit("owner",request(11),snapshot(),100);moves.tick(1300);moves.tick(1316);moves.tick(1332);moves.tick(1348);check(moves.status("owner",id(11))["state"]=="rolled_back");check(actual["activities"]==QJsonValue(QJsonArray{}));
 moves.submit("owner",request(12),snapshot(),1400);actual["desktops"]=QJsonArray{id(88)};auto before=writes;moves.tick(1416);check(moves.status("owner",id(12))["state"]=="failed"&&writes==before);
 actual["desktops"]=QJsonArray{desktop};ignore=false;moves.submit("owner",request(13),snapshot(),1500);before=writes;moves.release("owner",1501);moves.tick(3000);moves.teardown();check(writes==before&&actual["activities"]==QJsonValue(QJsonArray{target}));
 std::cout<<"PASS Activity catalog, source/ownership checks, stable confirmation, preserved desktop/geometry, bounded failure and no-write disconnect\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
