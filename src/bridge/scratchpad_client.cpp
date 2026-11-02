#include "client.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QUuid>
#include <limits>
#include <stdexcept>
namespace kwabai {
QJsonObject BridgeClient::submitScratchpad(QJsonObject request){
    if(!state_.hasScratchpads()||!events_.status()["ready"].toBool())throw std::invalid_argument("scratchpads require the updated effect and ready events; run scripts/reload-effect.sh");
    for(const auto &job:scratchpadJobs_)if(!job["completed"].toBool())throw std::invalid_argument("another scratchpad request is pending");
    if(scratchpadSequence_==std::numeric_limits<quint64>::max())throw std::invalid_argument("scratchpad sequence exhausted");
    auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);request["id"]=id;request["protocol"]=1;request["bridge-instance"]=state_.windows()["instance"];request["request-sequence"]=QString::number(++scratchpadSequence_);
    if(scratchpadJobs_.size()>=64)scratchpadJobs_.remove(scratchpadOrder_.takeFirst());
    scratchpadOrder_.append(id);
    scratchpadJobs_[id]={{"id",id},{"kind","scratchpad"},{"window",request["window"]},{"state","submitting"},{"completed",false},{"success",false},{"acknowledged",false}};
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1","Scratchpad");message<<QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));auto epoch=epoch_;
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1500),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,id,epoch,watcher]{
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();auto &job=scratchpadJobs_[id];QJsonObject result;
        if(epoch==epoch_&&!reply.isError()&&reply.value().toUtf8().size()<=16384)result=QJsonDocument::fromJson(reply.value().toUtf8()).object();
        auto state=result["state"].toString();bool applied=state=="applied";
        bool valid=(applied||state=="blocked"||state=="rejected")&&result["completed"]==true&&result["success"]==applied&&result["acknowledged"]==applied&&(!applied||result["id"]==QJsonValue(id));
        if(valid)for(auto it=result.begin();it!=result.end();++it)job[it.key()]=it.value();
        else {job["state"]="unknown";job["completed"]=true;job["error"]=reply.isError()?reply.error().message():QString("scratchpad response unconfirmed or bridge changed");}
        refresh();Q_EMIT updated();
    });return scratchpadJobs_[id];
}
QJsonObject BridgeClient::scratchpadStatus(const QString &id) const {if(!scratchpadJobs_.contains(id))throw std::invalid_argument("unknown or expired scratchpad request");return scratchpadJobs_[id];}
}
