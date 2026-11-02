#include "focus.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *error){if(!ok)throw std::invalid_argument(error);}
bool close(const QJsonValue &a,const QJsonValue &b) {
    if(!a.isObject()||!b.isObject())return false;
    auto first=a.toObject(),second=b.toObject();
    for(auto key:{"x","y","w","h"})if(!first[key].isDouble()||!second[key].isDouble()||!std::isfinite(first[key].toDouble())||!std::isfinite(second[key].toDouble())||std::abs(first[key].toDouble()-second[key].toDouble())>1)return false;
    return first["w"].toDouble()>0&&first["h"].toDouble()>0&&second["w"].toDouble()>0&&second["h"].toDouble()>0;
}
}
QJsonObject focusRequest(const QJsonObject &neighbor,const QJsonObject &plan,const QJsonObject &windows,const QString &id) {
    auto source=neighbor["source"].toObject();QMap<QString,QJsonObject> observed;
    for(auto v:windows["windows"].toArray()){auto w=v.toObject();observed[w["uuid"].toString()]=w;}
    need(close(source["frame"],observed[source["uuid"].toString()]["frame"])&&close(neighbor["frame"],observed[neighbor["uuid"].toString()]["frame"]),"source or target differs from the applied layout");
    return {{"protocol",1},{"id",id},{"bridge-instance",plan["bridge-instance"]},{"context",plan["context"]},{"source",source["uuid"]},{"target",neighbor["uuid"]},{"source-frame",source["frame"]},{"target-frame",neighbor["frame"]}};
}
void validateFocusRequest(const QJsonObject &r,const QJsonObject &snapshot) {
    need(r["protocol"]==QJsonValue(1)&&!QUuid(r["id"].toString()).isNull(),"invalid focus protocol or request ID");
    need(r["bridge-instance"]==snapshot["instance"],"stale focus bridge instance");
    const auto source=r["source"].toString(),target=r["target"].toString();need(!QUuid(source).isNull()&&!QUuid(target).isNull()&&source!=target,"invalid focus source or target");
    auto context=r["context"].toObject();QJsonObject output;
    for(auto v:snapshot["outputs"].toArray())if(v.toObject()["id"]==context["output-id"])output=v.toObject();
    need(!output.isEmpty()&&output["name"]==context["output"]&&output["current-desktop"]==context["desktop"]&&output["current-activity"]==context["activity"],"focus context changed");
    QMap<QString,QJsonObject> windows;int focused=0;
    for(auto v:snapshot["windows"].toArray()){auto w=v.toObject();windows[w["uuid"].toString()]=w;if(w["focused"].toBool())++focused;}
    need(windows.contains(source)&&windows.contains(target)&&focused==1&&windows[source]["focused"].toBool(),"focus source changed");
    for(auto uuid:{source,target}) {
        auto w=windows[uuid];need(w["candidate"]==QJsonValue(true)&&!w["minimized"].toBool()&&!w["fullscreen"].toBool()&&w["output"]==context["output"],"focus window is no longer eligible");
        auto ds=w["desktops"].toArray(),as=w["activities"].toArray();need((ds.isEmpty()||ds.contains(context["desktop"]))&&(as.isEmpty()||as.contains(context["activity"])),"focus window changed desktop or Activity");
        need(close(w["frame"],r[uuid==source?"source-frame":"target-frame"]),"focus window moved since selection");
    }
}
QJsonObject FocusRequests::submit(const QString &owner,const QJsonObject &r,const QJsonObject &snapshot,qint64 now) {
    need(!owner.isEmpty(),"focus requires a D-Bus owner");auto id=r["id"].toString();auto digest=QJsonDocument(r).toJson(QJsonDocument::Compact);
    if(jobs_.contains(id)){need(jobs_[id].owner==owner&&jobs_[id].digest==digest,"focus request ID reused");return status(owner,id);}
    bool ok=false;auto text=r["request-sequence"].toString();auto sequence=text.toULongLong(&ok);need(ok&&sequence>0&&QString::number(sequence)==text&&sequence>sequences_.value(owner),"invalid or expired focus request sequence");
    need(sequences_.contains(owner)||sequences_.size()<64,"focus owner capacity exceeded");
    for(const auto &job:jobs_)need(job.state!="pending","another focus request is pending");
    validateFocusRequest(r,snapshot);need(active_()==r["source"].toString(),"focus changed before activation");
    if(jobs_.size()>=32){jobs_.remove(order_.takeFirst());}
    Job job;job.owner=owner;job.source=r["source"].toString();job.target=r["target"].toString();job.digest=digest;job.since=now;jobs_[id]=job;order_.append(id);sequences_[owner]=sequence;
    if(!activate_(job.target)){jobs_[id].state="failed";jobs_[id].error="KWin rejected window activation";}
    return status(owner,id);
}
QJsonObject FocusRequests::status(const QString &owner,const QString &id) const {
    need(jobs_.contains(id)&&jobs_[id].owner==owner,"unknown focus request for this owner");auto job=jobs_[id];
    return {{"id",id},{"source",job.source},{"target",job.target},{"state",job.state},{"completed",job.state!="pending"},{"success",job.state=="focused"},{"acknowledged",job.state=="focused"},{"error",job.error}};
}
bool FocusRequests::tick(qint64 now) {
    bool changed=false;
    for(auto &job:jobs_)if(job.state=="pending") {
        auto active=active_();
        if(active==job.target){job.state="focused";changed=true;}
        else if(!active.isEmpty()&&active!=job.source){job.state="failed";job.error="focus changed to a different window";changed=true;}
        else if(now-job.since>=600){job.state="timed_out";job.error="target focus was not observed";changed=true;}
    }
    return changed;
}
void FocusRequests::release(const QString &owner) {
    sequences_.remove(owner);for(auto &job:jobs_)if(job.owner==owner&&job.state=="pending"){job.state="cancelled";job.error="focus owner disconnected";}
}
}
