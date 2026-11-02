#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("output movement check failed");}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::invalid_argument &){failed=true;}check(failed);}
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
int main(){try{
 const QString uuid="33333333-3333-4333-8333-333333333333",id="44444444-4444-4444-8444-444444444444",instance="11111111-1111-4111-8111-111111111111";
 QJsonObject from{{"id","a"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(1080,270,2560,1408)}};
 auto to=from;to["id"]="b";to["name"]="DP-2";to["work-area"]=rect(0,0,1080,1920);
 QJsonObject w{{"uuid",uuid},{"id","1"},{"candidate",true},{"minimized",false},{"fullscreen",false},{"desktops",QJsonArray{"desktop"}},{"activities",QJsonArray{"work"}},{"output","DP-1"},{"frame",rect(1280,400,1800,600)},{"minimum",QJsonObject{{"w",100},{"h",100}}}};
 QJsonObject s{{"instance",instance},{"outputs",QJsonArray{from,to}},{"windows",QJsonArray{w}}};
 from["geometry"]=from.value("work-area");to["geometry"]=to.value("work-area");s["outputs"]=QJsonArray{from,to};
 check(outputDestination(s["outputs"].toArray(),from,"direction:west")["id"]==to["id"]);
 check(outputDestination(s["outputs"].toArray(),to,"direction:east")["id"]==from["id"]);
 rejects([&]{outputDestination(s["outputs"].toArray(),to,"direction:west");});
 rejects([&]{outputDestination(s["outputs"].toArray(),from,"direction:unknown");});
 auto r=outputMoveRequest(w,from,to,s,id);r["request-sequence"]="1";validateGeometryRequest(r,s);
 auto target=r["placements"].toArray()[0].toObject()["frame"].toObject();check(target["x"]==QJsonValue(0)&&target["w"]==QJsonValue(1080));
 auto bad=r;bad["move-output"]=false;rejects([&]{validateGeometryRequest(bad,s);});bad=r;bad["supersedes"]=id;rejects([&]{validateGeometryRequest(bad,s);});
 bad=r;auto changed=from;changed["current-activity"]="personal";bad["source-output"]=changed;rejects([&]{validateGeometryRequest(bad,s);});
 bad=r;bad["source-output"]=to;rejects([&]{validateGeometryRequest(bad,s);});
 auto small=w;small["minimum"]=QJsonObject{{"w",1200},{"h",100}};rejects([&]{outputMoveRequest(small,from,to,s,id);});
 auto context=s;auto windows=context["windows"].toArray();auto movedContext=w;movedContext["activities"]=QJsonArray{"personal"};windows[0]=movedContext;context["windows"]=windows;rejects([&]{validateGeometryRequest(r,context);});
 QRectF actual(1280,400,1800,600);QString output="DP-1";int writes=0;bool follow=false;
 GeometryTransactions g([&](auto)->std::optional<QRectF>{return actual;},[&](auto,const QRectF &frame){actual=frame;++writes;if(follow)output=frame.x()<1080?"DP-2":"DP-1";return true;},[&](auto){return output;});
 check(g.apply("owner",r,s,0)["state"]==QJsonValue("pending"));g.tick(40);g.tick(80);g.tick(120);check(!g.status("owner",id)["completed"].toBool()); // Frame alone cannot confirm an output transfer.
 output="DP-2";g.tick(160);g.tick(200);g.tick(240);auto done=g.status("owner",id);check(done["state"]==QJsonValue("moved")&&done["success"].toBool()&&done["acknowledged"].toBool());check(done["managed-windows"].toArray().isEmpty());
 auto n=writes;g.apply("owner",r,s,260);check(writes==n);rejects([&]{g.restore("owner",id,300);});rejects([&]{g.status("other",id);});g.release("owner",350);g.teardown();check(writes==n); // Completed movement leaves no lingering restoration ownership.
 // Move back with a fresh ID; a refused/no-op write rolls back to its source.
 auto current=w;current["output"]="DP-2";current["frame"]=target;auto backSnapshot=s;backSnapshot["windows"]=QJsonArray{current};
 auto back=outputMoveRequest(current,to,from,backSnapshot,"55555555-5555-4555-8555-555555555555");back["request-sequence"]="1";
 const auto original=actual;bool ignore=true;
 GeometryTransactions rollback([&](auto)->std::optional<QRectF>{return actual;},[&](auto,const QRectF &frame){if(!ignore)actual=frame;return true;},[&](auto){return output;});
 rollback.apply("owner",back,backSnapshot,0);rollback.tick(1200);ignore=false;rollback.tick(1240);rollback.tick(1280);rollback.tick(1320);check(rollback.status("owner",back["id"].toString())["state"]==QJsonValue("rolled_back"));check(actual==original);
 // Native tiling ownership cannot be bypassed by the move flag.
 actual=QRectF(1280,400,1800,600);output="DP-1";follow=true;
 int ownedWrites=0;
 GeometryTransactions owned([&](auto)->std::optional<QRectF>{return actual;},[&](auto,const QRectF &frame){actual=frame;++ownedWrites;return true;},[&](auto){return output;});
 auto tile=r;tile.remove("move-output");tile.remove("source-output");tile["context"]=QJsonObject{{"output-id","a"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}};tile["work-area"]=from["work-area"];auto ps=tile["placements"].toArray();auto placement=ps[0].toObject();placement["frame"]=w["frame"];ps[0]=placement;tile["placements"]=ps;
 owned.apply("owner",tile,s,0);owned.tick(40);owned.tick(80);owned.tick(120);r["id"]="66666666-6666-4666-8666-666666666666";r["request-sequence"]="2";n=ownedWrites;rejects([&]{owned.apply("owner",r,s,160);});check(ownedWrites==n);
 // Matching geometry cannot bypass confirmation when KWin still reports the
 // source output (a dragged window may already be over the destination).
 auto lagged=w;lagged["frame"]=target;auto laggedSnapshot=s;laggedSnapshot["windows"]=QJsonArray{lagged};auto same=outputMoveRequest(lagged,from,to,laggedSnapshot,"77777777-7777-4777-8777-777777777777");ps=same["placements"].toArray();placement=ps[0].toObject();placement["frame"]=placement["expected-frame"];ps[0]=placement;same["placements"]=ps;same["request-sequence"]="1";actual=QRectF(target["x"].toDouble(),target["y"].toDouble(),target["w"].toDouble(),target["h"].toDouble());output="DP-1";int laggedWrites=0;
 GeometryTransactions assignment([&](auto)->std::optional<QRectF>{return actual;},[&](auto,auto){++laggedWrites;return true;},[&](auto){return output;});check(assignment.apply("owner",same,laggedSnapshot,0)["state"]=="pending"&&laggedWrites==1);assignment.tick(40);assignment.tick(80);assignment.tick(120);check(!assignment.status("owner",same["id"].toString())["completed"].toBool());output="DP-2";assignment.tick(160);assignment.tick(200);assignment.tick(240);check(assignment.status("owner",same["id"].toString())["state"]=="moved");
 std::cout<<"PASS output mapping, context guards, observed output confirmation, ownership release, rollback and idempotency\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
