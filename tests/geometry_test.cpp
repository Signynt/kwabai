#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("geometry transaction assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
const QString native="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",instance="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",id="cccccccc-cccc-4ccc-8ccc-cccccccccccc";
QJsonObject snapshot(){return {{"instance",instance},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"current-desktop","desktop"},{"current-activity","work"},{"work-area",rect(0,40,2560,1400)}}}},{"windows",QJsonArray{QJsonObject{{"uuid",native},{"frame",rect(100,100,800,600)},{"minimum",QJsonObject{{"w",200},{"h",100}}},{"candidate",true},{"minimized",false},{"fullscreen",false},{"output","DP-1"},{"desktops",QJsonArray{"desktop"}},{"activities",QJsonArray{"work"}}}}}};}
QJsonObject request(){return {{"protocol",1},{"request-sequence","1"},{"id",id},{"bridge-instance",instance},{"context",QJsonObject{{"output-id","output"},{"output","DP-1"},{"desktop","desktop"},{"activity","work"}}},{"work-area",rect(0,40,2560,1400)},{"placements",QJsonArray{QJsonObject{{"uuid",native},{"expected-frame",rect(100,100,800,600)},{"frame",rect(10,50,2540,1380)}}}}};}
int main(){try{
 auto s=snapshot(),r=request();validateGeometryRequest(r,s);
 // Zoom permits only explicitly marked root-sized windows to overlap peers.
 auto z=r;z["context-id"]="zoom-context";z["context-generation"]="1";z["zoom-frame"]=rect(10,50,2540,1380);
 auto zp=z["placements"].toArray()[0].toObject();zp["zoom-fullscreen"]=true;z["placements"]=QJsonArray{zp};validateGeometryRequest(z,s);
 auto invalidZoom=z;invalidZoom.remove("zoom-frame");rejects([&]{validateGeometryRequest(invalidZoom,s);});
 invalidZoom=z;invalidZoom["zoom-frame"]=rect(10,50,2500,1380);rejects([&]{validateGeometryRequest(invalidZoom,s);});
 invalidZoom=z;zp["zoom-fullscreen"]=false;invalidZoom["placements"]=QJsonArray{zp};rejects([&]{validateGeometryRequest(invalidZoom,s);});
 auto zs=s;auto zw=zs["windows"].toArray()[0].toObject();zw["uuid"]="88888888-8888-4888-8888-888888888888";zs["windows"]=QJsonArray{zs["windows"].toArray()[0],zw};
 auto peer=r["placements"].toArray()[0].toObject();peer["uuid"]=zw["uuid"];peer["frame"]=rect(20,60,1000,1000);zp=z["placements"].toArray()[0].toObject();z["placements"]=QJsonArray{zp,peer};validateGeometryRequest(z,zs);
 invalidZoom=z;zp["zoom-fullscreen"]=false;invalidZoom.remove("zoom-frame");invalidZoom["placements"]=QJsonArray{zp,peer};rejects([&]{validateGeometryRequest(invalidZoom,zs);});

 auto bad=r;bad["bridge-instance"]="old";rejects([&]{validateGeometryRequest(bad,s);});
 bad=r;auto placements=bad["placements"].toArray();auto p=placements[0].toObject();p["expected-frame"]=rect(150,100,800,600);placements[0]=p;bad["placements"]=placements;rejects([&]{validateGeometryRequest(bad,s);});
 bad=r;placements=bad["placements"].toArray();p=placements[0].toObject();p["frame"]=rect(-10,50,2540,1380);placements[0]=p;bad["placements"]=placements;rejects([&]{validateGeometryRequest(bad,s);});
 QMap<QString,QRectF> actual{{native,QRectF(100,100,800,600)}};QList<QPair<QString,QRectF>> writes;
 GeometryTransactions transactions([&](auto key)->std::optional<QRectF>{if(!actual.contains(key))return {};return actual[key];},[&](auto key,auto frame){writes.append({key,frame});return true;});
 auto applyWrites=[&]{for(auto w:writes)if(actual.contains(w.first))actual[w.first]=w.second;writes.clear();};
 check(transactions.apply("owner",r,s,0)["state"]==QJsonValue("pending"));check(writes.size()==1);transactions.tick(40);check(!transactions.status("owner",id)["completed"].toBool());
 applyWrites();transactions.tick(50);transactions.tick(60);transactions.tick(80);check(transactions.status("owner",id)["state"]==QJsonValue("applied"));check(transactions.status("owner",id)["success"].toBool());
 transactions.apply("owner",r,s,100);check(writes.isEmpty());rejects([&]{transactions.status("stranger",id);});
 bad=r;bad["request-sequence"]="2";bad["id"]="dddddddd-dddd-4ddd-8ddd-dddddddddddd";rejects([&]{transactions.apply("owner",bad,s,100);});check(writes.isEmpty());
 transactions.forget("owner",id);applyWrites();transactions.tick(140);transactions.tick(150);transactions.tick(160);check(actual[native]==QRectF(10,50,2540,1380));check(transactions.status("owner",id)["state"]==QJsonValue("forgotten"));actual[native]=QRectF(100,100,800,600);
 transactions.apply("owner",bad,s,200);writes.clear();transactions.tick(1400);check(transactions.status("owner",bad["id"].toString())["state"]==QJsonValue("rolling_back"));applyWrites();transactions.tick(1410);transactions.tick(1420);transactions.tick(1440);check(transactions.status("owner",bad["id"].toString())["state"]==QJsonValue("rolled_back"));check(!transactions.status("owner",bad["id"].toString())["success"].toBool());
 bad["request-sequence"]="3";bad["id"]="eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee";transactions.apply("owner",bad,s,1500);applyWrites();transactions.tick(1510);transactions.tick(1520);transactions.tick(1540);
 actual[native]=QRectF(300,300,900,700);transactions.release("owner",1600);applyWrites();transactions.tick(1610);transactions.tick(1620);transactions.tick(1640);check(actual[native]==QRectF(300,300,900,700));
 // Closing a pending client causes a bounded failed rollback, never false acknowledgement.
 bad["request-sequence"]="4";bad["id"]="ffffffff-ffff-4fff-8fff-ffffffffffff";actual[native]=QRectF(100,100,800,600);transactions.apply("owner",bad,s,1700);writes.clear();actual.remove(native);transactions.tick(2900);transactions.tick(4100);check(transactions.status("owner",bad["id"].toString())["state"]==QJsonValue("restore_failed"));
 // A second write rejected after the first succeeds must roll the first back.
 const QString second="99999999-9999-4999-8999-999999999999";
 auto two=snapshot();auto ws=two["windows"].toArray();auto w2=ws[0].toObject();w2["uuid"]=second;w2["frame"]=rect(1000,100,800,600);ws.append(w2);two["windows"]=ws;
 auto twoRequest=request();auto ps=twoRequest["placements"].toArray();auto firstPlacement=ps[0].toObject();firstPlacement["frame"]=rect(10,50,1265,1380);ps[0]=firstPlacement;auto secondPlacement=firstPlacement;secondPlacement["uuid"]=second;secondPlacement["expected-frame"]=rect(1000,100,800,600);secondPlacement["frame"]=rect(1285,50,1265,1380);ps.append(secondPlacement);twoRequest["placements"]=ps;
 QMap<QString,QRectF> pair{{native,QRectF(100,100,800,600)},{second,QRectF(1000,100,800,600)}};
 GeometryTransactions partial([&](auto key)->std::optional<QRectF>{return pair[key];},[&](auto key,auto target){if(key==second&&target.x()==1285)return false;pair[key]=target;return true;});
 check(partial.apply("owner",twoRequest,two,0)["state"]==QJsonValue("rolling_back"));partial.tick(40);partial.tick(80);partial.tick(120);check(partial.status("owner",id)["state"]==QJsonValue("rolled_back"));check(pair[native]==QRectF(100,100,800,600));check(pair[second]==QRectF(1000,100,800,600));
 // Unchanged peers do not receive another configure. Real changes still wait
 // for three observed matches, including an output-assignment confirmation.
 pair={{native,QRectF(100,100,800,600)},{second,QRectF(1000,100,800,600)}};QList<QPair<QString,QRectF>> changedWrites;
 GeometryTransactions selective([&](auto key)->std::optional<QRectF>{return pair[key];},[&](auto key,auto target){changedWrites.append({key,target});return true;});
 ps=twoRequest["placements"].toArray();p=ps[0].toObject();p["frame"]=p["expected-frame"];ps[0]=p;twoRequest["placements"]=ps;twoRequest["context-id"]="12345678-1234-4234-8234-123456789abc";twoRequest["context-generation"]="1";
 check(selective.apply("owner",twoRequest,two,0)["state"]=="pending");check(changedWrites.size()==1&&changedWrites[0].first==second);selective.tick(40);check(!selective.status("owner",id)["completed"].toBool());
 pair[second]=changedWrites[0].second;selective.tick(80);selective.tick(120);check(!selective.status("owner",id)["completed"].toBool());selective.tick(160);check(selective.status("owner",id)["success"].toBool());
 auto continuation=twoRequest,currentSnapshot=two;auto currentWindows=two["windows"].toArray();ps=continuation["placements"].toArray();for(int i=0;i<ps.size();++i){p=ps[i].toObject();p["expected-frame"]=p["frame"];ps[i]=p;auto w=currentWindows[i].toObject();w["frame"]=p["frame"];currentWindows[i]=w;}currentSnapshot["windows"]=currentWindows;continuation["placements"]=ps;continuation["id"]="23456789-1234-4234-8234-123456789abc";continuation["request-sequence"]="2";continuation["context-generation"]="2";continuation["supersedes"]=id;
 check(selective.apply("owner",continuation,currentSnapshot,170)["state"]=="applied"&&changedWrites.size()==1);check(selective.status("owner",id)["state"]=="superseded");selective.forget("owner",continuation["id"].toString());for(auto write:changedWrites)pair[write.first]=write.second;selective.tick(200);selective.tick(240);selective.tick(280);check(pair[native]==QRectF(100,100,800,600)&&pair[second]==QRectF(1285,50,1265,1380));
 // An all-unchanged request settles ownership synchronously with zero writes.
 auto same=request();ps=same["placements"].toArray();p=ps[0].toObject();p["frame"]=p["expected-frame"];ps[0]=p;same["placements"]=ps;int noOpWrites=0;
 GeometryTransactions noOp([](auto)->std::optional<QRectF>{return QRectF(100,100,800,600);},[&](auto,auto){++noOpWrites;return true;});
 auto settled=noOp.apply("owner",same,snapshot(),0);check(settled["state"]=="applied"&&settled["acknowledged"].toBool()&&!noOp.pending()&&noOp.owns(native)&&noOpWrites==0);check(noOp.apply("owner",same,snapshot(),1)==settled);
 rejects([&]{noOp.apply("stranger",same,snapshot(),2);});auto stale=same;stale["id"]="12345678-1234-4234-8234-123456789abc";stale["request-sequence"]="2";ps=stale["placements"].toArray();p=ps[0].toObject();p["expected-frame"]=rect(110,100,800,600);ps[0]=p;stale["placements"]=ps;rejects([&]{noOp.apply("owner",stale,snapshot(),3);});check(noOpWrites==0);
 // Faster sampling still requires three consecutive matches. A delayed or
 // intermediate client size resets the streak rather than acknowledging early.
 QRectF observed(100,100,800,600);GeometryTransactions changing([&](auto)->std::optional<QRectF>{return observed;},[](auto,auto){return true;});changing.apply("owner",request(),snapshot(),0);changing.tick(16);check(changing.pending());observed=QRectF(10,50,2540,1380);changing.tick(32);changing.tick(48);check(changing.pending());observed.setWidth(2500);changing.tick(64);observed.setWidth(2540);changing.tick(80);changing.tick(96);check(changing.pending());changing.tick(112);check(changing.status("owner",id)["acknowledged"].toBool());
 // New lifecycle clients may accept a resize but center their window. A
 // single move after three stable size samples repairs that startup race.
 auto startup=request();startup["lifecycle"]=true;startup["context-id"]="12345678-1234-4234-8234-123456789abc";startup["context-generation"]="1";
 QRectF centered(100,100,800,600);int startupWrites=0;
 GeometryTransactions centering([&](auto)->std::optional<QRectF>{return centered;},[&](auto,auto target){++startupWrites;if(startupWrites==1)centered=QRectF(QPointF(318,357),target.size());else centered=target;return true;});
 centering.apply("owner",startup,snapshot(),0);centering.tick(16);centering.tick(32);centering.tick(48);check(startupWrites==1);centering.tick(80);check(startupWrites==2&&centering.pending());centering.tick(96);centering.tick(112);check(centering.pending());centering.tick(128);check(centering.status("owner",id)["success"].toBool()&&centering.status("owner",id)["position-corrections"]==1);
 // Reopened windows can already have the target size. A startup move must
 // receive the same bounded correction when the first configure centers it.
 auto moveStartup=startup,moveSnapshot=snapshot();auto movePlacement=moveStartup["placements"].toArray()[0].toObject();movePlacement["frame"]=rect(10,50,800,600);moveStartup["placements"]=QJsonArray{movePlacement};
 QRectF reopened(100,100,800,600);int reopenedWrites=0;
 GeometryTransactions reopen([&](auto)->std::optional<QRectF>{return reopened;},[&](auto,auto target){++reopenedWrites;reopened=reopenedWrites==1?QRectF(QPointF(318,357),target.size()):target;return true;});
 reopen.apply("owner",moveStartup,moveSnapshot,0);reopen.tick(16);reopen.tick(32);reopen.tick(48);reopen.tick(80);check(reopenedWrites==2);reopen.tick(96);reopen.tick(112);reopen.tick(128);check(reopen.status("owner",id)["success"].toBool());
 // Lifecycle replacement must not opt an existing member into startup repair.
 QRectF existingFrame(100,100,800,600);int existingWrites=0;bool misplace=false;
 GeometryTransactions existing([&](auto)->std::optional<QRectF>{return existingFrame;},[&](auto,auto target){++existingWrites;existingFrame=misplace?QRectF(QPointF(318,357),target.size()):target;return true;});
 existing.apply("owner",startup,snapshot(),0);existing.tick(16);existing.tick(32);existing.tick(48);check(existing.status("owner",id)["success"].toBool());
 auto update=startup,updatedSnapshot=snapshot();auto up=update["placements"].toArray()[0].toObject();up["expected-frame"]=up["frame"];auto observedWindow=updatedSnapshot["windows"].toArray()[0].toObject();observedWindow["frame"]=up["frame"];updatedSnapshot["windows"]=QJsonArray{observedWindow};up["frame"]=rect(10,50,1265,1380);update["placements"]=QJsonArray{up};update["id"]="23456789-1234-4234-8234-123456789abc";update["request-sequence"]="2";update["context-generation"]="2";update["supersedes"]=id;misplace=true;
 existing.apply("owner",update,updatedSnapshot,100);for(int time=116;time<1300;time+=16)existing.tick(time);check(existingWrites==2&&existing.pending());
 // Non-lifecycle requests never reassert position. Uncooperative clients get
 // one correction, then the original deadline and bounded rollback still apply.
 centered=QRectF(100,100,800,600);int stubbornWrites=0;
 GeometryTransactions stubborn([&](auto)->std::optional<QRectF>{return centered;},[&](auto,auto target){++stubbornWrites;centered=QRectF(QPointF(318,357),target.size());return true;});
 stubborn.apply("owner",startup,snapshot(),0);for(int time=16;time<=1184;time+=16)stubborn.tick(time);check(stubbornWrites==2);stubborn.tick(1200);check(stubbornWrites==3&&stubborn.status("owner",id)["state"]=="rolling_back");
 centered=QRectF(100,100,800,600);int directWrites=0;GeometryTransactions direct([&](auto)->std::optional<QRectF>{return centered;},[&](auto,auto target){++directWrites;centered=QRectF(QPointF(318,357),target.size());return true;});direct.apply("owner",request(),snapshot(),0);for(int time=16;time<1200;time+=16)direct.tick(time);check(directWrites==1);
 // Applied and pending ownership can be abandoned without any additional writes.
 // A removed restore call must not change its receipt, ownership or geometry.
 const auto noOpReceipt=noOp.status("owner",id);rejects([&]{noOp.restore("owner",id,10);});check(noOp.status("owner",id)==noOpReceipt&&noOpWrites==0);
 noOp.release("stranger",11);check(noOp.owns(native)&&noOpWrites==0);
 noOp.teardown();check(!noOp.owns(native)&&noOpWrites==0);
 actual[native]=QRectF(100,100,800,600);writes.clear();GeometryTransactions abandoned([&](auto key)->std::optional<QRectF>{return actual.value(key);},[&](auto key,auto target){writes.append({key,target});actual[key]=target;return true;});
 abandoned.apply("owner",request(),snapshot(),0);check(writes.size()==1&&abandoned.pending());const auto pendingFrame=actual[native];abandoned.release("owner",1);abandoned.tick(1500);abandoned.teardown();check(writes.size()==1&&actual[native]==pendingFrame&&!abandoned.owns(native));rejects([&]{abandoned.status("owner",id);});
 // Reconnecting captures the current frame as input, without a historical baseline.
 auto reconnect=request(),freshSnapshot=snapshot();auto freshWindows=freshSnapshot["windows"].toArray();auto freshWindow=freshWindows[0].toObject();freshWindow["frame"]=rect(pendingFrame.x(),pendingFrame.y(),pendingFrame.width(),pendingFrame.height());freshWindows[0]=freshWindow;freshSnapshot["windows"]=freshWindows;auto freshPlacements=reconnect["placements"].toArray();auto freshPlacement=freshPlacements[0].toObject();freshPlacement["expected-frame"]=freshWindow["frame"];freshPlacements[0]=freshPlacement;reconnect["placements"]=freshPlacements;
 check(abandoned.apply("new-owner",reconnect,freshSnapshot,1600)["state"]=="applied"&&writes.size()==1);abandoned.teardown();check(actual[native]==pendingFrame&&writes.size()==1);
 std::cout<<"PASS stale-plan rejection, delayed acknowledgement, no-op ownership, selective writes, idempotency, owner isolation, no-move disconnect/teardown, rollback and window loss\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
