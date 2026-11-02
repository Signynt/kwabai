#include "layer_policy.h"
#include <QJsonArray>
#include <QUuid>
#include <stdexcept>
namespace kwabai {
LayerPolicy::LayerPolicy(BridgeClient &b,ContextStore &c,bool enabled):bridge_(b),contexts_(c),enabled_(enabled) {
    QObject::connect(&b,&BridgeClient::updated,&timer_,[this]{step();},Qt::QueuedConnection);
    timer_.setInterval(200);QObject::connect(&timer_,&QTimer::timeout,[this]{step();});timer_.start();
}
QString LayerPolicy::signature(const QJsonObject &w) const {auto r=contexts_.layerRule(w);return r.contains("layer")?r["layer-rule"].toString()+":"+r["layer"].toString():QString("default");}
QJsonObject LayerPolicy::submit(const QJsonObject &w,const QString &layer,bool reset) {
    QJsonObject r{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"window",w["uuid"]},{"window-id",w["id"]},{"layer",layer},{"expected-layer",w["layer"]}};
    if(layer=="auto")r["reset-auto"]=reset;
    auto receipt=bridge_.submitLayer(r);pending_=receipt["id"].toString();pendingWindow_=w["uuid"].toString();pendingClock_.start();return receipt;
}
QJsonObject LayerPolicy::command(const QString &selector,const QString &layer) {
    if(!enabled_)throw std::invalid_argument("layer control requires --enable-control");
    if(!QStringList{"above","below","normal","auto"}.contains(layer))throw std::invalid_argument("layer must be above, below, normal or auto");
    if(!settle())throw std::invalid_argument("another layer request is pending");
    QJsonObject selected;int matches=0;for(auto v:bridge_.state().windows()["windows"].toArray()){auto w=v.toObject();if((selector=="focused"&&w["focused"].toBool())||w["id"]==QJsonValue(selector)){selected=w;++matches;}}
    if(matches!=1)throw std::invalid_argument("layer window selector missing or ambiguous");
    auto receipt=submit(selected,layer,true);seen_[selected["uuid"].toString()]=signature(selected);return receipt;
}
QJsonObject LayerPolicy::status() const {QJsonObject errors;for(auto it=errors_.begin();it!=errors_.end();++it)errors[it.key()]=it.value();return {{"enabled",enabled_},{"available",bridge_.state().hasLayers()},{"pending",pending_},{"errors",errors},{"automatic","scratchpad-above-zoom-above-floating-above-tiled"}};}
bool LayerPolicy::settle() {
    if(pending_.isEmpty())return true;
    auto job=bridge_.layerStatus(pending_);if(!job["completed"].toBool())return false;
    if(job["success"].toBool()){
        bool current=false;for(auto v:bridge_.state().windows()["windows"].toArray()){auto w=v.toObject();if(w["uuid"]==QJsonValue(pendingWindow_)&&w["layer"]==job["layer"])current=true;}
        if(!current){
            if(pendingClock_.isValid()&&pendingClock_.elapsed()<1500)return false;
            errors_[pendingWindow_]="layer acknowledged but snapshot changed; request was not replayed";pending_.clear();pendingWindow_.clear();return true;
        }
        errors_.remove(pendingWindow_);
    }else errors_[pendingWindow_]=job["error"].toString();
    pending_.clear();pendingWindow_.clear();return true;
}
void LayerPolicy::step() {
    if(!enabled_||paused_||!bridge_.state().hasLayers()||!bridge_.eventStatus()["ready"].toBool())return;
    auto instance=bridge_.state().windows()["instance"].toString();if(instance!=instance_){instance_=instance;pending_.clear();seen_.clear();errors_.clear();}
    if(!settle())return;
    if(bridge_.geometryBusy())return;
    QSet<QString> live;for(auto v:bridge_.state().windows()["windows"].toArray())live.insert(v.toObject()["uuid"].toString());
    for(auto it=seen_.begin();it!=seen_.end();)if(!live.contains(it.key())){errors_.remove(it.key());it=seen_.erase(it);}else ++it;
    for(auto v:bridge_.state().windows()["windows"].toArray()) {
        auto w=v.toObject();if(w["fullscreen"].toBool()||w["interaction"].toObject()["state"]=="active")continue;
        const auto uuid=w["uuid"].toString(),sig=signature(w);if(seen_.value(uuid)==sig)continue;
        // Removing a rule does not undo an already applied window property.
        if(sig=="default"&&seen_.contains(uuid)){seen_[uuid]=sig;continue;}
        auto rule=contexts_.layerRule(w);auto desired=rule.value("layer").toString("auto");
        try {submit(w,desired,rule.contains("layer"));seen_[uuid]=sig;}
        catch(const std::exception &e){errors_[uuid]=QString::fromUtf8(e.what());seen_[uuid]=sig;}
        return;
    }
}
}
