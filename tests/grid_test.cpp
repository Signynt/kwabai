#include "../src/compat/grid.h"
#include "geometry.h"
#include "rules.h"
#include <QJsonArray>
#include <QUuid>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool value){if(!value)throw std::runtime_error("grid assertion failed");}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
int main(){try{
 using namespace kwabai::compat;
 auto frame=gridFrame(parseGrid("11:11:1:1:9:9"),rect(0,0,1100,1100),0,0,0,0,0);check(frame==rect(100,100,900,900));
 auto clamped=gridFrame(parseGrid("2:2:99:99:0:0"),rect(-200,20,200,200),0,0,0,0,0);check(clamped==rect(-100,120,100,100));
 check(gridFrame(parseGrid("1:1:0:0:1:1"),rect(0,0,1000,800),10,20,30,40,50)==rect(40,10,940,760));
 auto spaced=gridFrame(parseGrid("11:11:1:1:9:9"),rect(-1080,270,1100,1100),0,10,10,10,10);
 check(std::abs(spaced["x"].toDouble()-(-1060+1060.0/11))<1e-6&&std::abs(spaced["w"].toDouble()-1060.0*9/11)<1e-6);
 for(auto text:{"0:1:0:0:1:1","1:0:0:0:1:1","1:1:-1:0:1:1","1:1:0:0:1","1:1:0:0:1:1:1","1:1:0:0:nan:1","4294967296:1:0:0:1:1","+1:1:0:0:1:1"})rejects([&]{parseGrid(text);});
 rejects([&]{gridFrame(parseGrid("2:2:1:1:1:1"),rect(0,0,20,20),0,0,0,0,100);});
 RuleSet rules;rules.edit({"add-posix","whatsapp","inherit","elecwhat",".*","","whatsapp","11:11:1:1:9:9"});QJsonObject app{{"app","elecwhat"},{"title","Messages"}};
 auto rule=rules.evaluate(app);check(rule["grid"]=="11:11:1:1:9:9"&&rule["scratchpad"]=="whatsapp");check(RuleSet::restore(rules.state()).state()==rules.state());check(rules.listPosix()[0].toObject()["grid"]==rule["grid"]);
 auto saved=rules.state();rejects([&]{rules.edit({"add-posix","whatsapp","inherit","elecwhat",".*","","whatsapp","0:11:1:1:9:9"});});check(rules.state()==saved);
 rules.edit({"add-posix","above","inherit","elecwhat","","above"});check(rules.evaluate(app)["grid"]==rule["grid"]);
 const QString uuid="aaaaaaaa-aaaa-4aaa-8aaa-000000000001",instance="11111111-1111-4111-8111-111111111111";
 auto original=rect(10,10,500,500);QJsonObject window{{"uuid",uuid},{"id","1"},{"candidate",true},{"minimized",false},{"fullscreen",false},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"output","DP-1"},{"frame",original},{"minimum",QJsonObject{{"w",50},{"h",50}}}};
 QJsonObject output{{"id","a"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(0,0,1100,1100)}};
 QJsonObject snapshot{{"instance",instance},{"windows",QJsonArray{window}},{"outputs",QJsonArray{output}}};
 const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
 QJsonObject request{{"protocol",1},{"id",id},{"request-sequence","1"},{"bridge-instance",instance},{"floating-frame",true},{"context",QJsonObject{{"output-id","a"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",output["work-area"]},{"placements",QJsonArray{QJsonObject{{"uuid",uuid},{"frame",frame},{"expected-frame",original}}}}};
 validateGeometryRequest(request,snapshot);auto bad=request;bad["floating-frame"]=false;rejects([&]{validateGeometryRequest(bad,snapshot);});bad=request;bad["context-id"]=id;rejects([&]{validateGeometryRequest(bad,snapshot);});
 QRectF actual(10,10,500,500);int writes=0;GeometryTransactions engine([&](const QString &)->std::optional<QRectF>{return actual;},[&](const QString &,const QRectF &target){actual=target;++writes;return true;});
 engine.apply(":test",request,snapshot,0);check(engine.owns(uuid)&&!engine.tiles(uuid));engine.tick(16);engine.tick(32);engine.tick(48);auto result=engine.status(":test",id);check(result["state"]=="moved"&&result["success"]==true&&result["managed-windows"].toArray().isEmpty()&&!engine.owns(uuid));
 auto count=writes;engine.apply(":test",request,snapshot,64);engine.release(":test",80);engine.teardown();check(writes==count&&actual==QRectF(100,100,900,900));
 // A grid cannot take a currently owned tiled member.
 actual=QRectF(10,10,500,500);GeometryTransactions tiled([&](const QString &)->std::optional<QRectF>{return actual;},[&](const QString &,const QRectF &target){actual=target;++writes;return true;});
 auto tile=request;tile.remove("floating-frame");tile["placements"]=QJsonArray{QJsonObject{{"uuid",uuid},{"frame",original},{"expected-frame",original}}};tiled.apply(":test",tile,snapshot,0);check(tiled.tiles(uuid));auto other=request;other["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);other["request-sequence"]="2";count=writes;rejects([&]{tiled.apply(":test",other,snapshot,20);});check(writes==count);
 std::cout<<"PASS Yabai grid math, padding/gaps, clamping, rules, native confirmation and ephemeral ownership\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
