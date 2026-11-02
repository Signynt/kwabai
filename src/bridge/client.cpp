#include "client.h"
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <stdexcept>
#include <limits>
#include <cmath>
#include <QJsonDocument>
namespace kwabai {
namespace {bool retired(const QString &s){return s=="forgotten"||s=="moved"||s=="superseded"||s=="restored"||s=="rolled_back"||s=="rejected";}}
BridgeClient::BridgeClient(QObject *parent):QObject(parent) {
    notificationTimer_.setSingleShot(true);notificationTimer_.setInterval(16);
    connect(&notificationTimer_,&QTimer::timeout,this,[this]{poll();pollGeometry();pollFocus();pollDesktops();});
    timer_.setInterval(500);connect(&timer_,&QTimer::timeout,this,[this]{poll();pollGeometry();pollFocus();pollDesktops();});
}
void BridgeClient::setDropPreview(QJsonObject request) {
    if(!state_.hasDropPreview())return;
    previewWanted_=std::move(request);sendDropPreview();
}
void BridgeClient::clearDropPreview(const QString &contextId) {
    if(previewWanted_.isEmpty()||(!contextId.isEmpty()&&previewWanted_["context-id"]!=QJsonValue(contextId)))return;
    previewWanted_=QJsonObject{{"context-id",previewWanted_.value("context-id")},{"clear",true}};sendDropPreview();
}
void BridgeClient::sendDropPreview() {
    if(previewPending_||previewWanted_.isEmpty()||!state_.hasDropPreview())return;
    if(previewWanted_==previewSent_&&previewClock_.isValid()&&previewClock_.elapsed()<200)return;
    auto desired=previewWanted_,request=desired;request["protocol"]=1;request["bridge-instance"]=state_.windows()["instance"];request["request-sequence"]=QString::number(++previewSequence_);
    previewSent_=desired;previewClock_.start();previewPending_=true;auto epoch=epoch_;
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1","DropPreview");message<<QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,300),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher,epoch,desired]{
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();previewPending_=false;
        if(epoch!=epoch_){previewWanted_=QJsonObject{};previewSent_=QJsonObject{};return;}
        previewStatus_=reply.isError()?QJsonObject{{"visible",false},{"error",reply.error().message()}}:QJsonDocument::fromJson(reply.value().toUtf8()).object();
        if(desired.value("clear").toBool()&&previewWanted_==desired)previewWanted_=QJsonObject{};
        if(previewWanted_!=desired)sendDropPreview();
    });
}
void BridgeClient::start() {
    if(timer_.isActive())return;
    state_.disconnect("waiting for KWin bridge");
    auto *watcher=new QDBusServiceWatcher("org.kde.KWin",QDBusConnection::sessionBus(),QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString &,const QString &,const QString &) {
        ++epoch_;pending_=false;failed("KWin service owner changed");poll();
    });
    QDBusConnection::sessionBus().connect("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1","Changed",this,SLOT(onChanged(QString)));
    timer_.start();poll();
}
void BridgeClient::onChanged(const QString &) {
    // Signals are only hints; state still comes from validated method replies.
    ++notifications_;refreshRequested_=true;
    if(!notificationTimer_.isActive())notificationTimer_.start();
}
quint64 BridgeClient::refresh(){
    // Capture before starting: callers need a snapshot whose request began
    // after this call, including when an older request is already in flight.
    auto revision=snapshotRequestRevision_;refreshRequested_=urgentRefreshRequested_=true;poll();return revision;
}
void BridgeClient::finishPoll() {
    snapshotRevision_=snapshotRequestRevision_;pending_=false;Q_EMIT updated();
    if(urgentRefreshRequested_)poll();
    else if(refreshRequested_&&!notificationTimer_.isActive())notificationTimer_.start();
}
void BridgeClient::poll() {
    if(pending_)return;
    if(!QDBusConnection::sessionBus().isConnected()){failed("session bus unavailable");return;}
    pending_=true;++snapshotRequestRevision_;refreshRequested_=urgentRefreshRequested_=false;auto epoch=epoch_;
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1","Snapshot");
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,epoch,watcher] {
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();
        if(epoch!=epoch_)return;
        if(reply.isError()){failed(reply.error().name()+": "+reply.error().message());return;}
        try {
            state_.accept(reply.value().toUtf8());
            if(!state_.hasEvents()){events_.clear();finishPoll();return;}
            auto instance=state_.windows().value("instance").toString();
            if(events_.instance()!=instance){events_.baseline(instance,state_.eventSequence());finishPoll();return;}
            fetchEvents(epoch);
        }
        catch(const std::exception &e){failed(QString::fromUtf8(e.what()));}
    });
}
void BridgeClient::failed(const QString &reason){if(state_.connected()||lastFailure_!=reason)qWarning().noquote()<<QString::fromUtf8(QJsonDocument(QJsonObject{{"event","bridge_unavailable"},{"error",reason}}).toJson(QJsonDocument::Compact));++failures_;lastFailure_=reason;pending_=false;state_.disconnect(reason);events_.clear();Q_EMIT updated();}
QJsonObject BridgeClient::eventStatus() const {auto status=events_.status();status["resyncs"]=QString::number(resyncs_);status["notifications"]=QString::number(notifications_);status["failures"]=QString::number(failures_);status["last-error"]=lastFailure_;return status;}
void BridgeClient::fetchEvents(quint64 epoch) {
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1","Events");
    message<<events_.instance()<<QString::number(events_.cursor());
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,epoch,watcher] {
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();if(epoch!=epoch_)return;
        if(reply.isError()){failed(reply.error().name()+": "+reply.error().message());return;}
        try {
            if(!events_.accept(reply.value().toUtf8())){++resyncs_;failed("event journal requires snapshot resync");return;}
            if(events_.cursor()<state_.eventSequence())throw std::invalid_argument("event journal does not cover snapshot cursor");
            finishPoll();
        }catch(const std::exception &e){failed(QString::fromUtf8(e.what()));}
    });
}
void BridgeClient::validateFocusReady() const {
    if(!state_.hasFocus()||!events_.status()["ready"].toBool())throw std::invalid_argument("focus bridge and ready events required; run scripts/reload-effect.sh for the focus capability");
    for(const auto &job:focusJobs_)if(!job["completed"].toBool())throw std::invalid_argument("another focus request is unresolved");
}
QJsonObject BridgeClient::submitFocus(QJsonObject request) {
    validateFocusReady();
    if(focusSequence_==std::numeric_limits<quint64>::max())throw std::invalid_argument("focus request sequence exhausted");
    auto id=request["id"].toString();if(focusJobs_.contains(id))throw std::invalid_argument("duplicate focus request ID");
    request["request-sequence"]=QString::number(++focusSequence_);
    if(focusJobs_.size()>=32){auto old=focusOrder_.takeFirst();focusJobs_.remove(old);}
    focusOrder_.append(id);focusJobs_[id]=QJsonObject{{"id",id},{"source",request["source"]},{"target",request["target"]},{"bridge-instance",request["bridge-instance"]},{"state","submitting"},{"completed",false},{"success",false},{"acknowledged",false}};
    focusCall("Focus",id,QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));return focusJobs_[id];
}
QJsonObject BridgeClient::focusStatus(const QString &id) const {
    if(!focusJobs_.contains(id))throw std::invalid_argument("unknown or expired focus request in this daemon lifetime");
    return focusJobs_[id];
}
void BridgeClient::focusCall(const QString &method,const QString &id,const QVariant &argument) {
    focusBusy_.insert(id);auto epoch=epoch_;
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1",method);message<<argument;
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,epoch,id,watcher]{
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();focusBusy_.remove(id);auto &job=focusJobs_[id];
        if(epoch!=epoch_){job["state"]="connection_lost";job["completed"]=true;job["success"]=false;job["acknowledged"]=false;return;}
        if(reply.isError()){job["state"]="unknown";job["error"]=reply.error().message();return;}
        auto bytes=reply.value().toUtf8();auto status=bytes.size()<=16384?QJsonDocument::fromJson(bytes).object():QJsonObject{};auto state=status["state"].toString();
        const bool validState=state=="pending"||state=="focused"||state=="failed"||state=="timed_out"||state=="cancelled"||state=="rejected";
        if(!validState||!status["completed"].isBool()||status["completed"].toBool()!=(state!="pending")||!status["success"].isBool()||status["success"].toBool()!=(state=="focused")||!status["acknowledged"].isBool()||status["acknowledged"].toBool()!=(state=="focused")||(status.contains("id")&&status["id"]!=QJsonValue(id))||(state!="rejected"&&(status["source"]!=job["source"]||status["target"]!=job["target"]))) {
            job["state"]="unknown";job["error"]="invalid or unconfirmed focus response";return;
        }
        for(auto key:{"state","completed","success","acknowledged","error"})job[key]=status[key];
        if(state=="focused"){refreshRequested_=true;if(!notificationTimer_.isActive())notificationTimer_.start();}
    });
}
void BridgeClient::pollFocus() {
    if(!state_.connected())return;
    for(auto it=focusJobs_.begin();it!=focusJobs_.end();++it)if(!it.value()["completed"].toBool()&&!focusBusy_.contains(it.key())) {
        if(it.value()["bridge-instance"]!=state_.windows()["instance"]){it.value()["state"]="bridge_changed";it.value()["completed"]=true;it.value()["success"]=false;it.value()["acknowledged"]=false;continue;}
        focusCall("FocusStatus",it.key(),it.key());
    }
}
QJsonObject BridgeClient::latestMembershipMove(bool activity) const {
    for(auto it=desktopOrder_.crbegin();it!=desktopOrder_.crend();++it)if(desktopJobs_[*it]["kind"]==QJsonValue(activity?"activity-move":"desktop-move"))return desktopJobs_[*it];
    return {};
}
QJsonObject BridgeClient::submitDesktopMove(QJsonObject request,bool activity) {
    if(!(activity?state_.hasActivityMove():state_.hasDesktopMove())||!events_.status()["ready"].toBool())throw std::invalid_argument(activity?"activity-move and ready events required; run scripts/reload-effect.sh":"desktop-move and ready events required; run scripts/reload-effect.sh");
    if(geometryBusy())throw std::invalid_argument("another geometry or desktop operation is unresolved");
    if(desktopSequence_==std::numeric_limits<quint64>::max())throw std::invalid_argument("desktop request sequence exhausted");
    auto id=request.value("id").toString();if(desktopJobs_.contains(id))throw std::invalid_argument("duplicate desktop request ID");
    request["request-sequence"]=QString::number(++desktopSequence_);
    if(desktopJobs_.size()>=32)desktopJobs_.remove(desktopOrder_.takeFirst());
    desktopOrder_.append(id);desktopJobs_[id]={{"id",id},{"window",request.value("window")},{activity?"activity":"desktop",request.value(activity?"activity":"desktop")},{"bridge-instance",request.value("bridge-instance")},{"kind",activity?"activity-move":"desktop-move"},{"state","submitting"},{"completed",false},{"success",false},{"acknowledged",false}};
    desktopCall(activity?"MoveActivity":"MoveDesktop",id,QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));return desktopJobs_[id];
}
QJsonObject BridgeClient::desktopTransaction(const QString &id) const {if(!desktopJobs_.contains(id))throw std::invalid_argument("unknown or expired desktop movement in this daemon lifetime");return desktopJobs_[id];}
void BridgeClient::desktopCall(const QString &method,const QString &id,const QVariant &argument) {
    desktopBusy_.insert(id);auto epoch=epoch_;auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1",method);message<<argument;
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,epoch,id,watcher]{
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();desktopBusy_.remove(id);auto &job=desktopJobs_[id];
        if(epoch!=epoch_){job["state"]="connection_lost";job["completed"]=true;job["success"]=false;job["acknowledged"]=false;return;}
        if(reply.isError()){job["state"]="unknown";job["error"]=reply.error().message();return;}
        auto bytes=reply.value().toUtf8();auto status=bytes.size()<=16384?QJsonDocument::fromJson(bytes).object():QJsonObject{};auto state=status.value("state").toString();
        const bool terminal=state=="moved"||state=="rolled_back"||state=="failed"||state=="restore_failed"||state=="rejected";
        const bool valid=terminal||state=="pending"||state=="rolling_back";
        if(!valid||!status["completed"].isBool()||status["completed"].toBool()!=terminal||!status["success"].isBool()||status["success"].toBool()!=(state=="moved")||!status["acknowledged"].isBool()||status["acknowledged"].toBool()!=(state=="moved")||(status.contains("id")&&status["id"]!=QJsonValue(id))||(state!="rejected"&&(status["window"]!=job["window"]||status[job["kind"]=="activity-move"?"activity":"desktop"]!=job[job["kind"]=="activity-move"?"activity":"desktop"]))) {job["state"]="unknown";job["error"]="invalid or unconfirmed desktop movement response";return;}
        for(auto key:{"state","completed","success","acknowledged","error"})job[key]=status[key];
        if(state=="moved")refresh();
        Q_EMIT updated();
    });
}
void BridgeClient::pollDesktops() {
    if(!state_.connected())return;
    for(auto it=desktopJobs_.begin();it!=desktopJobs_.end();++it)if(!it.value()["completed"].toBool()&&!desktopBusy_.contains(it.key())) {
        if(it.value()["bridge-instance"]!=state_.windows()["instance"]){it.value()["state"]="bridge_changed";it.value()["completed"]=true;it.value()["success"]=false;it.value()["acknowledged"]=false;continue;}
        desktopCall(it.value()["kind"]=="activity-move"?"ActivityTransaction":"DesktopTransaction",it.key(),it.key());
    }
}
bool BridgeClient::geometryBusy() const {
    if(state_.hasMouse())for(auto v:state_.windows()["windows"].toArray())if(v.toObject().value("interaction").toObject()["state"]=="active")return true;
    for(const auto &job:desktopJobs_)if(!job["completed"].toBool())return true;
    for(const auto &job:jobs_)if(!job["completed"].toBool()&&job["state"]!="superseding")return true;
    return false;
}
QJsonObject BridgeClient::submitGeometry(QJsonObject request) {
    if(geometryBusy())throw std::invalid_argument("another geometry operation is unresolved; wait for confirmation");
    if(!state_.hasGeometry()||!events_.status()["ready"].toBool())throw std::invalid_argument("geometry bridge and event baseline required");
    if(!state_.hasNoRestore())throw std::invalid_argument("loaded effect retains incompatible restore behavior; stop the daemon and run scripts/reload-effect.sh");
    if(request.contains("interaction")&&!state_.hasMouse())throw std::invalid_argument("loaded effect lacks mouse interactions; run scripts/reload-effect.sh");
    if(request.value("release-mode")=="hidden"&&!state_.hasHiddenRelease())throw std::invalid_argument("loaded effect lacks hidden-window release; run scripts/reload-effect.sh");
    if(request.value("release-mode")=="keep-frame"&&!state_.hasMouseOutputSwap())throw std::invalid_argument("loaded effect lacks cross-output swaps; run scripts/reload-effect.sh");
    if(request.contains("zoom-frame")&&!state_.hasGeometryZoom())throw std::invalid_argument("zoom-fullscreen requires the updated effect; run scripts/reload-effect.sh");
    if(request.contains("floating-frame")&&!state_.hasFloatingGrid())throw std::invalid_argument("floating grids require the updated effect; run scripts/reload-effect.sh");
    if(request.contains("move-output")&&!state_.hasOutputMove())throw std::invalid_argument("loaded effect lacks output-move; run scripts/reload-effect.sh");
    if(request.contains("release")&&!state_.hasGeometryRelease())throw std::invalid_argument("loaded effect lacks geometry-release; run scripts/reload-effect.sh");
    if(request.contains("lifecycle")&&!state_.hasGeometryLifecycle())throw std::invalid_argument("loaded effect lacks geometry-lifecycle; run scripts/reload-effect.sh");
    if(request["placements"].toArray().size()>state_.geometryLimit())throw std::invalid_argument("selected window count exceeds loaded effect limit; run scripts/reload-effect.sh for larger contexts");
    if(request.contains("supersedes")) {
        if(!state_.hasGeometryUpdate())throw std::invalid_argument("loaded effect lacks geometry-update; run scripts/reload-effect.sh");
        auto prior=request["supersedes"].toString();auto old=transaction(prior);
        if(busy_.contains(prior)||old["state"]!="applied"||!old["success"].toBool()||old["bridge-instance"]!=request["bridge-instance"])throw std::invalid_argument("context update requires a confirmed applied transaction from this bridge");
        if(old["context-id"]!=request["context-id"]||request["context-generation"].toString().toULongLong()<=old["context-generation"].toString().toULongLong())throw std::invalid_argument("context update requires the same context and a newer generation");
    }
    auto id=request["id"].toString();
    if(jobs_.contains(id))throw std::invalid_argument("duplicate local transaction ID");
    if(state_.hasGeometryRetention()) {
        if(requestSequence_==std::numeric_limits<quint64>::max())throw std::invalid_argument("geometry request sequence exhausted");
        request["request-sequence"]=QString::number(++requestSequence_);
    }
    makeRoom();jobOrder_.append(id);
    jobs_[id]=QJsonObject{{"id",id},{"state","submitting"},{"completed",false},{"success",false},{"bridge-instance",request["bridge-instance"]}};
    // Non-const operator[] inserts a null value when this optional flag is absent.
    // Keep ordinary Apply requests free of move-output metadata on the wire.
    jobs_[id]["move-output"]=request.value("move-output")==QJsonValue(true);
    jobs_[id]["floating-frame"]=request.value("floating-frame")==QJsonValue(true);
    QJsonArray targets;for(auto value:request["placements"].toArray()){auto target=value.toObject();target.remove("expected-frame");targets.append(target);}
    jobs_[id]["target-placements"]=targets;jobs_[id]["requested-placements"]=targets;jobs_[id]["reconcile"]=request.value("reconcile").toBool();jobs_[id]["target-work-area"]=request["work-area"];
    QJsonArray managed;for(auto v:request["placements"].toArray())managed.append(v.toObject()["uuid"]);jobs_[id]["managed-windows"]=managed;
    jobs_[id]["lineage"]=request.contains("supersedes")?jobs_[request["supersedes"].toString()]["lineage"]:QJsonValue(id);
    if(request.contains("context-generation")){jobs_[id]["context-generation"]=request["context-generation"];jobs_[id]["context-id"]=request["context-id"];}
    if(request.contains("supersedes")){auto prior=request["supersedes"].toString();jobs_[id]["previous-id"]=prior;jobs_[prior]["state"]="superseding";jobs_[prior]["active-transaction"]=id;jobs_[prior]["acknowledged"]=false;jobs_[prior]["success"]=false;jobs_[prior]["completed"]=false;jobs_[prior]["replacement-id"]=id;}
    if(request.contains("context-id"))latestContextJobs_[request["context-id"].toString()]=id;
    geometryCall("Apply",id,QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));return jobs_[id];
}
QJsonObject BridgeClient::transaction(const QString &id) const {
    if(!jobs_.contains(id))throw std::invalid_argument("unknown transaction in this daemon lifetime");
    auto result=jobs_[id];
    QString active;auto lineage=result["lineage"];
    for(auto it=jobs_.cbegin();it!=jobs_.cend();++it) {
        if(it.value()["lineage"]!=lineage||retired(it.value()["state"].toString()))continue;
        if(it.value()["state"]=="superseding") {
            auto replacement=jobs_.value(it.value()["replacement-id"].toString());
            if(replacement["state"]!="rejected"&&replacement["state"]!="rolled_back")continue;
        }
        active=it.key();break;
    }
    result["active-transaction"]=active;
    return result;
}
void BridgeClient::makeRoom() {
    if(jobs_.size()<32)return;
    if(!state_.hasGeometryRetention())throw std::invalid_argument("loaded effect lacks geometry-retention; stop the daemon and run scripts/reload-effect.sh");
    for(auto id:jobOrder_)if(!busy_.contains(id)&&jobs_[id]["completed"].toBool()&&retired(jobs_[id]["state"].toString())) {
        jobs_.remove(id);jobOrder_.removeOne(id);
        for(auto *mapping:{&latestContextJobs_})for(auto it=mapping->begin();it!=mapping->end();)if(it.value()==id)it=mapping->erase(it);else ++it;
        return;
    }
    throw std::invalid_argument("geometry capacity exhausted by active or unresolved transactions; reset unused contexts");
}
QJsonObject BridgeClient::retentionStatus() const {return {{"enabled",state_.hasGeometryRetention()},{"retained",jobs_.size()},{"limit",32},{"request-sequence",QString::number(requestSequence_)}};}
QSet<QString> BridgeClient::controlledWindows() const {
    QSet<QString> result;
    for(const auto &job:jobs_)if(job["bridge-instance"]==state_.windows()["instance"]&&!retired(job["state"].toString())&&job["state"]!="bridge_changed"&&job["state"]!="connection_lost")for(auto uuid:job["managed-windows"].toArray())result.insert(uuid.toString());
    return result;
}
QJsonObject BridgeClient::contextStatus(const QString &contextId) const {
    QSet<QString> activeIds;
    for(auto it=jobs_.begin();it!=jobs_.end();++it)if(it.value()["context-id"]==QJsonValue(contextId)) {
        auto active=transaction(it.key())["active-transaction"].toString();
        if(!active.isEmpty())activeIds.insert(active);
    }
    QJsonObject result{{"context-id",contextId},{"active-count",activeIds.size()},{"active",QJsonValue(QJsonValue::Null)},{"latest",QJsonValue(QJsonValue::Null)}};
    if(activeIds.size()==1)result["active"]=transaction(*activeIds.begin());
    if(latestContextJobs_.contains(contextId))result["latest"]=transaction(latestContextJobs_[contextId]);
    return result;
}
QJsonObject BridgeClient::forgetGeometry(const QString &id) {
    if(!state_.hasGeometryForget())throw std::invalid_argument("context reset with native ownership requires geometry-forget; run scripts/reload-effect.sh");
    if(geometryBusy())throw std::invalid_argument("wait for pending geometry before resetting");
    auto job=transaction(id);
    if(job["bridge-instance"]!=state_.windows()["instance"])throw std::invalid_argument("bridge changed; restart the observation daemon before resetting");
    jobs_[id].remove("forget-error");jobs_[id]["forget-requested"]=true;jobs_[id]["state"]="submitting_forget";jobs_[id]["completed"]=false;jobs_[id]["success"]=false;jobs_[id]["acknowledged"]=false;geometryCall("Forget",id,id);return jobs_[id];
}
void BridgeClient::geometryCall(const QString &method,const QString &id,const QVariant &argument) {
    busy_.insert(id);auto epoch=epoch_;
    auto message=QDBusMessage::createMethodCall("org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1",method);message<<argument;
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,1000),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,id,epoch,watcher,method] {
        QDBusPendingReply<QString> reply=*watcher;watcher->deleteLater();busy_.remove(id);
        if(epoch!=epoch_){jobs_[id]["state"]="connection_lost";jobs_[id]["completed"]=true;jobs_[id]["success"]=false;return;}
        if(reply.isError()){jobs_[id]["state"]="unknown";jobs_[id]["error"]=reply.error().message();return;}
        auto bytes=reply.value().toUtf8();if(bytes.size()>1024*1024){jobs_[id]["state"]="unknown";jobs_[id]["error"]="oversized transaction response";return;}auto doc=QJsonDocument::fromJson(bytes);auto status=doc.object();
        if(!doc.isObject()||!status["state"].isString()||!status["completed"].isBool()||!status["success"].isBool()||(status.contains("id")&&status["id"]!=QJsonValue(id))){jobs_[id]["state"]="unknown";jobs_[id]["error"]="invalid transaction response";return;}
        if(status["success"].toBool()&&(!status["completed"].toBool()||!status["acknowledged"].toBool()||(status["state"]!="applied"&&status["state"]!="restored"&&status["state"]!="moved"&&status["state"]!="forgotten"))){jobs_[id]["state"]="unknown";jobs_[id]["error"]="unconfirmed geometry success response";return;}
        // Forget can race a compositor operation not yet observed by this client.
        // A rejected release did not change the native transaction. Read it back
        // instead of replacing a still-owned applied transaction with rejection.
        if(method=="Forget"&&status["state"]=="rejected") {
            jobs_[id]["forget-error"]=status["error"];
            jobs_[id]["forget-requested"]=false;
            jobs_[id]["state"]="checking_forget";jobs_[id]["completed"]=false;
            Q_EMIT updated();return;
        }
        if(jobs_[id].contains("forget-error"))status["forget-error"]=jobs_[id]["forget-error"];
        if(jobs_[id].contains("previous-id")&&!status.contains("previous-id")){status["previous-id"]=jobs_[id]["previous-id"];if(status["state"]=="rejected")status["active-transaction"]=jobs_[id]["previous-id"];}
        if(status["success"].toBool()&&((status["state"]=="moved"&&!jobs_[id]["move-output"].toBool()&&!jobs_[id]["floating-frame"].toBool())||(status["state"]=="applied"&&(jobs_[id]["move-output"].toBool()||jobs_[id]["floating-frame"].toBool())))){jobs_[id]["state"]="unknown";jobs_[id]["error"]="geometry success has the wrong operation type";return;}
        if(status["state"]=="forgotten"&&!jobs_[id]["forget-requested"].toBool()){jobs_[id]["state"]="unknown";jobs_[id]["error"]="unsolicited ownership release";return;}
        status["forget-requested"]=jobs_[id]["forget-requested"];
        status["move-output"]=jobs_[id]["move-output"];status["floating-frame"]=jobs_[id]["floating-frame"];
        if(status["state"]=="applied"&&jobs_[id]["reconcile"].toBool()){
            auto accepted=status["accepted-placements"].toArray();QJsonArray actual;QSet<QString> seen;bool valid=status["reconciled"].toBool()&&status["accepted-placements"].isArray()&&accepted.size()<=20;
            for(auto value:accepted){auto a=value.toObject();QJsonObject p;for(auto v:jobs_[id]["requested-placements"].toArray())if(v.toObject()["uuid"]==a["uuid"])p=v.toObject();auto f=a["frame"].toObject();valid=valid&&!p.isEmpty()&&!seen.contains(a["uuid"].toString());for(auto key:{"x","y","w","h"})valid=valid&&f[key].isDouble()&&std::isfinite(f[key].toDouble());valid=valid&&f["w"].toDouble()>0&&f["h"].toDouble()>0;seen.insert(a["uuid"].toString());p["frame"]=f;actual.append(p);}
            if(!valid){jobs_[id]["state"]="unknown";jobs_[id]["error"]="invalid reconciled geometry response";return;}
            jobs_[id]["target-placements"]=actual;
        }
        status["requested-placements"]=jobs_[id]["requested-placements"];status["reconcile"]=jobs_[id]["reconcile"];
        status["target-placements"]=jobs_[id]["target-placements"];status["target-work-area"]=jobs_[id]["target-work-area"];
        status["lineage"]=jobs_[id]["lineage"];
        if(!status.contains("managed-windows"))status["managed-windows"]=jobs_[id]["managed-windows"];
        if(jobs_[id].contains("context-generation")){status["context-generation"]=jobs_[id]["context-generation"];status["context-id"]=jobs_[id]["context-id"];}
        status["id"]=id;status["bridge-instance"]=jobs_[id]["bridge-instance"];jobs_[id]=status;
        if(status["state"]=="moved"){refreshRequested_=true;if(!notificationTimer_.isActive())notificationTimer_.start();}
        // A confirmed child outcome also settles its predecessor; do not wait for an extra poll.
        for(auto it=jobs_.begin();it!=jobs_.end();++it) {
            auto child=jobs_.value(it.value()["replacement-id"].toString());auto childState=child["state"].toString();
            if(childState=="applied"||childState=="superseding"||childState=="superseded"||childState=="restoring"||childState=="restored"||childState=="forgotten") {
                it.value()["state"]="superseded";it.value()["completed"]=true;it.value()["success"]=false;it.value()["acknowledged"]=false;
            } else if(childState=="rejected"||childState=="rolled_back") {
                it.value()["state"]="applied";it.value()["completed"]=true;it.value()["success"]=true;it.value()["acknowledged"]=true;it.value()["replacement-id"]="";
            }
        }
        Q_EMIT updated();
    });
}
void BridgeClient::pollGeometry() {
    if(!state_.connected())return;
    for(auto it=jobs_.begin();it!=jobs_.end();++it) {
        if(it.value()["completed"].toBool()||busy_.contains(it.key()))continue;
        if(it.value()["bridge-instance"]!=state_.windows()["instance"]){it.value()["state"]="bridge_changed";it.value()["completed"]=true;it.value()["success"]=false;continue;}
        geometryCall("Transaction",it.key(),it.key());
    }
}

}
