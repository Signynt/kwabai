#include "focus.h"
#include "layers.h"
#include "scratchpads.h"
#include "geometry.h"
#include "desktops.h"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QMap>
#include <QTextStream>
#include <iostream>
class FakeBridge: public QObject, protected QDBusContext {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface","org.kwabai.Bridge1")
public:
 QJsonObject snapshotObject(){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};auto s=QJsonDocument::fromJson(f.readAll()).object();auto caps=s["capabilities"].toArray();if(caps.contains("geometry")&&!s["test-legacy-restore"].toBool()&&!caps.contains("geometry-no-restore"))caps.append("geometry-no-restore");if(caps.contains("geometry-update")){if(!caps.contains("geometry-retention"))caps.append("geometry-retention");if(!caps.contains("geometry-forget"))caps.append("geometry-forget");}if(caps.contains("mouse")&&!caps.contains("geometry-reconcile"))caps.append("geometry-reconcile");s["capabilities"]=caps;if(caps.contains("layers")){auto ws=s["windows"].toArray();for(int i=0;i<ws.size();++i){auto w=ws[i].toObject();if(!w.contains("layer"))w["layer"]="normal";auto info=layers.describe(w["uuid"].toString());w["layer-mode"]=info["layer-mode"];w["layer-error"]=info["layer-error"];ws[i]=w;}s["windows"]=ws;}if(caps.contains("scratchpads"))s["windows"]=scratchpads.decorate(s["windows"].toArray());return s;}
 void writeSnapshot(QJsonObject s){s["sequence"]=QString::number(s["sequence"].toString().toULongLong()+1);QSaveFile out(path);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(s).toJson());out.commit();}}
 QString active(){for(auto v:snapshotObject()["windows"].toArray())if(v.toObject()["focused"].toBool())return v.toObject()["uuid"].toString();return {};}
 kwabai::FocusRequests focus{[this]{return active();},[this](const QString &target){
  auto s=snapshotObject();if(s["test-focus-refuse"].toBool())return false;
  if(!s["test-focus-ignore"].toBool())QTimer::singleShot(50,this,[this,target]{auto current=snapshotObject();auto ws=current["windows"].toArray();for(int n=0;n<ws.size();++n){auto w=ws[n].toObject();w["focused"]=w["uuid"]==QJsonValue(target);ws[n]=w;}current["windows"]=ws;current["sequence"]=QString::number(current["sequence"].toString().toULongLong()+1);QFile f(path);if(f.open(QIODevice::WriteOnly|QIODevice::Truncate)){f.write(QJsonDocument(current).toJson());f.close();notify();}});
  return true;
 }};
 kwabai::DesktopMoves desktopMoves{
  [this](const QString &uuid){for(auto v:snapshotObject()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject();return QJsonObject{};},
  [this](const QString &uuid,const QJsonArray &ids){auto s=snapshotObject();if(s["test-desktop-reject"].toBool())return false;if(s["test-desktop-ignore"].toBool())return true;
   for(auto id:ids){bool found=false;for(auto d:s["desktops"].toArray())if(d.toObject()["id"]==id)found=true;if(!found)return false;}
   auto windows=s["windows"].toArray();bool found=false;
   for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]!=QJsonValue(uuid))continue;found=true;w["desktops"]=ids;QJsonObject output;for(auto o:s["outputs"].toArray())if(o.toObject()["name"]==w["output"])output=o.toObject();
    const bool current=ids.isEmpty()||ids.contains(output["current-desktop"]);w["candidate"]=current&&!w["minimized"].toBool()&&!w["fullscreen"].toBool();if(!current)w["focused"]=false;windows[i]=w;}
   if(!found)return false;s["windows"]=windows;writeSnapshot(s);return true;},
  [this](const QString &uuid){for(const auto &j:jobs){auto state=j["state"].toString();if(!j["completed"].toBool())return false;if(state=="applied"&&j["test-owned-frames"].toObject().contains(uuid))return false;}return true;}
 };
 kwabai::MembershipMoves activityMoves{
  [this](const QString &uuid){for(auto v:snapshotObject()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject();return QJsonObject{};},
  [this](const QString &uuid,const QJsonArray &ids){auto s=snapshotObject();if(s["test-activity-reject"].toBool())return false;if(s["test-activity-ignore"].toBool())return true;
   for(auto id:ids){bool found=false;for(auto v:s["activities"].toArray())if(v.toObject()["id"]==id)found=true;if(!found)return false;}
   auto windows=s["windows"].toArray();for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]!=QJsonValue(uuid))continue;w["activities"]=ids;QJsonObject output;for(auto o:s["outputs"].toArray())if(o.toObject()["name"]==w["output"])output=o.toObject();
    const bool current=(ids.isEmpty()||ids.contains(output["current-activity"]))&&(w["desktops"].toArray().isEmpty()||w["desktops"].toArray().contains(output["current-desktop"]));w["candidate"]=current&&!w["minimized"].toBool()&&!w["fullscreen"].toBool();if(!current)w["focused"]=false;windows[i]=w;s["windows"]=windows;writeSnapshot(s);return true;}return false;},
  [this](const QString &uuid){if(desktopMoves.pending()||mouseGeometry.pending()||mouseGeometry.owns(uuid))return false;for(const auto &j:jobs)if(!j["completed"].toBool()||(j["state"]=="applied"&&j["test-owned-frames"].toObject().contains(uuid)))return false;return true;},true
 };
 kwabai::GeometryTransactions mouseGeometry{
  [this](const QString &uuid)->std::optional<QRectF>{for(auto v:snapshotObject()["windows"].toArray()){auto w=v.toObject();if(w["uuid"]==QJsonValue(uuid)){auto r=w["frame"].toObject();return QRectF(r["x"].toDouble(),r["y"].toDouble(),r["w"].toDouble(),r["h"].toDouble());}}return {};},
  [this](const QString &uuid,const QRectF &r){auto s=snapshotObject();if(s["test-geometry-reject"].toBool())return false;auto windows=s["windows"].toArray();for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]!=QJsonValue(uuid))continue;if(!w["candidate"].toBool())return false;if(w["test-ignore-frame"].toBool())return true;w["frame"]=QJsonObject{{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};if(s["test-center-first-resize"].toArray().contains(uuid)&&!startupCentered.contains(uuid)){auto f=w["frame"].toObject();f["x"]=800;f["y"]=350;w["frame"]=f;startupCentered.insert(uuid);}if(s["capabilities"].toArray().contains("mouse-output-drop"))for(auto value:s["outputs"].toArray()){auto o=value.toObject(),g=o["geometry"].toObject();QRectF bounds(g["x"].toDouble(),g["y"].toDouble(),g["w"].toDouble(),g["h"].toDouble());if(bounds.contains(r.center()))w["output"]=o["name"];}windows[i]=w;s["windows"]=windows;writeSnapshot(s);return true;}return false;},
  [this](const QString &uuid){for(auto v:snapshotObject()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject()["output"].toString();return QString{};},
  [this](const QString &uuid){auto s=snapshotObject();auto windows=s["windows"].toArray();for(int i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]==QJsonValue(uuid)){w["test-raise-count"]=w["test-raise-count"].toInt()+1;windows[i]=w;}}s["windows"]=windows;auto order=stackOrder();order.removeAll(uuid);order.append(uuid);s["test-stacking-order"]=QJsonArray::fromStringList(order);writeSnapshot(s);}
 };
 QStringList stackOrder(){auto s=rawSnapshot();QStringList live,order;for(auto v:s["windows"].toArray()){auto w=v.toObject();if(!w["hidden"].toBool()&&!w["minimized"].toBool()&&!w["fullscreen"].toBool())live.append(w["uuid"].toString());}for(auto id:s["test-stacking-order"].toArray())if(live.contains(id.toString()))order.append(id.toString());for(auto id:live)if(!order.contains(id))order.append(id);return order;}
 bool mouse(){return snapshotObject()["capabilities"].toArray().contains("mouse");}
 kwabai::LayerRequests layers{
  [this](const QString &uuid){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QString{};auto s=QJsonDocument::fromJson(f.readAll()).object();for(auto v:s["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject().value("layer").toString("normal");return QString{};},
  [this](const QString &uuid,const QString &layer){auto s=snapshotObject();auto ws=s["windows"].toArray();for(int i=0;i<ws.size();++i){auto w=ws[i].toObject();if(w["uuid"]!=QJsonValue(uuid))continue;if(w.contains("test-force-layer")&&w["test-force-layer"]!=QJsonValue(layer))return false;w["layer"]=layer;ws[i]=w;s["windows"]=ws;writeSnapshot(s);return true;}return false;},
  [this](const QString &uuid){if(mouseGeometry.tiles(uuid))return true;for(const auto &job:jobs)if(job["state"]=="applied"&&job["test-owned-frames"].toObject().contains(uuid))return true;return false;},
  [this](const QString &uuid){return !scratchpads.describe(uuid)["scratchpad"].toString().isEmpty()?2:mouseGeometry.zoomed(uuid)?1:0;},
  [this]{return stackOrder();},
  [this](const QString &uuid,const QString &reference){auto order=stackOrder();if(!order.contains(uuid)||!order.contains(reference))return false;order.removeAll(uuid);order.insert(order.indexOf(reference)+1,uuid);auto s=rawSnapshot();s["test-stacking-order"]=QJsonArray::fromStringList(order);s["test-restacks"]=s["test-restacks"].toInt()+1;writeSnapshot(s);return true;}
 };
 QJsonObject rawSnapshot(){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
 kwabai::Scratchpads scratchpads{
  [this](const QString &uuid){auto s=rawSnapshot();for(auto v:s["windows"].toArray()){auto w=v.toObject();if(w["uuid"]!=QJsonValue(uuid))continue;QJsonObject o;for(auto v:s["outputs"].toArray())if(v.toObject()["name"]==w["output"])o=v.toObject();auto ds=w["desktops"].toArray(),as=w["activities"].toArray();w["hidden"]=w["hidden"].toBool()||(w["test-minimized-hide"].toBool()&&w["minimized"].toBool());w["current"]=(s["test-active-output"].toString().isEmpty()||w["output"]==s["test-active-output"])&&(ds.isEmpty()||ds.contains(o["current-desktop"]))&&(as.isEmpty()||as.contains(o["current-activity"]));return w;}return QJsonObject{};},
  [this](const QString &uuid,bool hide,bool summon){auto s=rawSnapshot();if(s["test-scratchpad-refuse"].toBool())return false;auto ws=s["windows"].toArray();bool found=false;for(int i=0;i<ws.size();++i){auto w=ws[i].toObject();if(w["uuid"]==QJsonValue(uuid)){found=true;w["hidden"]=hide;if(w["test-x11"].toBool()){w["hidden"]=false;w["minimized"]=hide;w["test-minimized-hide"]=hide;}w["focused"]=!hide&&summon;w["candidate"]=!hide;if(!hide&&summon){auto o=s["outputs"].toArray().first().toObject();for(auto v:s["outputs"].toArray())if(v.toObject()["name"]==s["test-active-output"])o=v.toObject();w["desktops"]=QJsonArray{o["current-desktop"]};w["activities"]=o["current-activity"].toString().isEmpty()?QJsonArray{}:QJsonArray{o["current-activity"]};w["output"]=o["name"];w["minimized"]=false;}}
   else if(!hide&&summon)w["focused"]=false;ws[i]=w;}s["windows"]=ws;writeSnapshot(s);return found;},
  [this](const QString &uuid){if(mouseGeometry.pending()||mouseGeometry.owns(uuid))return true;for(const auto &j:jobs)if(!j["completed"].toBool()||(j["state"]=="applied"&&j["test-owned-frames"].toObject().contains(uuid)))return true;return false;}
 };
 QDBusServiceWatcher *geometryOwners=nullptr;
 QDBusServiceWatcher *layerOwners=nullptr;
 QMap<QString,QPointF> interactionPointers;
 QSet<QString> startupCentered;
 QByteArray mouseContent;quint64 mouseSequence=0;
 QElapsedTimer clock;QTimer focusTimer;
 void updateGeometryInterval(){auto interval=mouseGeometry.pollInterval();if(focusTimer.interval()!=interval)focusTimer.setInterval(interval);}
 FakeBridge(){clock.start();focusTimer.setTimerType(Qt::PreciseTimer);focusTimer.setInterval(mouseGeometry.pollInterval());connect(&focusTimer,&QTimer::timeout,this,[this]{bool changed=mouseGeometry.tick(clock.elapsed());changed=layers.tick()||changed;changed=focus.tick(clock.elapsed())||changed;changed=desktopMoves.tick(clock.elapsed())||changed;changed=activityMoves.tick(clock.elapsed())||changed;updateGeometryInterval();if(changed)notify();});focusTimer.start();}
 QString path;
 QMap<QString,QJsonObject> jobs;
 QMap<QString,quint64> sequences;
 QFileSystemWatcher changes;
 void notify(){QFile f(path);if(f.open(QIODevice::ReadOnly)){auto s=QJsonDocument::fromJson(f.readAll()).object();if(s["capabilities"].toArray().contains("notifications")&&!s["test-suppress-notifications"].toBool())Q_EMIT Changed(s["instance"].toString());}}
 void watch(){changes.addPath(QFileInfo(path).absolutePath());connect(&changes,&QFileSystemWatcher::directoryChanged,this,[this]{notify();});}
Q_SIGNALS:
 void Changed(const QString &instance);
public Q_SLOTS:
 void watchGeometryOwner(){if(!geometryOwners){geometryOwners=new QDBusServiceWatcher(this);geometryOwners->setConnection(QDBusConnection::sessionBus());geometryOwners->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);connect(geometryOwners,&QDBusServiceWatcher::serviceUnregistered,this,[this](const QString &owner){mouseGeometry.release(owner,clock.elapsed());scratchpads.release(owner);notify();});}geometryOwners->addWatchedService(message().service());}
 QString Scratchpad(const QString &request){try{watchGeometryOwner();auto result=scratchpads.submit(message().service(),QJsonDocument::fromJson(request.toUtf8()).object(),snapshotObject());notify();return QString::fromUtf8(QJsonDocument(result).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString SetLayer(const QString &request){try{if(!layerOwners){layerOwners=new QDBusServiceWatcher(this);layerOwners->setConnection(QDBusConnection::sessionBus());layerOwners->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);connect(layerOwners,&QDBusServiceWatcher::serviceUnregistered,this,[this](const QString &owner){layers.release(owner);scratchpads.release(owner);mouseGeometry.release(owner,clock.elapsed());notify();});}layerOwners->addWatchedService(message().service());auto result=layers.submit(message().service(),QJsonDocument::fromJson(request.toUtf8()).object(),snapshotObject());notify();return QString::fromUtf8(QJsonDocument(result).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString DropPreview(const QString &request){try{return QString::fromUtf8(QJsonDocument(mouseGeometry.setDropPreview(message().service(),QJsonDocument::fromJson(request.toUtf8()).object(),snapshotObject(),clock.elapsed())).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"visible",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString TestDropPreview(){return QString::fromUtf8(QJsonDocument(mouseGeometry.dropPreview(snapshotObject(),clock.elapsed())).toJson());}
 QString TestInteraction(const QString &uuid,const QString &kind,const QString &phase,double x,double y){if(phase=="start"){interactionPointers[uuid]={x,y};mouseGeometry.beginInteraction(uuid,kind,clock.elapsed());}else if(phase=="step")interactionPointers[uuid]={x,y};else mouseGeometry.finishInteraction(uuid,{x,y},clock.elapsed());writeSnapshot(snapshotObject());notify();return "ok";}
 QString MouseSnapshot(){auto s=snapshotObject();auto windows=s["windows"].toArray();for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();auto g=mouseGeometry.interaction(w["uuid"].toString(),clock.elapsed());if(!g.isEmpty()){if(g["state"]=="active"){auto pointer=interactionPointers.value(w["uuid"].toString());g["pointer"]=QJsonObject{{"x",pointer.x()},{"y",pointer.y()}};}w["interaction"]=g;}windows[i]=w;}s["windows"]=windows;auto content=QJsonDocument(s).toJson();if(content!=mouseContent){mouseContent=content;mouseSequence=std::max(mouseSequence+1,s["sequence"].toString().toULongLong());}s["sequence"]=QString::number(mouseSequence);return QString::fromUtf8(QJsonDocument(s).toJson());}
 QString Snapshot(){if(mouse())return MouseSnapshot();QFile f(path);if(!f.open(QIODevice::ReadOnly))return "{}";auto bytes=f.readAll();if(QJsonDocument::fromJson(bytes).object().value("test-stall").toBool()){setDelayedReply(true);return {}; }auto snapshot=snapshotObject();snapshot.remove("test-events");snapshot.remove("test-event-gap");snapshot.remove("test-geometry-reject");return QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact));}
 QString Focus(const QString &bytes){try{return QString::fromUtf8(QJsonDocument(focus.submit(message().service(),QJsonDocument::fromJson(bytes.toUtf8()).object(),snapshotObject(),clock.elapsed())).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString FocusStatus(const QString &id){try{return QString::fromUtf8(QJsonDocument(focus.status(message().service(),id)).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString MoveActivity(const QString &bytes){try{return QString::fromUtf8(QJsonDocument(activityMoves.submit(message().service(),QJsonDocument::fromJson(bytes.toUtf8()).object(),snapshotObject(),clock.elapsed())).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString ActivityTransaction(const QString &id){try{return QString::fromUtf8(QJsonDocument(activityMoves.status(message().service(),id)).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString MoveDesktop(const QString &bytes){try{return QString::fromUtf8(QJsonDocument(desktopMoves.submit(message().service(),QJsonDocument::fromJson(bytes.toUtf8()).object(),snapshotObject(),clock.elapsed())).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString DesktopTransaction(const QString &id){try{return QString::fromUtf8(QJsonDocument(desktopMoves.status(message().service(),id)).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"acknowledged",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString Apply(const QString &bytes) {
  if(snapshotObject()["test-reject-pending-once"].toBool()){auto s=snapshotObject();s.remove("test-reject-pending-once");writeSnapshot(s);return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","another geometry operation is pending"}}).toJson());}
  if(mouse()){try{watchGeometryOwner();auto result=mouseGeometry.apply(message().service(),QJsonDocument::fromJson(bytes.toUtf8()).object(),QJsonDocument::fromJson(MouseSnapshot().toUtf8()).object(),clock.elapsed());updateGeometryInterval();return QString::fromUtf8(QJsonDocument(result).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
  if(desktopMoves.pending())return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","desktop movement is pending"}}).toJson());
  auto request=QJsonDocument::fromJson(bytes.toUtf8()).object();auto id=request["id"].toString();
  QFile f(path);if(!f.open(QIODevice::ReadOnly))return "{}";auto s=QJsonDocument::fromJson(f.readAll()).object();
  if(s["capabilities"].toArray().contains("geometry-retention")) {
   bool ok=false;auto text=request["request-sequence"].toString();auto serial=text.toULongLong(&ok);auto owner=message().service();
   if(!ok||serial==0||QString::number(serial)!=text||serial<=sequences.value(owner))return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","stale test request sequence"}}).toJson());
   sequences[owner]=serial;
  }
  if(s["test-geometry-exclusive"].toBool())for(const auto &job:jobs)if(!job["completed"].toBool())return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","concurrent test geometry"}}).toJson());
  if(s.value("test-geometry-reject").toBool())return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","stale test request"}}).toJson());
  // Mirror the native validator: an absent optional flag is valid, an explicit
  // null/false flag is malformed. Reading it must not insert it into the request.
  const bool moving=request.value("move-output")==QJsonValue(true);
  if(request.contains("move-output")&&!moving)return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","invalid output move flag"}}).toJson());
  if(moving){try{kwabai::validateGeometryRequest(request,s);}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
  auto previous=request["supersedes"].toString();
  if(!previous.isEmpty()){jobs[previous]["state"]="superseding";jobs[previous]["completed"]=false;jobs[previous]["success"]=false;jobs[previous]["replacement-id"]=id;}
  jobs[id]=QJsonObject{{"id",id},{"state","pending"},{"completed",false},{"success",false},{"acknowledged",false}};
  jobs[id]["previous-id"]=previous;jobs[id]["release-request"]=request["release"];
  if(s["test-geometry-write"].toBool()) {
   auto original=jobs.value(previous)["test-owned-frames"].toObject();auto windows=s["windows"].toArray();
   for(int n=0;n<windows.size();++n){auto w=windows[n].toObject();auto uuid=w["uuid"].toString();
    for(auto value:request["release"].toArray())if(value.toObject()["uuid"]==w["uuid"]){original.remove(uuid);}
    for(auto value:request["placements"].toArray())if(value.toObject()["uuid"]==w["uuid"]){original[uuid]=value.toObject()["frame"];w["frame"]=value.toObject()["frame"];if(moving)w["output"]=request["context"].toObject()["output"];}
    windows[n]=w;
   }
   jobs[id]["test-owned-frames"]=original;s["windows"]=windows;writeSnapshot(s);
  }
  auto complete=[this,id,previous,moving,floating=request.value("floating-frame").toBool()]{if(!previous.isEmpty()){jobs[previous]["state"]="superseded";jobs[previous]["completed"]=true;jobs[previous]["acknowledged"]=false;}jobs[id]["state"]=(moving||floating)?"moved":"applied";if(moving||floating)jobs[id]["managed-windows"]=QJsonArray{};jobs[id]["completed"]=true;jobs[id]["success"]=true;jobs[id]["acknowledged"]=true;notify();};if(s["test-geometry-fast"].toBool())complete();else QTimer::singleShot(150,this,complete);
  return Transaction(id);
 }
 QString Transaction(const QString &id){if(mouse())return QString::fromUtf8(QJsonDocument(mouseGeometry.status(message().service(),id)).toJson());return QString::fromUtf8(QJsonDocument(jobs.value(id)).toJson());}
 QString Forget(const QString &id){auto snapshot=snapshotObject();if(snapshot["test-forget-pending-once"].toBool()){snapshot.remove("test-forget-pending-once");snapshot["test-forget-refusals"]=snapshot["test-forget-refusals"].toInt()+1;writeSnapshot(snapshot);return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","wait for pending native geometry before resetting a context"}}).toJson());}if(!mouse()){auto &j=jobs[id];j["state"]="forgotten";j["completed"]=true;j["success"]=true;j["acknowledged"]=true;j["test-owned-frames"]=QJsonObject{};j["managed-windows"]=QJsonArray{};return QString::fromUtf8(QJsonDocument(j).toJson());}try{return QString::fromUtf8(QJsonDocument(mouseGeometry.forget(message().service(),id)).toJson());}catch(const std::exception &e){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error",QString::fromUtf8(e.what())}}).toJson());}}
 QString Restore(const QString &){return QString::fromUtf8(QJsonDocument(QJsonObject{{"state","rejected"},{"completed",true},{"success",false},{"error","layout restoration was removed"}}).toJson());}
 QString Events(const QString &instance,const QString &after) {
  QFile file(path);if(!file.open(QIODevice::ReadOnly))return "{}";auto s=QJsonDocument::fromJson(file.readAll()).object();
  bool gap=s.value("test-event-gap").toBool()||instance!=s.value("instance").toString();QJsonArray events;
  if(!gap)for(auto value:s.value("test-events").toArray())if(value.toObject().value("ordinal").toString().toULongLong()>after.toULongLong())events.append(value);
  return QString::fromUtf8(QJsonDocument(QJsonObject{{"protocol",1},{"instance",s.value("instance")},{"after",after},{"latest",s.value("event-sequence")},{"resync-required",gap},{"events",events}}).toJson());
 }
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;FakeBridge bridge;bridge.path=QString::fromLocal8Bit(argv[1]);bridge.watch();auto bus=QDBusConnection::sessionBus();if(!bus.registerService("org.kde.KWin")||!bus.registerObject("/org/kwabai/Bridge",&bridge,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals))return 1;std::cout<<"ready"<<std::endl;return app.exec();}
#include "fake_bridge.moc"
