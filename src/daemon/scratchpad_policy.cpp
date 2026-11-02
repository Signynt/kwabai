#include "scratchpad_policy.h"
#include <QJsonArray>
namespace kwabai {
ScratchpadPolicy::ScratchpadPolicy(BridgeClient &b,ContextStore &c,ContextManager &m,bool enabled):bridge_(b),contexts_(c),manager_(m),enabled_(enabled){
    QObject::connect(&b,&BridgeClient::updated,&timer_,[this]{step();},Qt::QueuedConnection);
    timer_.setInterval(200);QObject::connect(&timer_,&QTimer::timeout,[this]{step();});timer_.start();
}
QString ScratchpadPolicy::signature(const QJsonObject &w) const {auto r=contexts_.layerRule(w);return r["scratchpad-rule"].toString()+":"+r["scratchpad"].toString();}
void ScratchpadPolicy::overrideWindow(const QString &uuid){errors_.remove(uuid);for(auto v:bridge_.state().windows()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))seen_[uuid]=signature(v.toObject());}
QJsonObject ScratchpadPolicy::status() const {QJsonObject errors;for(auto it=errors_.begin();it!=errors_.end();++it)errors[it.key()]=it.value();return {{"enabled",enabled_},{"available",bridge_.state().hasScratchpads()},{"pending",pending_},{"errors",errors}};}
void ScratchpadPolicy::step(){
    if(!enabled_||paused_||!bridge_.state().hasScratchpads()||!bridge_.eventStatus()["ready"].toBool())return;
    auto instance=bridge_.state().windows()["instance"].toString();if(instance!=instance_){instance_=instance;pending_.clear();seen_.clear();errors_.clear();}
    if(!pending_.isEmpty()){auto job=bridge_.scratchpadStatus(pending_);if(!job["completed"].toBool())return;if(!job["success"].toBool())errors_[pendingWindow_]=job["error"].toString();else errors_.remove(pendingWindow_);pending_.clear();}
    if(bridge_.geometryBusy()||manager_.mouseDropBusy())return;
    for(auto v:manager_.status()["contexts"].toArray())if(v.toObject()["paused-for-transfer"].toBool())return;
    QSet<QString> live;for(auto v:bridge_.state().windows()["windows"].toArray())live.insert(v.toObject()["uuid"].toString());
    for(auto it=seen_.begin();it!=seen_.end();)if(!live.contains(it.key())){errors_.remove(it.key());it=seen_.erase(it);}else ++it;
    for(auto v:bridge_.state().windows()["windows"].toArray()){
        auto w=v.toObject();auto uuid=w["uuid"].toString(),sig=signature(w);if(seen_.value(uuid)==sig)continue;
        auto rule=contexts_.layerRule(w);auto label=rule["scratchpad"].toString();
        if(label.isEmpty()||w["scratchpad"]==QJsonValue(label)){seen_[uuid]=sig;continue;}
        if(!w["candidate"].toBool()||w["fullscreen"].toBool()||w["interaction"].toObject()["state"]=="active")continue;
        if(bridge_.controlledWindows().contains(uuid)){
            bool lifecycle=false;
            for(auto value:manager_.status()["contexts"].toArray()){
                auto status=value.toObject();if(!status["enabled"].toBool()||!status["automatic-membership"].toBool())continue;
                auto active=bridge_.contextStatus(status["context-id"].toString())["active"].toObject();
                if(active["managed-windows"].toArray().contains(uuid))lifecycle=true;
            }
            if(!lifecycle){errors_[uuid]="scratchpad assignment requires BSP lifecycle management for this tiled window";continue;}
        }
        try{auto receipt=bridge_.submitScratchpad({{"operation","assign"},{"window",uuid},{"label",label}});pending_=receipt["id"].toString();pendingWindow_=uuid;seen_[uuid]=sig;}
        catch(const std::exception &e){if(QString::fromUtf8(e.what())=="another scratchpad request is pending")return;errors_[uuid]=QString::fromUtf8(e.what());seen_[uuid]=sig;}
        return;
    }
}
}
