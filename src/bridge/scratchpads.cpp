#include "scratchpads.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <stdexcept>
namespace kwabai {
namespace {void need(bool ok,const char *error){if(!ok)throw std::invalid_argument(error);}}
bool Scratchpads::validLabel(const QString &s){
    if(s.isEmpty()||s.size()>256||s.contains(QChar(0)))return false;
    return !QStringList{"recover","float","sticky","pip","shadow","split","zoom-parent","zoom-fullscreen","windowed-fullscreen","native-fullscreen","expose"}.contains(s);
}
QJsonObject Scratchpads::describe(const QString &uuid) const {
    return {{"scratchpad",labels_.value(uuid)},{"scratchpad-hidden",labels_.contains(uuid)&&read_(uuid)["hidden"].toBool()}};
}
QJsonArray Scratchpads::decorate(QJsonArray windows,const std::function<QJsonObject(const QString &)> &interaction) const {
    for(qsizetype i=0;i<windows.size();++i){
        auto window=windows[i].toObject();const auto uuid=window["uuid"].toString();
        auto info=describe(uuid);for(auto it=info.begin();it!=info.end();++it)window[it.key()]=it.value();
        if(interaction){auto gesture=interaction(uuid);if(!gesture.isEmpty())window["interaction"]=gesture;}
        // Scratchpad metadata is present even without a mouse interaction.
        windows[i]=window;
    }
    return windows;
}
void Scratchpads::prune(){for(auto it=labels_.begin();it!=labels_.end();)if(read_(it.key()).isEmpty())it=labels_.erase(it);else ++it;}
void Scratchpads::release(const QString &owner){sequences_.remove(owner);}
void Scratchpads::teardown(){for(auto it=labels_.begin();it!=labels_.end();++it)if(read_(it.key())["hidden"].toBool())write_(it.key(),false,false);labels_.clear();}
QJsonObject Scratchpads::submit(const QString &owner,const QJsonObject &r,const QJsonObject &s){
    need(!owner.isEmpty(),"scratchpad requires a D-Bus owner");
    const auto id=r["id"].toString();const auto digest=QJsonDocument(r).toJson(QJsonDocument::Compact);
    need(!QUuid(id).isNull()&&r["protocol"]==1&&r["bridge-instance"]==s["instance"],"invalid or stale scratchpad request");
    if(jobs_.contains(id)){need(jobs_[id].owner==owner&&jobs_[id].digest==digest,"scratchpad request ID reused");return jobs_[id].result;}
    bool ok=false;auto text=r["request-sequence"].toString();auto sequence=text.toULongLong(&ok);
    need(ok&&sequence>sequences_.value(owner)&&QString::number(sequence)==text,"invalid or expired scratchpad sequence");
    need(sequences_.contains(owner)||sequences_.size()<64,"scratchpad owner capacity exceeded");
    prune();const auto op=r["operation"].toString(),label=r["label"].toString();auto uuid=r["window"].toString();
    need(QStringList{"assign","clear","toggle","recover"}.contains(op),"invalid scratchpad operation");
    if(op=="assign"){
        need(validLabel(label),"invalid or reserved scratchpad label");need(!labels_.values().contains(label),"scratchpad label already assigned");
        need(labels_.contains(uuid)||labels_.size()<256,"scratchpad capacity exceeded");
    }
    if(op=="toggle"){need(validLabel(label),"invalid or reserved scratchpad label");uuid=labels_.key(label);need(!uuid.isEmpty(),"scratchpad label not found");}
    QJsonObject window;if(op!="recover"){
        need(!QUuid(uuid).isNull()&&QUuid(uuid).toString(QUuid::WithoutBraces)==uuid,"invalid or noncanonical scratchpad window");window=read_(uuid);need(!window.isEmpty(),"scratchpad window no longer exists");
        need(!window["fullscreen"].toBool()&&!window["interacting"].toBool(),"scratchpad window is fullscreen or being moved/resized");
        if(op=="assign")need(window["candidate"].toBool()&&!window["hidden"].toBool(),"scratchpad assignment requires an eligible visible window");
        if(op=="clear")need(labels_.contains(uuid),"window has no scratchpad assignment");
    }
    // This rejection is before any write and may be retried after layout release.
    if(op=="toggle"||op=="clear")need(!busy_(uuid),"scratchpad layout release pending");
    if(op=="recover")for(auto it=labels_.begin();it!=labels_.end();++it)need(!busy_(it.key()),"scratchpad layout release pending");
    sequences_[owner]=sequence;
    QJsonObject result{{"id",id},{"window",uuid},{"state","applied"},{"completed",true},{"success",true},{"acknowledged",true},{"error",""}};
    bool applied=true;
    if(op=="assign")labels_[uuid]=label;
    else if(op=="recover"){
        QJsonArray failed;for(auto it=labels_.begin();it!=labels_.end();++it)if(!write_(it.key(),false,true))failed.append(it.value());
        result["failed-labels"]=failed;applied=failed.isEmpty();
    }else {
        const bool hide=op=="toggle"&&!window["hidden"].toBool()&&!window["minimized"].toBool()&&window["current"].toBool();
        applied=write_(uuid,hide,true);if(applied&&op=="clear")labels_.remove(uuid);
    }
    if(!applied){result["state"]="blocked";result["success"]=false;result["acknowledged"]=false;result["error"]="KDE rejected scratchpad visibility, context or focus; inspect the window before retrying";}
    if(op!="recover"){auto info=describe(uuid);for(auto it=info.begin();it!=info.end();++it)result[it.key()]=it.value();}
    if(jobs_.size()>=64)jobs_.remove(order_.takeFirst());
    order_.append(id);jobs_[id]={owner,digest,result};return result;
}
}
