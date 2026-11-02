#include "layers.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *error){if(!ok)throw std::invalid_argument(error);}
bool concrete(const QString &layer){return layer=="above"||layer=="normal"||layer=="below";}
}
bool setLayerFlags(const QString &layer,
    const std::function<std::optional<std::pair<bool,bool>>()> &read,
    const std::function<void(bool)> &above,const std::function<void(bool)> &below) {
    need(concrete(layer),"invalid concrete layer");auto before=read();if(!before||(before->first&&before->second))return false;
    auto apply=[&](std::pair<bool,bool> desired){
        auto current=read();if(!current)return false;
        // Clear the opposing flag first. A forced KDE rule may refuse it;
        // do not then try to enable a contradictory flag.
        if(!desired.first&&current->first){above(false);current=read();if(!current||current->first)return false;}
        if(!desired.second&&current->second){below(false);current=read();if(!current||current->second)return false;}
        if(desired.first&&!current->first){above(true);current=read();if(!current)return false;}
        if(desired.second&&!current->second){below(true);current=read();if(!current)return false;}
        return *current==desired;
    };
    if(apply({layer=="above",layer=="below"}))return true;
    // A rejected request must not leave the opposing flag cleared. These
    // same setters still defer to KDE rules; never bypass or change the rules.
    if(read())apply(*before);
    return false;
}
QString LayerRequests::automatic(const QString &uuid,const QString &baseline) const {
    if(baseline!="normal")return baseline;
    if(priority_&&priority_(uuid)>0)return "above";
    return managed_(uuid)?QString("below"):QString("normal");
}
bool LayerRequests::orderPriorities() {
    if(!priority_||!stack_||!restack_)return false;
    const auto stack=stack_();int highestZoom=-1;
    auto eligible=[&](const QString &uuid){auto it=entries_.constFind(uuid);return it!=entries_.cend()&&it->mode=="auto"&&!it->suppressed&&read_(uuid)=="above";};
    for(int i=0;i<stack.size();++i)if(eligible(stack[i])&&priority_(stack[i])==1)highestZoom=i;
    if(highestZoom<0)return false;
    bool changed=false;auto reference=stack[highestZoom];
    // Both special groups use KDE's above layer. Repair only inversions;
    // preserve order within each group and leave explicit overrides alone.
    for(int i=0;i<highestZoom;++i)if(eligible(stack[i])&&priority_(stack[i])==2){
        if(restack_(stack[i],reference)){reference=stack[i];changed=true;}
    }
    return changed;
}
QJsonObject LayerRequests::submit(const QString &owner,const QJsonObject &r,const QJsonObject &s) {
    need(!owner.isEmpty()&&r["protocol"]==1&&r["bridge-instance"]==s["instance"],"invalid layer caller, protocol or bridge instance");
    auto id=r["id"].toString(),uuid=r["window"].toString(),layer=r["layer"].toString();auto digest=QJsonDocument(r).toJson(QJsonDocument::Compact);
    need(!QUuid(id).isNull()&&!QUuid(uuid).isNull(),"invalid layer request or window ID");
    if(jobs_.contains(id)){need(jobs_[id].owner==owner&&jobs_[id].digest==digest,"layer request ID reused");return jobs_[id].result;}
    bool ok=false;auto text=r["request-sequence"].toString();auto seq=text.toULongLong(&ok);
    need(ok&&seq>0&&QString::number(seq)==text&&seq>sequences_.value(owner),"invalid or expired layer request sequence");
    need(sequences_.contains(owner)||sequences_.size()<64,"layer owner capacity exceeded");
    need(concrete(layer)||layer=="auto","layer must be below, normal, above or auto");
    need(!r.contains("reset-auto")||(r["reset-auto"].isBool()&&layer=="auto"),"invalid automatic layer reset");
    QJsonObject window;for(auto v:s["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))window=v.toObject();
    need(!window.isEmpty()&&!window["fullscreen"].toBool(),"layer target is missing or native fullscreen");
    auto actual=read_(uuid);need(concrete(actual)&&window["layer"]==actual&&r["expected-layer"]==actual,"window layer changed since selection");
    need(!entries_.contains(uuid)||entries_[uuid].owner==owner,"window layer belongs to another controller");
    need(entries_.contains(uuid)||entries_.size()<256,"layer window capacity exceeded");
    Entry next;next.owner=owner;next.mode=layer;next.baseline=actual;next.last=actual;
    if(layer=="auto"&&r["reset-auto"].toBool())next.baseline="normal";
    const auto desired=layer=="auto"?automatic(uuid,next.baseline):layer;
    bool success=actual==desired||write_(uuid,desired);actual=read_(uuid);success=success&&actual==desired;
    if(success){next.last=actual;entries_[uuid]=next;}
    auto result=QJsonObject{{"id",id},{"window",uuid},{"requested-layer",layer},{"layer",actual},{"state",success?"applied":"blocked"},{"completed",true},{"success",success},{"acknowledged",success},{"error",success?"":"KWin rejected the layer change; check KDE Keep Above/Keep Below window rules"}};
    if(jobs_.size()>=64)jobs_.remove(order_.takeFirst());
    order_.append(id);jobs_[id]={owner,digest,result};sequences_[owner]=seq;
    return result;
}
QJsonObject LayerRequests::describe(const QString &uuid) const {
    if(!entries_.contains(uuid))return {{"layer-mode","auto"},{"layer-error",""}};
    const auto &e=entries_[uuid];return {{"layer-mode",e.suppressed?QString("external"):e.mode},{"layer-error",e.error}};
}
bool LayerRequests::tick() {
    bool changed=false;
    for(auto it=entries_.begin();it!=entries_.end();) {
        const auto uuid=it.key();auto &e=it.value();auto actual=read_(uuid);
        if(actual.isEmpty()){it=entries_.erase(it);continue;}++it;
        if(e.mode!="auto"||e.suppressed)continue;
        if(actual!=e.last){e.suppressed=true;e.error="layer changed outside kwabai; automatic changes relinquished";changed=true;continue;}
        const auto desired=automatic(uuid,e.baseline);
        if(actual==desired)continue;
        changed=true;
        if(!write_(uuid,desired)||read_(uuid)!=desired){e.suppressed=true;e.error="KDE window rules blocked automatic layer adjustment";}
        else e.last=desired;
    }
    return orderPriorities()||changed;
}
void LayerRequests::release(const QString &owner) {
    for(auto it=entries_.begin();it!=entries_.end();)if(it->owner==owner){
        // Undo only our temporary automatic layer, never a later user change.
        if(it->mode=="auto"&&!it->suppressed&&it->last!=it->baseline&&read_(it.key())==it->last)write_(it.key(),it->baseline);
        it=entries_.erase(it);
    }else ++it;
    sequences_.remove(owner);for(auto it=jobs_.begin();it!=jobs_.end();)if(it->owner==owner){order_.removeOne(it.key());it=jobs_.erase(it);}else ++it;
}
void LayerRequests::teardown(){while(!entries_.isEmpty())release(entries_.first().owner);jobs_.clear();order_.clear();sequences_.clear();}
}
