#include "plan.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("plan assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
const QString desktop="11111111-1111-4111-8111-111111111111",otherDesktop="22222222-2222-4222-8222-222222222222";
QJsonObject rect(double x,double y,double w,double h){return {{"x",x},{"y",y},{"w",w},{"h",h}};}
QJsonObject output(const QString &name,double x,double width,double height){return {{"id",name+"-stable"},{"name",name},{"scale",1.25},{"geometry",rect(x,0,width,height)},{"work-area",rect(x,40,width,height-40)},{"current-desktop",desktop},{"current-activity","work"}};}
QJsonObject window(int n){return {{"uuid",QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'))},{"app","kitty"},{"title","test"},{"output","DP-1"},{"desktops",QJsonArray{"{11111111-1111-4111-8111-111111111111}"}},{"activities",QJsonArray{"work"}}, {"frame",rect(0,0,500,500)},{"minimum",QJsonObject{{"w",100},{"h",100}}},{"minimized",false},{"fullscreen",false},{"focused",false},{"candidate",true}};}
int main(){try{
 QJsonArray windows;for(int i=1;i<=5;i++)windows.append(window(i));
 QJsonObject s{{"protocol",1},{"instance","33333333-3333-4333-8333-333333333333"},{"sequence","1"},{"kwin-version","6.8.1"},{"capabilities",QJsonArray{"snapshot","contexts"}},{"outputs",QJsonArray{output("DP-1",0,2560,1440),output("DP-2",-1080,1080,1920)}},{"windows",windows}};
 BridgeState state;auto load=[&]{state.accept(QJsonDocument(s).toJson());};load();check(state.hasContexts());auto before=state.windows();
 QStringList ids{"1","2","3","4","5"};auto plan=dryRunPlan(state,"DP-1",ids);check(plan["dry-run"].toBool());check(!plan["desktop-control"].toBool());check(plan["placements"].toArray().size()==5);check(plan==dryRunPlan(state,"DP-1-stable",ids));check(state.windows()==before);
 for(auto value:plan["placements"].toArray()){auto r=value.toObject()["frame"].toObject();check(r["y"].toDouble()>=50);check(r["x"].toDouble()>=10);check(r["w"].toDouble()>=100);check(r["h"].toDouble()>=100);}
 rejects([&]{dryRunPlan(state,"DP-1",{"1","1"});});rejects([&]{dryRunPlan(state,"DP-2",{"1"});});rejects([&]{dryRunPlan(state,"missing",ids);});rejects([&]{dryRunPlan(state,"DP-1",{"999"});});
 // A new topology and work area must recompute a portrait plan without stale geometry.
 s["sequence"]="2";for(qsizetype i=0;i<windows.size();i++){auto w=windows[i].toObject();w["output"]="DP-2";w["desktops"]=QJsonArray{};w["activities"]=QJsonArray{};windows[i]=w;}s["windows"]=windows;load();
 auto portrait=dryRunPlan(state,"DP-2",ids);for(auto value:portrait["placements"].toArray()){auto r=value.toObject()["frame"].toObject();check(r["x"].toDouble()>=-1070);check(r["x"].toDouble()+r["w"].toDouble()<=-10+1e-7);}
 // Visibility metadata is validated independently of the preliminary candidate flag.
 s["sequence"]="3";auto w=windows[0].toObject();w["desktops"]=QJsonArray{otherDesktop};windows[0]=w;s["windows"]=windows;load();rejects([&]{dryRunPlan(state,"DP-2",{"1"});});
 s["sequence"]="4";w["desktops"]=QJsonArray{};w["activities"]=QJsonArray{"personal"};windows[0]=w;s["windows"]=windows;load();rejects([&]{dryRunPlan(state,"DP-2",{"1"});});
 s["sequence"]="5";w["activities"]=QJsonArray{};w["minimum"]=QJsonObject{{"w",2000},{"h",100}};windows[0]=w;s["windows"]=windows;load();rejects([&]{dryRunPlan(state,"DP-2",{"1"});});
 auto valid=s;s["sequence"]="6";auto outputs=s["outputs"].toArray();auto bad=outputs[0].toObject();bad["work-area"]=rect(-100,0,4000,1440);outputs[0]=bad;s["outputs"]=outputs;rejects(load);check(state.outputs()["sequence"]==QJsonValue("5"));
 s=valid;s["sequence"]="6";outputs=s["outputs"].toArray();outputs.append(outputs[0]);s["outputs"]=outputs;rejects(load);
 s=valid;s["sequence"]="6";windows=s["windows"].toArray();w=windows[0].toObject();w["output"]="gone";windows[0]=w;s["windows"]=windows;rejects(load);
 state.disconnect("lost");check(state.outputs()["outputs"].toArray().isEmpty());rejects([&]{dryRunPlan(state,"DP-2",ids);});
 std::cout<<"PASS contextual planning, output validation, panels, portrait layouts, explicit selection and minimum-size rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
