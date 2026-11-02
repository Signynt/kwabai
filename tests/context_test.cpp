#include "store.h"
#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("context assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
QJsonObject window(int n){return {{"uuid",QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'))},{"app","test"},{"title","test"},{"output","DP-1"},{"desktops",QJsonArray{}},{"activities",QJsonArray{}},{"frame",rect(100,100,800,600)},{"minimum",QJsonObject{{"w",100},{"h",100}}},{"candidate",true},{"minimized",false},{"fullscreen",false},{"focused",false}};}
int main(){try{
 const QString desktop="11111111-1111-4111-8111-111111111111",other="22222222-2222-4222-8222-222222222222";
 QJsonArray windows;for(int i=1;i<=5;i++)windows.append(window(i));
 QJsonObject s{{"protocol",1},{"instance","33333333-3333-4333-8333-333333333333"},{"sequence","1"},{"kwin-version","6.8.1"},{"capabilities",QJsonArray{"snapshot","contexts","geometry"}},{"geometry-window-limit",20},{"outputs",QJsonArray{QJsonObject{{"id","output"},{"name","DP-1"},{"scale",1},{"geometry",rect(0,0,2560,1440)},{"work-area",rect(0,40,2560,1400)},{"current-desktop",desktop},{"current-activity","work"}}}},{"windows",windows}};
 const auto base=s;
 BridgeState bridge;bridge.accept(QJsonDocument(s).toJson());ContextStore contexts;
 // Automatic admissions break equal-area ties toward the bottom/right.
 ContextStore insertion;for(int i=1;i<=3;++i)insertion.edit(bridge,{"add","DP-1",QString::number(i)});
 auto automatic=insertion.plan(bridge,"DP-1")["placements"].toArray();QMap<QString,QJsonObject> frames;
 for(auto p:automatic){auto w=p.toObject();frames[w["id"].toString()]=w["frame"].toObject();}
 check(frames["1"]["h"].toDouble()>frames["2"]["h"].toDouble());
 check(frames["3"]["x"]==frames["2"]["x"]&&frames["3"]["y"].toDouble()>frames["2"]["y"].toDouble());
 // A larger leaf still wins, avoiding repeated subdivision of a small corner.
 insertion.edit(bridge,{"add","DP-1","4"});automatic=insertion.plan(bridge,"DP-1")["placements"].toArray();
 for(auto p:automatic){auto w=p.toObject();frames[w["id"].toString()]=w["frame"].toObject();}
 check(frames["4"]["x"]==frames["1"]["x"]&&frames["4"]["y"].toDouble()>frames["1"]["y"].toDouble());
 contexts.edit(bridge,{"add","DP-1","1"});contexts.edit(bridge,{"add","DP-1","2","1","east","0.6"});
 auto before=contexts.state();auto originalTree=contexts.show(bridge,"DP-1")["tree"].toObject();
 contexts.edit(bridge,{"swap","DP-1","1","2"});check(contexts.show(bridge,"DP-1")["tree"].toObject()["root"]==originalTree["root"]);
 auto placements=contexts.plan(bridge,"DP-1")["placements"].toArray();check(placements[0].toObject()["id"]==QJsonValue("2"));
 contexts.edit(bridge,{"swap","DP-1","1","2"});contexts.edit(bridge,{"ratio","DP-1","1","x","0.7"});
 auto saved=contexts.state();rejects([&]{contexts.edit(bridge,{"add","DP-1","1"});});check(contexts.state()==saved);rejects([&]{contexts.edit(bridge,{"ratio","DP-1","1","x","NaN"});});check(contexts.state()==saved);
 for(int i=3;i<=5;i++)contexts.edit(bridge,{"add","DP-1",QString::number(i)});
 check(contexts.plan(bridge,"DP-1")["placements"].toArray().size()==5);
 auto request=geometryRequest(contexts.plan(bridge,"DP-1"),bridge.windows(),"44444444-4444-4444-8444-444444444444");validateGeometryRequest(request,s);
 auto portrait=QJsonObject{{"id","portrait-output"},{"name","DP-2"},{"scale",1},{"geometry",rect(-1080,0,1080,1920)},{"work-area",rect(-1080,0,1080,1920)},{"current-desktop",desktop},{"current-activity","work"}};
 auto allOutputs=s["outputs"].toArray();allOutputs.append(portrait);s["outputs"]=allOutputs;auto sixth=window(6);sixth["output"]="DP-2";windows.append(sixth);s["windows"]=windows;s["sequence"]="2";bridge.accept(QJsonDocument(s).toJson());auto landscape=contexts.show(bridge,"DP-1");contexts.edit(bridge,{"add","DP-2","6"});check(contexts.show(bridge,"DP-1")==landscape);check(contexts.list()["contexts"].toArray().size()==2);check(contexts.plan(bridge,"DP-2")["placements"].toArray()[0].toObject()["frame"].toObject()["x"]==QJsonValue(-1070));
 auto snapshot=contexts.state();auto restored=ContextStore::restore(snapshot);check(restored.state()==snapshot);
 // A new observer may assign different public IDs to the same UUIDs.
 QJsonArray reversed;for(qsizetype i=windows.size();i>0;--i)reversed.append(windows[i-1]);windows=reversed;s["windows"]=windows;s["instance"]="55555555-5555-4555-8555-555555555555";BridgeState rebound;rebound.accept(QJsonDocument(s).toJson());
 auto reboundPlan=restored.plan(rebound,"DP-1");check(reboundPlan["placements"].toArray()[0].toObject()["uuid"]==contexts.plan(bridge,"DP-1")["placements"].toArray()[0].toObject()["uuid"]);check(reboundPlan["placements"].toArray()[0].toObject()["id"]!=contexts.plan(bridge,"DP-1")["placements"].toArray()[0].toObject()["id"]);
 // Desktop switching preserves an independent tree instead of relocating windows.
 auto outputs=s["outputs"].toArray();auto o=outputs[0].toObject();o["current-desktop"]=other;outputs[0]=o;s["outputs"]=outputs;s["sequence"]="3";rebound.accept(QJsonDocument(s).toJson());rejects([&]{restored.show(rebound,"DP-1");});rejects([&]{restored.edit(rebound,{"add","DP-1","1"});});check(restored.state()==snapshot);
 o["current-desktop"]=desktop;outputs[0]=o;s["outputs"]=outputs;s["sequence"]="4";windows.removeLast();s["windows"]=windows;rebound.accept(QJsonDocument(s).toJson());rejects([&]{restored.plan(rebound,"DP-1");});auto beforePrune=restored.show(rebound,"DP-1");check(beforePrune["members"].toArray().size()==5);restored.edit(rebound,{"prune","DP-1"});check(restored.plan(rebound,"DP-1")["placements"].toArray().size()==4);
 auto corrupt=snapshot;auto cs=corrupt["contexts"].toArray();auto c=cs[0].toObject();c["members"]=QJsonArray{};cs[0]=c;corrupt["contexts"]=cs;rejects([&]{ContextStore::restore(corrupt);});
 // Reconciliation previews are passive; a batch preserves blocked members and prunes only missing UUIDs.
 auto current=base;BridgeState lifecycle;lifecycle.accept(QJsonDocument(current).toJson());ContextStore managed;
 auto emptyState=managed.state();auto preview=managed.reconcile(lifecycle,"DP-1");check(preview["add"].toArray().size()==5);check(managed.state()==emptyState);
 managed.edit(lifecycle,{"reconcile","DP-1"});check(managed.show(lifecycle,"DP-1")["generation"]==QJsonValue("1"));check(managed.plan(lifecycle,"DP-1")["placements"].toArray().size()==5);
 auto unchanged=managed.state();managed.edit(lifecycle,{"reconcile","DP-1"});check(managed.state()==unchanged);
 auto changed=base["windows"].toArray();changed.removeFirst();auto minimized=changed[0].toObject();minimized["minimized"]=true;changed[0]=minimized;
 auto moved=changed[1].toObject();moved["desktops"]=QJsonArray{other};changed[1]=moved;changed.append(window(7));current["windows"]=changed;current["sequence"]="2";lifecycle.accept(QJsonDocument(current).toJson());
 preview=managed.reconcile(lifecycle,"DP-1");check(preview["remove"].toArray().size()==1);check(preview["blocked"].toArray().size()==2);check(preview["add"].toArray().size()==1);
 managed.edit(lifecycle,{"reconcile","DP-1"});check(managed.show(lifecycle,"DP-1")["members"].toArray().size()==5);check(managed.show(lifecycle,"DP-1")["generation"]==QJsonValue("2"));rejects([&]{managed.plan(lifecycle,"DP-1");});check(ContextStore::restore(managed.state()).state()==managed.state());
 // A batch exceeding capacity rejects without publishing pruning or identity allocation.
 auto prior=managed.state();for(int i=8;i<30;++i)changed.append(window(i));current["windows"]=changed;current["sequence"]="3";lifecycle.accept(QJsonDocument(current).toJson());check(!managed.reconcile(lifecycle,"DP-1")["within-capacity"].toBool());rejects([&]{managed.edit(lifecycle,{"reconcile","DP-1"});});check(managed.state()==prior);
 // Sticky windows already held by another stored desktop must not be claimed twice.
 auto switched=base;auto switchedOutputs=switched["outputs"].toArray();auto switchedOutput=switchedOutputs[0].toObject();switchedOutput["current-desktop"]=other;switchedOutputs[0]=switchedOutput;switched["outputs"]=switchedOutputs;BridgeState secondDesktop;secondDesktop.accept(QJsonDocument(switched).toJson());preview=managed.reconcile(secondDesktop,"DP-1");check(preview["add"].toArray().size()==1);check(preview["excluded"].toArray().size()==4);
 lifecycle.disconnect("test disconnect");rejects([&]{managed.reconcile(lifecycle,"DP-1");});check(managed.state()==prior);
 // Global preview settings survive state saves without editing any tree,
 // generations or undo history; pre-settings schemas inherit the default.
 auto beforeColor=contexts.state();check(contexts.insertFeedbackColor()=="0xffd75f5f");contexts.setInsertFeedbackColor("0x80AABBCC");auto colored=contexts.state();check(colored["contexts"]==beforeColor["contexts"]);check(ContextStore::restore(colored).insertFeedbackColor()=="0x80aabbcc");
 auto legacyColor=colored;legacyColor["version"]=5;legacyColor.remove("settings");check(ContextStore::restore(legacyColor).insertFeedbackColor()=="0xffd75f5f");
 auto version6=colored;version6["version"]=6;version6["settings"]=QJsonObject{{"insert_window_border_color","0x80aabbcc"}};auto migrated6=ContextStore::restore(version6);check(migrated6.insertFeedbackColor()=="0x80aabbcc"&&migrated6.windowBorderRadius()==0);
 contexts.setWindowBorderRadius("12");check(ContextStore::restore(contexts.state()).windowBorderRadius()==12);check(contexts.state()["contexts"]==colored["contexts"]);for(auto invalid:{"-1","21","1.5","nan",""})rejects([&]{contexts.setWindowBorderRadius(invalid);});check(contexts.windowBorderRadius()==12);
 auto corruptColor=colored;corruptColor["settings"]=QJsonObject{{"insert_feedback_color","0xnope"},{"window_border_radius",0}};rejects([&]{ContextStore::restore(corruptColor);});
 rejects([&]{contexts.setInsertFeedbackColor("#ffffff");});check(contexts.insertFeedbackColor()=="0x80aabbcc");
 std::cout<<"PASS stored topology, generations, atomic edits, five-window planning, UUID rebinding, desktop isolation and prune\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
