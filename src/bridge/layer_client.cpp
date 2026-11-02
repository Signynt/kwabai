#include "client.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <limits>
#include <stdexcept>
namespace kwabai {
QJsonObject BridgeClient::submitLayer(QJsonObject request) {
    if(!state_.hasLayers()||!events_.status()["ready"].toBool())throw std::invalid_argument("layer control requires the updated effect and ready events; run scripts/reload-effect.sh");
    for(const auto &job:layerJobs_)if(!job["completed"].toBool())throw std::invalid_argument("another layer request is pending");
    if(layerSequence_==std::numeric_limits<quint64>::max())throw std::invalid_argument("layer sequence exhausted");
    auto id=request["id"].toString();request["protocol"]=1;request["bridge-instance"]=state_.windows()["instance"];request["request-sequence"]=QString::number(++layerSequence_);
    if(layerJobs_.size()>=64)layerJobs_.remove(layerOrder_.takeFirst());
    layerOrder_.append(id);
    layerJobs_[id]={{"id",id},{"kind","layer"},{"confirmed-layer-id",request["window-id"]},{"window",request["window"]},{"state","submitting"},{"completed",false},{"success",false},{"acknowledged",false}};
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1","SetLayer");message<<QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));auto epoch=epoch_;
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,id,epoch,watcher]{
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();auto &job=layerJobs_[id];QJsonObject result;
        if(epoch==epoch_&&!reply.isError()&&reply.value().toUtf8().size()<=16384)result=QJsonDocument::fromJson(reply.value().toUtf8()).object();
        const auto state=result["state"].toString();const bool applied=state=="applied";
        const bool valid=(applied||state=="blocked"||state=="rejected")&&result["completed"]==true&&result["success"]==applied&&result["acknowledged"]==applied&&(!result.contains("id")||result["id"]==QJsonValue(id))&&(!applied||(result["window"]==job["window"]&&QStringList{"above","below","normal"}.contains(result["layer"].toString())));
        if(valid)for(auto it=result.begin();it!=result.end();++it)job[it.key()]=it.value();
        else {job["state"]="unknown";job["completed"]=true;job["error"]=reply.isError()?reply.error().message():QString("layer response unconfirmed or bridge changed");}
        refresh();Q_EMIT updated();
    });return layerJobs_[id];
}
QJsonObject BridgeClient::layerStatus(const QString &id) const {if(!layerJobs_.contains(id))throw std::invalid_argument("unknown or expired layer request");return layerJobs_[id];}
}
