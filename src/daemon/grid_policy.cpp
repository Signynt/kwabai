#include "grid_policy.h"
#include "../compat/grid.h"
#include "geometry.h"
#include <QJsonArray>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}}
GridPolicy::GridPolicy(BridgeClient &b,ContextStore &c,ContextManager &m,bool enabled):bridge_(b),contexts_(c),manager_(m),enabled_(enabled){
    QObject::connect(&b,&BridgeClient::updated,&timer_,[this]{step();},Qt::QueuedConnection);
    timer_.setInterval(100);QObject::connect(&timer_,&QTimer::timeout,[this]{step();});timer_.start();
}
QString GridPolicy::signature(const QJsonObject &w) const {auto rule=contexts_.layerRule(w);return rule["grid-rule"].toString()+":"+rule["grid"].toString();}
QJsonObject GridPolicy::submit(const QJsonObject &window,const QString &text){
    need(enabled_,"grid control requires --enable-control");need(bridge_.state().hasFloatingGrid(),"floating grids require the updated effect; run scripts/reload-effect.sh");
    need(!bridge_.geometryBusy()&&!manager_.mouseDropBusy(),"another geometry operation is unresolved; wait for confirmation");
    auto grid=compat::parseGrid(text);const auto uuid=window["uuid"].toString();
    need(!bridge_.controlledWindows().contains(uuid),"grid requires a floating window; toggle float or assign a scratchpad first");
    need(window["candidate"].toBool()&&!window["minimized"].toBool()&&!window["fullscreen"].toBool()&&!window["scratchpad-hidden"].toBool(),"grid requires an eligible visible floating window");
    need(manager_.layout()!="bsp"||!window["scratchpad"].toString().isEmpty()||contexts_.testRule(bridge_.state(),uuid)["action"]=="float","grid requires a floating window; toggle float or use manage=off/scratchpad first");
    QJsonObject output;for(auto v:bridge_.state().outputs()["outputs"].toArray())if(v.toObject()["name"]==window["output"])output=v.toObject();
    need(!output.isEmpty(),"grid window output is unavailable");
    double top=contexts_.globalSpacing("top_padding"),right=contexts_.globalSpacing("right_padding"),bottom=contexts_.globalSpacing("bottom_padding"),left=contexts_.globalSpacing("left_padding"),gap=contexts_.globalSpacing("window_gap");
    QJsonObject context;try{context=contexts_.show(bridge_.state(),output["id"].toString());}catch(const std::invalid_argument &){}
    if(!context.isEmpty()){auto p=context["padding"].toArray();top=p[0].toDouble();right=p[1].toDouble();bottom=p[2].toDouble();left=p[3].toDouble();gap=context["gap"].toDouble();}
    auto target=compat::gridFrame(grid,output["work-area"].toObject(),top,right,bottom,left,gap);
    QJsonArray placements{QJsonObject{{"uuid",uuid},{"id",window["id"]},{"visible",true},{"expected-frame",window["frame"]},{"frame",target}}};
    QJsonObject request{{"protocol",1},{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"bridge-instance",bridge_.state().windows()["instance"]},{"floating-frame",true},
        {"context",QJsonObject{{"output-id",output["id"]},{"output",output["name"]},{"desktop",output["current-desktop"]},{"activity",output["current-activity"]}}},{"work-area",output["work-area"]},{"placements",placements}};
    auto snapshot=bridge_.state().windows();snapshot["outputs"]=bridge_.state().outputs()["outputs"];
    validateGeometryRequest(request,snapshot);auto receipt=bridge_.submitGeometry(request);receipt["confirmed-placements"]=placements;return receipt;
}
QJsonObject GridPolicy::command(const QString &selector,const QString &grid){
    QJsonObject window;int matches=0;for(auto v:bridge_.state().windows()["windows"].toArray()){auto w=v.toObject();if((selector=="focused"&&w["focused"].toBool())||w["id"]==QJsonValue(selector)){window=w;++matches;}}
    need(matches==1,"grid window selector missing or ambiguous");auto receipt=submit(window,grid);seen_[window["uuid"].toString()]=signature(window);return receipt;
}
QJsonObject GridPolicy::status() const {QJsonObject errors;for(auto it=errors_.begin();it!=errors_.end();++it)errors[it.key()]=it.value();return {{"enabled",enabled_},{"available",bridge_.state().hasFloatingGrid()},{"pending",pending_},{"errors",errors}};}
void GridPolicy::step(){
    if(!enabled_||paused_||!bridge_.state().hasFloatingGrid()||!bridge_.eventStatus()["ready"].toBool())return;
    auto instance=bridge_.state().windows()["instance"].toString();if(instance!=instance_){instance_=instance;pending_.clear();seen_.clear();errors_.clear();}
    if(!pending_.isEmpty()){
        auto job=bridge_.transaction(pending_);if(!job["completed"].toBool())return;
        bool matches=job["success"].toBool()&&job["acknowledged"].toBool();
        if(matches){auto placements=job["target-placements"].toArray();for(auto value:placements){auto p=value.toObject();QJsonObject w;for(auto v:bridge_.state().windows()["windows"].toArray())if(v.toObject()["uuid"]==p["uuid"])w=v.toObject();if(w.isEmpty())matches=false;for(auto f:{"x","y","w","h"})if(std::abs(w["frame"].toObject()[f].toDouble()-p["frame"].toObject()[f].toDouble())>1)matches=false;}
            if(!matches&&clock_.elapsed()<2000)return;
        }
        if(matches)errors_.remove(pendingWindow_);else errors_[pendingWindow_]=job["success"].toBool()?QString("grid acknowledged but frame was not observed; request was not replayed"):job["error"].toString();
        pending_.clear();
    }
    if(bridge_.geometryBusy()||manager_.mouseDropBusy())return;
    for(auto value:manager_.status()["contexts"].toArray())if(value.toObject()["paused-for-transfer"].toBool())return;
    QSet<QString> live;for(auto v:bridge_.state().windows()["windows"].toArray())live.insert(v.toObject()["uuid"].toString());
    for(auto it=seen_.begin();it!=seen_.end();)if(!live.contains(it.key())){errors_.remove(it.key());it=seen_.erase(it);}else ++it;
    for(auto value:bridge_.state().windows()["windows"].toArray()){
        auto w=value.toObject();auto uuid=w["uuid"].toString(),sig=signature(w);if(seen_.value(uuid)==sig)continue;
        auto rule=contexts_.layerRule(w);auto grid=rule["grid"].toString();if(grid.isEmpty()){seen_[uuid]=sig;continue;}
        // Scratchpad assignment and native ownership release precede its grid.
        if(!w["candidate"].toBool()||w["scratchpad-hidden"].toBool()||bridge_.controlledWindows().contains(uuid))continue;
        if(!rule["scratchpad"].toString().isEmpty()&&w["scratchpad"]!=rule["scratchpad"])continue;
        try{auto receipt=submit(w,grid);pending_=receipt["id"].toString();pendingWindow_=uuid;clock_.start();seen_[uuid]=sig;}
        catch(const std::exception &e){errors_[uuid]=QString::fromUtf8(e.what());seen_[uuid]=sig;}
        return;
    }
}
}
