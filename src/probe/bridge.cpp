#include "bridge.h"
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <QPointer>
#include <workspace.h>
#include <virtualdesktops.h>
#include <core/output.h>
#include <config-kwin.h>
#if KWIN_BUILD_ACTIVITIES
#include <activities.h>
#include <PlasmaActivities/Info>
#endif
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QDebug>
#include <limits>
#include <stdexcept>
#include <algorithm>
namespace {
QJsonObject rect(const KWin::RectF &r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
}
KwabaiReadOnlyBridge::KwabaiReadOnlyBridge(QObject *parent,bool previewSupported):QObject(parent),instance_(QUuid::createUuid().toString(QUuid::WithoutBraces)),journal_(instance_),geometry_(
    [](const QString &id)->std::optional<QRectF>{for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&!w->isDeleted()&&!w->isInternal()&&w->internalId()==QUuid(id))return QRectF(w->frameGeometry());}return {};},
    [](const QString &id,const QRectF &r){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&w->internalId()==QUuid(id)&&!w->isDeleted()&&!w->isInternal()&&!w->isOutline()&&w->isNormalWindow()&&!w->isSpecialWindow()&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity()&&!w->isMinimized()&&!w->isFullScreen()&&!w->isInteractiveMove()&&!w->isInteractiveResize()&&w->maximizeMode()==KWin::MaximizeRestore&&!w->isInteractiveMove()&&!w->isInteractiveResize()){bool reachable=false;for(auto *output:KWin::workspace()->outputs()){auto intersection=r.intersected(QRectF(output->geometryF()));if(intersection.width()>=std::min(50.0,r.width())&&intersection.height()>=std::min(50.0,r.height()))reachable=true;}if(!reachable)return false;w->moveResize(KWin::RectF(r));return true;}}return false;},
    [](const QString &id){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&!w->isDeleted()&&w->internalId()==QUuid(id))return w->output()?w->output()->name():QString{};}return QString{};},
    [](const QString &id){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&w->internalId()==QUuid(id)&&!w->isDeleted()&&!w->isInternal()&&w->isNormalWindow()&&!w->isSpecialWindow()&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity()&&!w->isMinimized()&&!w->isFullScreen()){KWin::workspace()->raiseWindow(w,true);return;}}}),focus_(
    []{auto *w=KWin::workspace()->activeWindow();return w?w->internalId().toString(QUuid::WithoutBraces):QString{};},
    [](const QString &id){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&w->internalId()==QUuid(id)&&!w->isDeleted()&&!w->isInternal()&&!w->isSpecialWindow()&&w->isNormalWindow()&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity()&&!w->isMinimized()&&!w->isFullScreen()&&!w->isInteractiveMove()&&!w->isInteractiveResize()&&w->wantsInput()){KWin::workspace()->activateWindow(w,true);return true;}}return false;}),layers_(
    [](const QString &id){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&!w->isDeleted()&&!w->isInternal()&&w->isNormalWindow()&&!w->isSpecialWindow()&&w->internalId()==QUuid(id))return QString(w->keepAbove()?"above":w->keepBelow()?"below":"normal");}return QString{};},
    [](const QString &id,const QString &layer){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(!w||w->internalId()!=QUuid(id)||w->isDeleted()||w->isInternal()||!w->isNormalWindow()||w->isSpecialWindow()||w->isInteractiveMove()||w->isInteractiveResize())continue;
        QPointer<KWin::Window> target=w;
        return kwabai::setLayerFlags(layer,
            [target]()->std::optional<std::pair<bool,bool>>{if(!target||target->isDeleted())return {};return std::pair{target->keepAbove(),target->keepBelow()};},
            [target](bool value){if(target&&!target->isDeleted())target->setKeepAbove(value);},
            [target](bool value){if(target&&!target->isDeleted())target->setKeepBelow(value);});}return false;},
    [this](const QString &id){return geometry_.tiles(id);},
    [this](const QString &id){return !scratchpads_.describe(id)["scratchpad"].toString().isEmpty()?2:geometry_.zoomed(id)?1:0;},
    []{QStringList result;for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&!w->isDeleted()&&!w->isInternal()&&w->isNormalWindow()&&!w->isSpecialWindow()&&!w->isHidden()&&!w->isMinimized()&&!w->isFullScreen()&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity())result.append(w->internalId().toString(QUuid::WithoutBraces));}return result;},
    [](const QString &id,const QString &reference){QPointer<KWin::Window> target,anchor;for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(!w||w->isDeleted()||w->isInternal())continue;if(w->internalId()==QUuid(id))target=w;if(w->internalId()==QUuid(reference))anchor=w;}if(!target||!anchor)return false;KWin::workspace()->stackAbove(target,anchor);return true;}),scratchpads_(
    [this](const QString &uuid){for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&!w->isDeleted()&&!w->isInternal()&&w->isNormalWindow()&&!w->isSpecialWindow()&&w->internalId()==QUuid(uuid))return QJsonObject{{"hidden",w->isHidden()||(scratchVisibility_.value(uuid).value("minimized-hide").toBool()&&w->isMinimized())},{"minimized",w->isMinimized()},{"fullscreen",w->isFullScreen()},{"interacting",w->isInteractiveMove()||w->isInteractiveResize()},{"current",w->output()==KWin::workspace()->activeOutput()&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity()},{"candidate",w->isMovable()&&w->isResizable()&&!w->isMinimized()&&w->maximizeMode()==KWin::MaximizeRestore}};}return QJsonObject{};},
    [this](const QString &uuid,bool hide,bool summon){for(auto *ew:KWin::effects->stackingOrder()){
        QPointer<KWin::Window> w=ew->window();if(!w||w->isDeleted()||w->internalId()!=QUuid(uuid))continue;
        if((hide||summon)&&(w->isFullScreen()||w->isInteractiveMove()||w->isInteractiveResize()))return false;
        if(hide){
            if(!scratchVisibility_.contains(uuid))scratchVisibility_[uuid]={{"pager",w->skipPager()},{"switcher",w->skipSwitcher()}};
            // Keep taskbar membership stable: toggling SkipTaskbar makes Plasma
            // remove/recreate the task and rematch it to its pinned launcher.
            w->setSkipPager(true);if(!w||w->isDeleted())return false;
            w->setSkipSwitcher(true);if(!w||w->isDeleted())return false;
#if KWIN_BUILD_X11
            // X11Window::updateVisibility forces SkipTaskbar when hidden.
            // Minimize this backend instead, retaining its taskbar identity.
            if(ew->isX11Client()){
                scratchVisibility_[uuid]["minimized-hide"]=true;
                w->setHidden(false);if(!w||w->isDeleted())return false;
                w->setMinimized(true);return w&&!w->isDeleted()&&w->isMinimized();
            }
#endif
            w->setHidden(true);return w&&!w->isDeleted()&&w->isHidden();
        }
        auto *output=KWin::workspace()->activeOutput();
        auto *desktop=output?KWin::effects->currentDesktop(output):nullptr;
        if(summon&&!desktop)return false;
        // Unhide first: even a later KDE rule refusal leaves the window reachable.
        w->setHidden(false);if(!w||w->isDeleted())return false;
        if(scratchVisibility_.contains(uuid)){
            auto flags=scratchVisibility_.take(uuid);
            if(flags["minimized-hide"].toBool()){w->setMinimized(false);if(!w||w->isDeleted())return false;}
            w->setSkipPager(flags["pager"].toBool());if(!w||w->isDeleted())return false;
            w->setSkipSwitcher(flags["switcher"].toBool());if(!w||w->isDeleted())return false;
        }
        if(!summon)return !w->isHidden();
        if(!w->isOnCurrentDesktop())KWin::workspace()->sendWindowToDesktops(w,{desktop},true);
        if(!w||w->isDeleted())return false;
        const auto activity=KWin::effects->currentActivity();if(!activity.isEmpty()&&!w->isOnCurrentActivity())w->setOnActivities({activity});if(!w||w->isDeleted())return false;
        w->sendToOutput(output);if(!w||w->isDeleted())return false;
        w->setMinimized(false);if(!w||w->isDeleted())return false;
        KWin::workspace()->activateWindow(w,true);
        return w&&!w->isDeleted()&&!w->isHidden()&&!w->isMinimized()&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity()&&w->isActive();
    }return false;},
    [this](const QString &uuid){return geometry_.pending()||geometry_.owns(uuid)||desktopMoves_.pending()||activityMoves_.pending();}),desktopMoves_(
    [this](const QString &uuid){for(auto v:QJsonDocument::fromJson(Snapshot().toUtf8()).object()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject();return QJsonObject{};},
    [](const QString &uuid,const QJsonArray &ids){QList<KWin::VirtualDesktop *> desktops;for(auto id:ids){KWin::VirtualDesktop *found=nullptr;for(auto *d:KWin::VirtualDesktopManager::self()->desktops())if(d->id()==id.toString())found=d;if(!found)return false;desktops.append(found);}
        for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&w->internalId()==QUuid(uuid)&&!w->isDeleted()&&!w->isInternal()&&!w->isOutline()&&w->isNormalWindow()&&!w->isSpecialWindow()&&!w->isMinimized()&&!w->isFullScreen()&&!w->isInteractiveMove()&&!w->isInteractiveResize()&&w->maximizeMode()==KWin::MaximizeRestore){KWin::workspace()->sendWindowToDesktops(w,desktops,true);return true;}}return false;},
    [this](const QString &uuid){return !geometry_.pending()&&!geometry_.owns(uuid)&&!activityMoves_.pending();}),activityMoves_(
    [this](const QString &uuid){for(auto v:QJsonDocument::fromJson(Snapshot().toUtf8()).object()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject();return QJsonObject{};},
    [](const QString &uuid,const QJsonArray &ids){
#if KWIN_BUILD_ACTIVITIES
        auto *activities=KWin::workspace()->activities();if(!activities)return false;QStringList target;for(auto id:ids){if(!activities->all().contains(id.toString()))return false;target.append(id.toString());}
        for(auto *ew:KWin::effects->stackingOrder()){auto *w=ew->window();if(w&&w->internalId()==QUuid(uuid)&&!w->isDeleted()&&!w->isInternal()&&!w->isOutline()&&w->isNormalWindow()&&!w->isSpecialWindow()&&!w->isMinimized()&&!w->isFullScreen()&&!w->isInteractiveMove()&&!w->isInteractiveResize()&&w->maximizeMode()==KWin::MaximizeRestore){w->setOnActivities(target);return true;}}
#else
        Q_UNUSED(uuid);Q_UNUSED(ids);
#endif
        return false;},
    [this](const QString &uuid){return !geometry_.pending()&&!geometry_.owns(uuid)&&!desktopMoves_.pending();},true) {
    previewSupported_=previewSupported;geometryClock_.start();geometryTimer_.setTimerType(Qt::PreciseTimer);geometryTimer_.setInterval(geometry_.pollInterval());
    connect(&geometryTimer_,&QTimer::timeout,this,[this]{auto now=geometryClock_.elapsed();scratchpads_.prune();bool changed=geometry_.tick(now);changed=focus_.tick(now)||changed;changed=layers_.tick()||changed;changed=desktopMoves_.tick(now)||changed;changed=activityMoves_.tick(now)||changed;publishPreview();updateGeometryInterval();if(changed)Q_EMIT Changed(instance_);});geometryTimer_.start();
    auto *owners=new QDBusServiceWatcher(this);
    owners->setConnection(QDBusConnection::sessionBus());owners->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    setProperty("geometry-owner-watcher",QVariant::fromValue<QObject *>(owners));
    connect(owners,&QDBusServiceWatcher::serviceUnregistered,this,[this,owners](const QString &owner){scratchpads_.release(owner);geometry_.release(owner,geometryClock_.elapsed());updateGeometryInterval();focus_.release(owner);layers_.release(owner);desktopMoves_.release(owner,geometryClock_.elapsed());activityMoves_.release(owner,geometryClock_.elapsed());publishPreview();owners->removeWatchedService(owner);});
    exported_=QDBusConnection::sessionBus().registerObject("/org/kwabai/Bridge",this,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals);
    if(!exported_)qWarning()<<"kwabai: read-only bridge export failed";
    refresh_.setSingleShot(true);refresh_.setInterval(34);
    connect(&refresh_,&QTimer::timeout,this,[this]{Snapshot();Q_EMIT Changed(instance_);});
    for(auto *w:KWin::effects->stackingOrder())observe(w);
    connect(KWin::effects,&KWin::EffectsHandler::windowAdded,this,[this](KWin::EffectWindow *w){observe(w);if(!w->window()||w->window()->isInternal()||w->window()->isOutline()||!w->window()->isNormalWindow()||w->window()->isSpecialWindow())return;journal_.append("window_added",{{"uuid",w->window()->internalId().toString(QUuid::WithoutBraces)}});refresh_.start();});
    connect(KWin::effects,&KWin::EffectsHandler::windowClosed,this,[this](KWin::EffectWindow *w){if(!w->window()||w->window()->isInternal()||w->window()->isOutline()||!w->window()->isNormalWindow()||w->window()->isSpecialWindow())return;scratchVisibility_.remove(w->window()->internalId().toString(QUuid::WithoutBraces));journal_.append("window_closed",{{"uuid",w->window()->internalId().toString(QUuid::WithoutBraces)}});refresh_.start();});
    connect(KWin::effects,&KWin::EffectsHandler::windowActivated,this,[this](KWin::EffectWindow *w){if(w&&w->window()&&!w->window()->isInternal()){journal_.append("window_focus",{{"uuid",w->window()->internalId().toString(QUuid::WithoutBraces)}});refresh_.start();}});
    connect(KWin::effects,&KWin::EffectsHandler::desktopChanged,this,[this]{journal_.append("context_changed");refresh_.start();});
    connect(KWin::effects,&KWin::EffectsHandler::currentActivityChanged,this,[this]{journal_.append("context_changed");refresh_.start();});
    Snapshot();
}
KwabaiReadOnlyBridge::~KwabaiReadOnlyBridge(){scratchpads_.teardown();layers_.teardown();activityMoves_.teardown();desktopMoves_.teardown();geometry_.teardown();if(exported_)QDBusConnection::sessionBus().unregisterObject("/org/kwabai/Bridge");}
void KwabaiReadOnlyBridge::updateGeometryInterval(){auto interval=geometry_.pollInterval();if(geometryTimer_.interval()!=interval)geometryTimer_.setInterval(interval);}
QString KwabaiReadOnlyBridge::Snapshot() {
    QJsonArray windows;
    for(auto *ew:KWin::effects->stackingOrder()) {
        auto *w=ew->window();
        if(!w||w->isDeleted()||w->isInternal()||w->isOutline()||!w->isNormalWindow()||w->isSpecialWindow())continue;
        QJsonArray desktops,activities;
        for(auto *desktop:w->desktops())desktops.append(desktop->id());
        for(auto activity:w->activities())activities.append(activity);
        windows.append(QJsonObject{{"uuid",w->internalId().toString(QUuid::WithoutBraces)},{"app",w->resourceClass()},
            {"is-modal",w->isModal()},{"is-transient",w->isTransient()},{"title",w->caption()},{"output",w->output()?w->output()->name():QString{}},{"desktops",desktops},{"activities",activities},
            {"frame",rect(w->frameGeometry())},{"requested-frame",rect(w->moveResizeGeometry())},{"client-frame",rect(w->clientGeometry())},{"buffer-frame",rect(w->bufferGeometry())},{"minimum",QJsonObject{{"w",w->constrainFrameSize(QSizeF(0,0)).width()},{"h",w->constrainFrameSize(QSizeF(0,0)).height()}}},{"maximum",QJsonObject{{"w",w->maxSize().width()},{"h",w->maxSize().height()}}},
            {"layer",w->keepAbove()?"above":w->keepBelow()?"below":"normal"},{"layer-mode",layers_.describe(w->internalId().toString(QUuid::WithoutBraces))["layer-mode"]},{"layer-error",layers_.describe(w->internalId().toString(QUuid::WithoutBraces))["layer-error"]},{"minimized",w->isMinimized()},{"fullscreen",w->isFullScreen()},{"focused",w->isActive()},
            {"candidate",!w->isHidden()&&!w->isMinimized()&&!w->isFullScreen()&&!w->isInteractiveMove()&&!w->isInteractiveResize()&&w->maximizeMode()==KWin::MaximizeRestore&&w->isOnCurrentDesktop()&&w->isOnCurrentActivity()&&w->isMovable()&&w->isResizable()}});
    }
    windows=scratchpads_.decorate(windows,[this](const QString &uuid){
        auto gesture=geometry_.interaction(uuid,geometryClock_.elapsed());
        if(gesture.value("state")=="active"){
            auto pointer=KWin::effects->cursorPos();gesture["pointer"]=QJsonObject{{"x",pointer.x()},{"y",pointer.y()}};
        }
        return gesture;
    });
    QJsonArray outputs;
    for(auto *output:KWin::workspace()->outputs()) {
        auto *desktop=KWin::effects->currentDesktop(output);
        outputs.append(QJsonObject{{"id",output->uuid()},{"name",output->name()},{"scale",output->scale()},
            {"geometry",rect(output->geometryF())},{"work-area",rect(KWin::effects->clientArea(KWin::MaximizeArea,output))},
            {"current-desktop",desktop?desktop->id():QString{}},{"current-activity",KWin::effects->currentActivity()}});
    }
    auto outputContent=QJsonDocument(outputs).toJson(QJsonDocument::Compact);
    if(!outputContent_.isEmpty()&&outputContent!=outputContent_)journal_.append("outputs_changed");
    outputContent_=outputContent;
    QJsonArray desktops;int desktopIndex=0;for(auto *d:KWin::VirtualDesktopManager::self()->desktops())desktops.append(QJsonObject{{"id",d->id()},{"index",++desktopIndex},{"name",d->name()}});
    QJsonArray capabilities{"geometry-reconcile","snapshot","contexts","events","layers","scratchpads","floating-grid","geometry","geometry-no-restore","geometry-zoom","geometry-update","geometry-retention","geometry-forget","geometry-lifecycle","notifications","focus","geometry-release","geometry-hidden-release","output-move","desktop-move","mouse","mouse-output-drop","mouse-output-swap"};if(previewSupported_){capabilities.append("drop-preview");capabilities.append("mouse-output-preview");}
    QJsonArray activities;
#if KWIN_BUILD_ACTIVITIES
    if(auto *controller=KWin::workspace()->activities()){for(const auto &id:controller->all())activities.append(QJsonObject{{"id",id},{"name",KActivities::Info(id).name()}});capabilities.append("activity-move");}
#endif
    auto body=QJsonObject{{"protocol",1},{"instance",instance_},{"kwin-version",QString(KWIN_VERSION_STRING)},
        {"capabilities",capabilities},{"outputs",outputs},{"desktops",desktops},{"activities",activities},{"windows",windows},{"event-sequence",QString::number(journal_.latest())},{"geometry-window-limit",20}};
    auto content=QJsonDocument(body).toJson(QJsonDocument::Compact);
    if(content!=content_) {if(sequence_==std::numeric_limits<quint64>::max()){instance_=QUuid::createUuid().toString(QUuid::WithoutBraces);body["instance"]=instance_;sequence_=0;}++sequence_;content_=QJsonDocument(body).toJson(QJsonDocument::Compact);}
    body["sequence"]=QString::number(sequence_);
    auto payload=QJsonDocument(body).toJson(QJsonDocument::Compact);
    if(payload.size()>1024*1024)return QStringLiteral("{\"error\":\"snapshot exceeds 1 MiB\"}");
    return QString::fromUtf8(payload);
}
QString KwabaiReadOnlyBridge::Scratchpad(const QString &request) {
    try {
        if(!calledFromDBus()||request.toUtf8().size()>16384)throw std::invalid_argument("invalid scratchpad caller or request size");
        auto doc=QJsonDocument::fromJson(request.toUtf8());if(!doc.isObject())throw std::invalid_argument("invalid scratchpad JSON");
        auto owner=message().service();auto result=scratchpads_.submit(owner,doc.object(),QJsonDocument::fromJson(Snapshot().toUtf8()).object());
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());owners->addWatchedService(owner);
        refresh_.start();Q_EMIT Changed(instance_);return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::SetLayer(const QString &request) {
    try {
        if(!calledFromDBus()||request.toUtf8().size()>16384)throw std::invalid_argument("invalid layer caller or request size");
        auto doc=QJsonDocument::fromJson(request.toUtf8());if(!doc.isObject())throw std::invalid_argument("invalid layer JSON");
        auto owner=message().service();auto result=layers_.submit(owner,doc.object(),QJsonDocument::fromJson(Snapshot().toUtf8()).object());
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());owners->addWatchedService(owner);
        refresh_.start();return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::Focus(const QString &request) {
    try {
        if(!calledFromDBus()||request.toUtf8().size()>16384)throw std::invalid_argument("invalid focus caller or request size");
        auto doc=QJsonDocument::fromJson(request.toUtf8());if(!doc.isObject())throw std::invalid_argument("invalid focus JSON");
        auto snapshot=QJsonDocument::fromJson(Snapshot().toUtf8()).object();auto owner=message().service();
        auto result=focus_.submit(owner,doc.object(),snapshot,geometryClock_.elapsed());
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());owners->addWatchedService(owner);
        return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::FocusStatus(const QString &id) {
    try {if(!calledFromDBus())throw std::invalid_argument("D-Bus owner required");return QString::fromUtf8(QJsonDocument(focus_.status(message().service(),id)).toJson());}
    catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::Apply(const QString &request) {
    try {
        if(!calledFromDBus()||request.toUtf8().size()>1024*1024)throw std::invalid_argument("invalid geometry caller or request size");
        auto owner=message().service();
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());
        auto r=QJsonDocument::fromJson(request.toUtf8());if(!r.isObject())throw std::invalid_argument("invalid geometry JSON");
        auto snapshot=QJsonDocument::fromJson(Snapshot().toUtf8()).object();
        if((desktopMoves_.pending()||activityMoves_.pending()))throw std::invalid_argument("desktop movement is pending");
        auto result=geometry_.apply(owner,r.object(),snapshot,geometryClock_.elapsed());updateGeometryInterval();owners->addWatchedService(owner);return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::Transaction(const QString &id) {
    try {if(!calledFromDBus())throw std::invalid_argument("D-Bus owner required");return QString::fromUtf8(QJsonDocument(geometry_.status(message().service(),id)).toJson());}
    catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::Forget(const QString &id) {
    try {if(!calledFromDBus()||(desktopMoves_.pending()||activityMoves_.pending()))throw std::invalid_argument("identified owner and settled desktop movement required");auto result=geometry_.forget(message().service(),id);publishPreview();return QString::fromUtf8(QJsonDocument(result).toJson());}
    catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::Restore(const QString &id) {
    try {if((desktopMoves_.pending()||activityMoves_.pending()))throw std::invalid_argument("desktop movement is pending");if(!calledFromDBus())throw std::invalid_argument("D-Bus owner required");auto result=geometry_.restore(message().service(),id,geometryClock_.elapsed());updateGeometryInterval();return QString::fromUtf8(QJsonDocument(result).toJson());}
    catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::Events(const QString &instance,const QString &after) {
    bool ok=false;auto cursor=after.toULongLong(&ok);
    if(!ok||QString::number(cursor)!=after)return QStringLiteral("{\"error\":\"invalid cursor\"}");
    return QString::fromUtf8(QJsonDocument(journal_.since(instance,cursor)).toJson(QJsonDocument::Compact));
}
void KwabaiReadOnlyBridge::observe(KWin::EffectWindow *ew) {
    auto *w=ew->window();if(!w||w->isInternal()||w->isOutline()||!w->isNormalWindow()||w->isSpecialWindow())return;
    auto changed=[this]{if(!refresh_.isActive())refresh_.start();};
    auto record=[this,w](const QString &type){journal_.append(type,{{"uuid",w->internalId().toString(QUuid::WithoutBraces)}});};
    connect(w,&KWin::Window::frameGeometryChanged,this,[changed,record]{record("window_geometry");changed();});
    connect(w,&KWin::Window::interactiveMoveResizeStarted,this,[this,w,changed,record]{geometry_.beginInteraction(w->internalId().toString(QUuid::WithoutBraces),w->isInteractiveResize()?"resize":"move",geometryClock_.elapsed());record("window_drag_start");changed();});
    connect(w,&KWin::Window::interactiveMoveResizeStepped,this,[changed,record]{record("window_drag_step");changed();});
    connect(w,&KWin::Window::interactiveMoveResizeFinished,this,[this,w,changed,record]{geometry_.finishInteraction(w->internalId().toString(QUuid::WithoutBraces),KWin::effects->cursorPos(),geometryClock_.elapsed());publishPreview();record("window_drag_finish");changed();});
    connect(w,&KWin::Window::captionChanged,this,[changed,record]{record("window_title");changed();});
    connect(w,&KWin::Window::modalChanged,this,[changed,record]{record("window_state");changed();});
    connect(w,&KWin::Window::transientChanged,this,[changed,record]{record("window_state");changed();});
    connect(w,&KWin::Window::keepAboveChanged,this,[changed,record]{record("window_state");changed();});
    connect(w,&KWin::Window::keepBelowChanged,this,[changed,record]{record("window_state");changed();});
    connect(w,&KWin::Window::minimizedChanged,this,[changed,record]{record("window_state");changed();});
    connect(w,&KWin::Window::fullScreenChanged,this,[changed,record]{record("window_state");changed();});
    connect(w,&KWin::Window::desktopsChanged,this,[changed,record]{record("window_context");changed();});
    connect(w,&KWin::Window::activitiesChanged,this,[changed,record]{record("window_context");changed();});
}

QString KwabaiReadOnlyBridge::MoveDesktop(const QString &request) {
    try {
        if(!calledFromDBus()||request.toUtf8().size()>16384)throw std::invalid_argument("invalid desktop movement caller/request");
        auto doc=QJsonDocument::fromJson(request.toUtf8());if(!doc.isObject())throw std::invalid_argument("invalid desktop movement JSON");
        auto result=desktopMoves_.submit(message().service(),doc.object(),QJsonDocument::fromJson(Snapshot().toUtf8()).object(),geometryClock_.elapsed());
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());owners->addWatchedService(message().service());return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::DesktopTransaction(const QString &id) {
    try {if(!calledFromDBus())throw std::invalid_argument("D-Bus owner required");return QString::fromUtf8(QJsonDocument(desktopMoves_.status(message().service(),id)).toJson());}
    catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}

void KwabaiReadOnlyBridge::publishPreview() {
    if(!previewTracking_)return;
    auto value=geometry_.dropPreview(QJsonDocument::fromJson(Snapshot().toUtf8()).object(),geometryClock_.elapsed());
    if(value!=lastPreview_){lastPreview_=value;Q_EMIT DropPreviewChanged(value);}
    if(!value["visible"].toBool())previewTracking_=false;
}
QString KwabaiReadOnlyBridge::DropPreview(const QString &request) {
    try {
        if(!previewSupported_||!calledFromDBus()||request.toUtf8().size()>16384)throw std::invalid_argument("drop preview unavailable or invalid caller/request");
        auto doc=QJsonDocument::fromJson(request.toUtf8());if(!doc.isObject())throw std::invalid_argument("invalid drop preview JSON");
        auto result=geometry_.setDropPreview(message().service(),doc.object(),QJsonDocument::fromJson(Snapshot().toUtf8()).object(),geometryClock_.elapsed());
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());owners->addWatchedService(message().service());previewTracking_=true;publishPreview();
        return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"visible",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}

QString KwabaiReadOnlyBridge::MoveActivity(const QString &request) {
    try {
        if(!calledFromDBus()||request.toUtf8().size()>16384)throw std::invalid_argument("invalid Activity movement caller/request");
        auto doc=QJsonDocument::fromJson(request.toUtf8());if(!doc.isObject())throw std::invalid_argument("invalid Activity movement JSON");
        auto result=activityMoves_.submit(message().service(),doc.object(),QJsonDocument::fromJson(Snapshot().toUtf8()).object(),geometryClock_.elapsed());
        auto *owners=qobject_cast<QDBusServiceWatcher *>(property("geometry-owner-watcher").value<QObject *>());owners->addWatchedService(message().service());return QString::fromUtf8(QJsonDocument(result).toJson());
    }catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}
QString KwabaiReadOnlyBridge::ActivityTransaction(const QString &id) {
    try {if(!calledFromDBus())throw std::invalid_argument("D-Bus owner required");return QString::fromUtf8(QJsonDocument(activityMoves_.status(message().service(),id)).toJson());}
    catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}
}

