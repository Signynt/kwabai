#include "desktops.h"
#include "state.h"
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("desktop move assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
int main(){try{
 const QString a="11111111-1111-4111-8111-111111111111",b="22222222-2222-4222-8222-222222222222",c="33333333-3333-4333-8333-333333333333",uuid="44444444-4444-4444-8444-444444444444",instance="55555555-5555-4555-8555-555555555555",id="66666666-6666-4666-8666-666666666666";
 const QJsonObject frame{{"x",100},{"y",100},{"w",800},{"h",600}},area{{"x",0},{"y",0},{"w",2560},{"h",1440}};
 QJsonArray catalog{QJsonObject{{"id",b},{"index",2},{"name","Second"}},QJsonObject{{"id",a},{"index",1},{"name","First"}},QJsonObject{{"id",c},{"index",3},{"name","Third"}}};
 QJsonObject output{{"id","screen"},{"name","DP-1"},{"scale",1},{"geometry",area},{"work-area",area},{"current-desktop",a},{"current-activity","work"}};
 QJsonObject w{{"uuid",uuid},{"app","kitty"},{"title","test"},{"output","DP-1"},{"desktops",QJsonArray{a}},{"activities",QJsonArray{"work"}},{"candidate",true},{"minimized",false},{"fullscreen",false},{"focused",true},{"frame",frame},{"minimum",QJsonObject{{"w",100},{"h",100}}}};
 QJsonObject s{{"protocol",1},{"instance",instance},{"sequence","1"},{"event-sequence","0"},{"kwin-version","6.8.1"},{"geometry-window-limit",20},{"capabilities",QJsonArray{"snapshot","contexts","events","geometry","geometry-update","geometry-retention","geometry-lifecycle","geometry-release","desktop-move"}},{"outputs",QJsonArray{output}},{"desktops",catalog},{"windows",QJsonArray{w}}};
 QJsonObject r{{"protocol",1},{"id",id},{"request-sequence","1"},{"bridge-instance",instance},{"window",uuid},{"desktop",b},{"source-output",output},{"expected-desktops",w["desktops"]},{"expected-activities",w["activities"]},{"expected-frame",frame}};
 check(desktopDestination(catalog,"2")["id"]==QJsonValue(b));check(desktopDestination(catalog,a)["index"]==QJsonValue(1));rejects([&]{desktopDestination(catalog,"02");});rejects([&]{desktopDestination(catalog,"4");});validateDesktopMove(r,s);
 auto bad=r;bad["bridge-instance"]=a;rejects([&]{validateDesktopMove(bad,s);});bad=r;bad["desktop"]=instance;rejects([&]{validateDesktopMove(bad,s);});bad=r;bad["expected-desktops"]=QJsonArray{};rejects([&]{validateDesktopMove(bad,s);});bad=r;bad["expected-frame"]=area;rejects([&]{validateDesktopMove(bad,s);});
 BridgeState state;state.accept(QJsonDocument(s).toJson());check(state.hasDesktopMove()&&state.desktops()["desktops"].toArray().size()==3);auto corrupt=s;auto duplicates=catalog;duplicates[0]=duplicates[1];corrupt["desktops"]=duplicates;rejects([&]{BridgeState other;other.accept(QJsonDocument(corrupt).toJson());});corrupt=s;corrupt.remove("desktops");rejects([&]{BridgeState other;other.accept(QJsonDocument(corrupt).toJson());});
 QJsonObject actual=w;int writes=0;bool available=false,ignore=false;
 DesktopMoves moves([&](auto){return actual;},[&](auto,const QJsonArray &ds){++writes;if(!ignore)actual["desktops"]=ds;return true;},[&](auto){return available;});
 rejects([&]{moves.submit("owner",r,s,0);});check(writes==0);available=true;auto receipt=moves.submit("owner",r,s,0);check(receipt["state"]==QJsonValue("pending"));check(actual["desktops"]==QJsonValue(QJsonArray{b}));check(moves.pending());
 auto second=r;second["id"]=c;second["request-sequence"]="2";rejects([&]{moves.submit("owner",second,s,20);});moves.tick(40);moves.tick(80);check(!moves.status("owner",id)["completed"].toBool());moves.tick(120);check(moves.status("owner",id)["success"].toBool());check(!moves.pending());auto before=writes;moves.submit("owner",r,s,140);check(writes==before);rejects([&]{moves.status("stranger",id);});moves.release("owner",160);moves.teardown();check(writes==before);
 // Ignored writes cannot yield false acknowledgement; unchanged original state confirms rollback.
 actual=w;ignore=true;second["request-sequence"]="1";moves.submit("owner",second,s,200);moves.tick(1400);check(moves.status("owner",c)["state"]==QJsonValue("rolling_back"));moves.tick(1440);moves.tick(1480);moves.tick(1520);check(moves.status("owner",c)["state"]==QJsonValue("rolled_back"));check(!moves.status("owner",c)["success"].toBool());
 // A user's intervening third membership is preserved rather than rolled back.
 actual=w;ignore=false;second["id"]=b;second["request-sequence"]="2";moves.submit("owner",second,s,1600);actual["desktops"]=QJsonArray{c};before=writes;moves.tick(1640);check(moves.status("owner",b)["state"]==QJsonValue("failed"));check(writes==before&&actual["desktops"]==QJsonValue(QJsonArray{c}));
 // Owner loss preserves current membership, including a pending move from sticky membership.
 actual=w;actual["desktops"]=QJsonArray{};auto sticky=s;sticky["windows"]=QJsonArray{actual};auto sr=r;sr["id"]=instance;sr["request-sequence"]="3";sr["expected-desktops"]=QJsonArray{};moves.submit("owner",sr,sticky,1700);moves.release("owner",1720);moves.tick(1760);moves.tick(1800);moves.tick(1840);check(actual["desktops"]==QJsonValue(QJsonArray{b}));rejects([&]{moves.status("owner",instance);});
 std::cout<<"PASS desktop catalog, stable identities, ownership/staleness guards, confirmation, rollback and intervening user changes\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
