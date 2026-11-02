#include "model.h"
#include "manager.h"
#include "layer_policy.h"
#include "scratchpad_policy.h"
#include "grid_policy.h"
#include "diagnostics.h"
#include "transfers.h"
#include "desktops.h"
#include <QScopedValueRollback>
#include "client.h"
#include "plan.h"
#include "geometry.h"
#include "focus.h"
#include "store.h"
#include "protocol.h"
#include "../compat/spacing.h"
#include "../compat/resize.h"
#include "../compat/config.h"
#include <optional>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>
#include <QQueue>
#include <QCryptographicHash>
#include <QtEndian>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <iostream>
#include <limits>
#include <cmath>
#include <memory>
using namespace kwabai;
namespace {
int signalWrite=-1;
void terminate(int) {int saved=errno;char c=0;if(signalWrite>=0){auto ignored=::write(signalWrite,&c,1);(void)ignored;}errno=saved;}
void save(const QString &path,const QJsonObject &state) {
    auto bytes=QJsonDocument(state).toJson(QJsonDocument::Indented);
    if(bytes.size()>ipc::MaxMessage)throw std::invalid_argument("model state exceeds 1 MiB");
    if(path.isEmpty())return;
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||!file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)
        ||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("cannot commit model state");
}
struct Session {QByteArray bytes;bool done=false;};
}
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 try {
    QString socketPath,statePath,contextPath;bool simulate=false,observeKwin=false,enableControl=false;auto args=app.arguments();
    for(qsizetype i=1;i<args.size();i++) {
        if(args[i]=="--simulate")simulate=true;
        else if(args[i]=="--observe-kwin")observeKwin=true;
        else if(args[i]=="--enable-control")enableControl=true;
        else if(args[i]=="--help") {std::cout<<"Usage: kwabaid [--socket PATH] [--simulate [--state FILE] | --observe-kwin [--enable-control] [--context-state FILE]]\nGeometry commands require explicit control enablement.\n";return 0;}
        else if(args[i]=="--socket"||args[i]=="--state"||args[i]=="--context-state") {auto key=args[i];if(++i>=args.size()||args[i].isEmpty()||args[i].startsWith("--"))throw std::invalid_argument("path argument missing");auto &dest=key=="--socket"?socketPath:(key=="--state"?statePath:contextPath);if(!dest.isEmpty())throw std::invalid_argument("duplicate path option");dest=QFileInfo(args[i]).absoluteFilePath();}
        else throw std::invalid_argument("unsupported daemon option");
    }
    if(!contextPath.isEmpty()&&!observeKwin)throw std::invalid_argument("--context-state requires --observe-kwin");
    if(enableControl&&!observeKwin)throw std::invalid_argument("--enable-control requires --observe-kwin");
    if(simulate&&observeKwin)throw std::invalid_argument("--simulate and --observe-kwin cannot be combined");
    if(!statePath.isEmpty()&&!simulate)throw std::invalid_argument("--state currently requires --simulate");
    if(socketPath.isEmpty())socketPath=ipc::defaultSocket();
    if(!statePath.isEmpty()&&(statePath==socketPath||statePath==socketPath+".lock"||socketPath==statePath+".lock"))
        throw std::invalid_argument("socket, state and lock paths must be distinct");
    if(!contextPath.isEmpty()&&(contextPath==socketPath||contextPath==socketPath+".lock"||socketPath==contextPath+".lock"||contextPath==statePath||contextPath==statePath+".lock"))throw std::invalid_argument("context state and socket/state/lock paths must be distinct");
    ipc::privateDirectory(QFileInfo(socketPath).absolutePath(),true);
    std::unique_ptr<QLockFile> stateLock;
    if(!statePath.isEmpty()) {
        ipc::privateDirectory(QFileInfo(statePath).absolutePath(),false);
        stateLock=std::make_unique<QLockFile>(statePath+".lock");stateLock->setStaleLockTime(0);
        if(!stateLock->tryLock())throw std::runtime_error("another daemon owns this state file");
    }
    ContextStore contexts;std::unique_ptr<QLockFile> contextLock;
    if(!contextPath.isEmpty()) {
        ipc::privateDirectory(QFileInfo(contextPath).absolutePath(),false);
        contextLock=std::make_unique<QLockFile>(contextPath+".lock");contextLock->setStaleLockTime(0);
        if(!contextLock->tryLock())throw std::runtime_error("another daemon owns this context state");
        struct stat contextStat{};auto encodedContext=QFile::encodeName(contextPath);
        if(lstat(encodedContext.constData(),&contextStat)==0) {
            if(!S_ISREG(contextStat.st_mode)||contextStat.st_uid!=geteuid()||(contextStat.st_mode&0077))throw std::runtime_error("context state must be a private regular file owned by you");
            QFile file(contextPath);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("cannot read context state");auto bytes=file.read(ipc::MaxMessage+1);if(bytes.size()>ipc::MaxMessage)throw std::invalid_argument("context state exceeds 1 MiB");contexts=ContextStore::restore(ipc::parse(bytes));
        } else if(errno!=ENOENT)throw std::runtime_error("cannot inspect context state");
    }
    Model model;
    if(!statePath.isEmpty()) {
        ipc::privateDirectory(QFileInfo(statePath).absolutePath(),false);
        struct stat st{};auto encoded=QFile::encodeName(statePath);
        if(lstat(encoded.constData(),&st)==0) {
            if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))throw std::runtime_error("state must be a private regular file owned by you");
            QFile file(statePath);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("cannot read model state");
            auto bytes=file.read(ipc::MaxMessage+1);if(bytes.size()>ipc::MaxMessage)throw std::invalid_argument("model state exceeds 1 MiB");
            model=Model::restore(ipc::parse(bytes));
        } else if(errno!=ENOENT)throw std::runtime_error("cannot inspect state path");
    }
    QLockFile lock(socketPath+".lock");lock.setStaleLockTime(0);
    if(!lock.tryLock())throw std::runtime_error("another daemon owns this socket lock");
    // Never remove a path belonging to a running listener, even one without our lock.
    QLocalSocket existing;existing.connectToServer(socketPath);
    if(existing.waitForConnected(100))throw std::runtime_error("socket already has a listener");
    struct stat st{};auto encoded=QFile::encodeName(socketPath);
    if(lstat(encoded.constData(),&st)==0) {
        if(!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid())throw std::runtime_error("refusing to replace non-socket or foreign path");
        if(!QLocalServer::removeServer(socketPath))throw std::runtime_error("cannot remove stale socket");
    } else if(errno!=ENOENT)throw std::runtime_error("cannot inspect socket path");
    QLocalServer server;server.setSocketOptions(QLocalServer::UserAccessOption);
    if(!server.listen(socketPath))throw std::runtime_error(server.errorString().toStdString());
    if(::chmod(encoded.constData(),0600)!=0)throw std::runtime_error("cannot restrict socket permissions");
    BridgeClient bridge;
    if(observeKwin)bridge.start();
    ContextManager manager(bridge,contexts,[&](const QJsonObject &state){save(contextPath,state);});
    LayerPolicy layers(bridge,contexts,enableControl);
    ScratchpadPolicy scratchpads(bridge,contexts,manager,enableControl);
    GridPolicy grids(bridge,contexts,manager,enableControl);
    std::function<QJsonObject(const QStringList &)> executeTransfer;bool internalTransfer=false;
    OutputTransfers transfers(bridge,contexts,manager,[&](const QStringList &args){return executeTransfer(args);});
    const auto instance=QUuid::createUuid().toString(QUuid::WithoutBraces);
    int active=0;QMap<QString,QPair<QByteArray,QJsonObject>> replay;QQueue<QString> replayOrder;
    auto response=[&](const QString &id,int status,const QString &out,const QString &error,const QString &code) {
        return QJsonObject{{"version",1},{"id",id},{"status",status},{"stdout",out},{"stderr",error},
            {"error",code},{"generation",QString::number(model.generation)},{"instance",instance}};
    };
    auto dispatch=[&](const QJsonObject &request)->QJsonObject {
        QString id=request.value("id").toString();
        try {
            if(request.size()!=3||request.value("version")!=QJsonValue(1)||id.isEmpty()||id.size()>128||!request.value("args").isArray())throw std::invalid_argument("invalid version, operation ID or arguments");
            auto array=request.value("args").toArray();if(array.isEmpty()||array.size()>64)throw std::invalid_argument("invalid argument count");
            QStringList arguments;for(auto v:array){if(!v.isString()||v.toString().size()>4096)throw std::invalid_argument("invalid command argument");arguments<<v.toString();}
            auto digest=QCryptographicHash::hash(QJsonDocument(request).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
            if(replay.contains(id)) {
                if(replay[id].first!=digest)throw std::invalid_argument("operation ID reused with a different request");
                return replay[id].second;
            }
            bool dragging=false;if(bridge.state().hasMouse())for(auto w:bridge.state().windows()["windows"].toArray())if(w.toObject().value("interaction").toObject()["state"]=="active")dragging=true;
            if((transfers.active()||manager.mouseDropBusy()||dragging)&&!internalTransfer) {
                const bool read=arguments==QStringList{"doctor"}||arguments==QStringList{"inspect"}||arguments==QStringList{"compat-windows"}||(arguments[0]=="config"&&(arguments.size()==2||(dragging&&!transfers.active()&&!manager.mouseDropBusy()&&arguments.size()==3))&&compat::isPreviewSetting(arguments[1]))
                    ||(arguments[0]=="bridge"&&arguments.size()>=2&&QStringList{"status","windows","outputs","events","transaction","focus-status","layer-status","scratchpad-status","plan","desktops","desktop-transaction","activities","activity-transaction"}.contains(arguments[1]))
                    ||(arguments[0]=="context"&&arguments.size()>=2&&(QStringList{"status","show","plan","list","managers","transfer-status","neighbor","geometry"}.contains(arguments[1])||(arguments[1]=="reconcile"&&arguments.size()==3)))
                    ||(arguments[0]=="rule"&&arguments.size()>=2&&QStringList{"list","list-posix","test"}.contains(arguments[1]));
                const bool stop=dragging&&arguments.size()==4&&arguments[0]=="context"&&arguments[3]=="off"&&(arguments[1]=="manage"||arguments[1]=="manage-id");
                if(!read&&!stop)throw std::invalid_argument(dragging?"mouse interaction in progress; finish the gesture before editing layouts":manager.mouseDropBusy()?"output drop in progress; inspect context status before editing layouts":"context transfer in progress; inspect context transfer-status before mutating layouts");
            }
            QJsonObject reply;
            if(arguments.size()==3&&arguments[0]=="context"&&arguments[1]=="configure"&&(arguments[2]=="begin"||arguments[2]=="end")) {
                manager.pauseConfiguration(arguments[2]=="begin");layers.pause(arguments[2]=="begin");scratchpads.pause(arguments[2]=="begin");grids.pause(arguments[2]=="begin");reply=response(id,0,"","","");
            }
            else if(arguments[0]=="config") {
                if((arguments.size()!=2&&arguments.size()!=3)||(!compat::isPreviewSetting(arguments[1])&&!compat::isLayoutSetting(arguments[1])))throw std::invalid_argument("unsupported config setting; window_shadow belongs to KDE decoration rules");
                auto name=arguments[1];QString result;
                if(name=="layout"){
                    if(arguments.size()==3){if(!enableControl)throw std::invalid_argument("layout control requires --enable-control");manager.setLayout(arguments[2]);}result=manager.layout();
                }else {
                    auto candidate=contexts;
                    if(arguments.size()==3){if(name=="float_popups")candidate.setFloatPopups(arguments[2]);else if(name=="window_border_radius")candidate.setWindowBorderRadius(arguments[2]);else if(compat::isInsertFeedbackSetting(name))candidate.setInsertFeedbackColor(arguments[2]);else candidate.setGlobalSpacing(bridge.state(),name,arguments[2]);if(candidate.state()!=contexts.state())save(contextPath,candidate.state());contexts=std::move(candidate);}
                    result=name=="float_popups"?(contexts.floatPopups()?"on":"off"):name=="window_border_radius"?QString::number(contexts.windowBorderRadius()):compat::isInsertFeedbackSetting(name)?contexts.insertFeedbackColor():QString::number(contexts.globalSpacing(name));
                }
                reply=response(id,0,arguments.size()==2?result+"\n":QString{},"","");
            }
            else if(arguments.size()==4&&arguments[0]=="context"&&(arguments[1]=="transfer-output"||arguments[1]=="transfer-desktop"||arguments[1]=="transfer-activity")) {
                if(!enableControl)return response(id,3,"","transfer requires --enable-control\n","control_disabled");
                reply=response(id,0,QString::fromUtf8(QJsonDocument(transfers.start(arguments[2],arguments[3],arguments[1]=="transfer-desktop",arguments[1]=="transfer-activity")).toJson()),"","");
            }
            else if(arguments.size()==3&&arguments[0]=="context"&&arguments[1]=="transfer-status")reply=response(id,0,QString::fromUtf8(QJsonDocument(transfers.status(arguments[2])).toJson()),"","");
            else if(arguments==QStringList{"doctor"}) {
                reply=response(id,0,QString::fromUtf8(QJsonDocument(QJsonObject{{"protocol",1},{"instance",instance},{"mode",simulate?"simulation":(observeKwin?"observation":"disconnected")},
                    {"bridge-connected",bridge.state().connected()},{"bridge",bridge.state().status()},{"event-stream",bridge.eventStatus()},{"desktop-control",enableControl&&bridge.state().hasGeometry()},{"generation",QString::number(model.generation)},
                    {"mouse-output-drop",manager.mouseDropBusy()},{"mouse-output-drop-status",manager.mouseDropStatus()},{"drop-preview",bridge.dropPreviewStatus()},{"desktop-move",bridge.latestDesktopMove()},{"activity-move",bridge.latestActivityMove()},{"output-transfer",transfers.latest()},{"geometry-busy",bridge.geometryBusy()},{"geometry-history",bridge.retentionStatus()},{"management",manager.status()},{"layout-policy",manager.layoutStatus()},{"layer-policy",layers.status()},{"scratchpad-policy",scratchpads.status()},{"grid-policy",grids.status()},{"context-state-file",contextPath},{"context-count",contexts.list().value("contexts").toArray().size()},{"state-file",statePath},{"pid",qint64(getpid())}}).toJson()),"","");
            } else if(arguments==QStringList{"inspect"})reply=response(id,0,model.inspect(),"","");
            else if(arguments==QStringList{"bridge","status"})reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.state().status()).toJson()),"","");
            else if(arguments==QStringList{"bridge","events"}) {
                if(!bridge.state().hasEvents()||!bridge.eventStatus().value("ready").toBool())reply=response(id,3,"","ordered event stream unavailable\n","events_unavailable");
                else reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.events()).toJson()),"","");
            }
            else if(arguments==QStringList{"bridge","windows"}) {
                if(!bridge.state().connected())reply=response(id,3,"","KWin bridge is unavailable; use --observe-kwin and load the effect\n","bridge_unavailable");
                else reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.state().windows()).toJson()),"","");
            }
            else if(arguments==QStringList{"bridge","outputs"}) {
                if(!bridge.state().hasContexts())reply=response(id,3,"",(bridge.state().connected()?QString("loaded bridge lacks contexts; run scripts/reload-effect.sh and restart the observation daemon\n"):QString("bridge disconnected: ")+bridge.state().status().value("error").toString()+"\n"),"contexts_unavailable");
                else reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.state().outputs()).toJson()),"","");
            }
            else if((arguments==QStringList{"bridge","desktops"}||arguments==QStringList{"bridge","activities"})) {
                const bool activity=arguments[1]=="activities";
                if(!(activity?bridge.state().hasActivityMove():bridge.state().hasDesktopMove()))throw std::invalid_argument(activity?"Activity catalog requires activity-move; run scripts/reload-effect.sh":"desktop catalog requires desktop-move; run scripts/reload-effect.sh");
                reply=response(id,0,QString::fromUtf8(QJsonDocument(activity?bridge.state().activities():bridge.state().desktops()).toJson()),"","");
            }
            else if(arguments.size()==3&&arguments[0]=="bridge"&&(arguments[1]=="desktop-transaction"||arguments[1]=="activity-transaction"))reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.desktopTransaction(arguments[2])).toJson()),"","");
            else if(arguments.size()==5&&arguments[0]=="context"&&(arguments[1]=="queue-desktop"||arguments[1]=="queue-activity")) {
                const bool activity=arguments[1]=="queue-activity";const QString membership=activity?"activities":"desktops",current=activity?"current-activity":"current-desktop";
                if(!internalTransfer)throw std::invalid_argument("membership admission is an internal transfer stage");
                if(!enableControl||!(activity?bridge.state().hasActivityMove():bridge.state().hasDesktopMove())||!bridge.eventStatus()["ready"].toBool()||bridge.geometryBusy())throw std::invalid_argument("membership arrival requires a ready bridge and confirmed native release");
                QJsonObject window,source;bool floated=false;
                for(auto v:bridge.state().windows()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(arguments[3]))window=v.toObject();
                for(auto v:bridge.state().outputs()["outputs"].toArray())if(v.toObject()["id"]==QJsonValue(arguments[2]))source=v.toObject();
                if(window.isEmpty()||source.isEmpty()||window["output"]!=source["name"]||window[membership]!=QJsonValue(QJsonArray{arguments[4]})||source[current]==QJsonValue(arguments[4]))throw std::invalid_argument("inactive destination membership was not observed");
                for(auto v:contexts.state()["contexts"].toArray()){auto c=v.toObject();for(auto m:c["members"].toArray())if(m.toObject()["uuid"]==window["uuid"])throw std::invalid_argument("membership arrival still belongs to a saved tree");if(c["floating"].toArray().contains(window["uuid"]))floated=true;}
                if(!floated||bridge.controlledWindows().contains(arguments[3]))throw std::invalid_argument("membership arrival was not released from tiling");
                auto candidate=contexts;auto context=candidate.stageDesktopAdmission(bridge.state(),arguments[2],arguments[3],arguments[4],activity);
                save(contextPath,candidate.state());contexts=std::move(candidate);manager.armArrival(context,arguments[3]);
                reply=response(id,0,QString::fromUtf8(QJsonDocument(context).toJson()),"","");
            }
            else if(arguments.size()==4&&arguments[0]=="context"&&(arguments[1]=="send-desktop"||arguments[1]=="send-activity")) {
                const bool activity=arguments[1]=="send-activity";
                if(!enableControl)return response(id,3,"","desktop movement requires --enable-control\n","control_disabled");
                if(!(activity?bridge.state().hasActivityMove():bridge.state().hasDesktopMove())||!bridge.eventStatus()["ready"].toBool())throw std::invalid_argument(activity?"Activity movement requires activity-move and ready events; run scripts/reload-effect.sh":"desktop movement requires desktop-move and ready events; run scripts/reload-effect.sh");
                if(bridge.geometryBusy())throw std::invalid_argument("another geometry or desktop operation is unresolved");
                QJsonObject window,source;int count=0;
                for(auto v:bridge.state().windows()["windows"].toArray()){auto w=v.toObject();if((arguments[2]=="focused"&&w["focused"].toBool())||w["id"]==QJsonValue(arguments[2])||w["uuid"]==QJsonValue(arguments[2])){window=w;++count;}}
                if(count!=1)throw std::invalid_argument("membership movement window is missing or ambiguous");
                for(auto v:bridge.state().outputs()["outputs"].toArray())if(v.toObject()["name"]==window["output"])source=v.toObject();
                if(source.isEmpty())throw std::invalid_argument("source output missing");
                auto target=activity?activityDestination(bridge.state().activities()["activities"].toArray(),arguments[3]):desktopDestination(bridge.state().desktops()["desktops"].toArray(),arguments[3]);bool floated=false;
                for(auto v:contexts.state()["contexts"].toArray()){auto c=v.toObject();for(auto m:c["members"].toArray())if(m.toObject()["uuid"]==window["uuid"])throw std::invalid_argument("float the tiled window before changing its membership directly");if(c["floating"].toArray().contains(window["uuid"]))floated=true;}
                if(!floated||bridge.controlledWindows().contains(window["uuid"].toString()))throw std::invalid_argument("membership movement requires an explicitly floated window and confirmed native release");
                auto snapshot=bridge.state().windows();snapshot["outputs"]=bridge.state().outputs()["outputs"];snapshot["desktops"]=bridge.state().desktops()["desktops"];snapshot["activities"]=bridge.state().activities()["activities"];
                QJsonObject request{{"protocol",1},{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"bridge-instance",snapshot["instance"]},{"window",window["uuid"]},{activity?"activity":"desktop",target["id"]},{"source-output",source},{"expected-desktops",window["desktops"]},{"expected-activities",window["activities"]},{"expected-frame",window["frame"]}};
                if(activity)validateActivityMove(request,snapshot);else validateDesktopMove(request,snapshot);auto receipt=bridge.submitDesktopMove(request,activity);receipt["confirmed-window-id"]=window["id"];receipt[activity?"confirmed-activity":"confirmed-desktop"]=target["id"];
                reply=response(id,0,QString::fromUtf8(QJsonDocument(receipt).toJson()),"","");
            }
            else if(arguments.size()==4&&arguments[0]=="context"&&arguments[1]=="send-output") {
                if(!enableControl)return response(id,3,"","output movement requires --enable-control\n","control_disabled");
                if(!bridge.state().hasOutputMove()||!bridge.eventStatus()["ready"].toBool())throw std::invalid_argument("output movement requires output-move and ready events; run scripts/reload-effect.sh");
                if(bridge.geometryBusy())throw std::invalid_argument("another geometry operation is unresolved; wait for confirmation");
                QJsonObject window,source,destination;int windows=0;
                for(auto v:bridge.state().windows()["windows"].toArray()) {auto w=v.toObject();if((arguments[2]=="focused"&&w["focused"].toBool())||w["id"]==QJsonValue(arguments[2])||w["uuid"]==QJsonValue(arguments[2])){window=w;++windows;}}
                if(windows!=1)throw std::invalid_argument("move window selector is missing or ambiguous");
                for(auto v:bridge.state().outputs()["outputs"].toArray())if(v.toObject()["name"]==window["output"])source=v.toObject();
                if(source.isEmpty())throw std::invalid_argument("source output is missing");
                destination=outputDestination(bridge.state().outputs()["outputs"].toArray(),source,arguments[3]);
                const auto uuid=window["uuid"].toString();bool floated=false;
                for(auto v:contexts.state()["contexts"].toArray()) {
                    auto c=v.toObject();for(auto m:c["members"].toArray())if(m.toObject()["uuid"]==QJsonValue(uuid))throw std::invalid_argument("float the tiled window before sending it to another output");
                    if(c["floating"].toArray().contains(uuid))floated=true;
                }
                if(!floated||bridge.controlledWindows().contains(uuid))throw std::invalid_argument("output movement requires an explicitly floated window and confirmed release from native tiling");
                auto snapshot=bridge.state().windows();snapshot["outputs"]=bridge.state().outputs()["outputs"];
                auto request=outputMoveRequest(window,source,destination,snapshot,QUuid::createUuid().toString(QUuid::WithoutBraces));validateGeometryRequest(request,snapshot);
                auto receipt=bridge.submitGeometry(request);receipt["confirmed-output"]=destination["name"];receipt["confirmed-window-id"]=window["id"];
                reply=response(id,0,QString::fromUtf8(QJsonDocument(receipt).toJson()),"","");
            }
            else if(arguments.size()==3&&arguments[0]=="bridge"&&arguments[1]=="focus-status") {
                reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.focusStatus(arguments[2])).toJson()),"","");
            }
            else if(arguments.size()>=2&&arguments[0]=="bridge"&&arguments[1]=="plan") {
                if(!bridge.state().hasContexts())reply=response(id,3,"",(bridge.state().connected()?QString("loaded bridge lacks contexts; run scripts/reload-effect.sh and restart the observation daemon\n"):QString("bridge disconnected: ")+bridge.state().status().value("error").toString()+"\n"),"contexts_unavailable");
                else {
                    if(arguments.size()<4)throw std::invalid_argument("bridge plan OUTPUT WINDOW_ID [WINDOW_ID...]");
                    reply=response(id,0,QString::fromUtf8(QJsonDocument(dryRunPlan(bridge.state(),arguments[2],arguments.mid(3))).toJson()),"","");
                }
            }
            else if(arguments.size()>=2&&((arguments[0]=="context"&&arguments[1]=="restore")||(arguments[0]=="bridge"&&arguments[1]=="restore")))throw std::invalid_argument("layout restoration was removed to match yabai; context reset releases stale ownership without moving windows");
            else if(arguments.size()>=2&&arguments[0]=="bridge"&&(arguments[1]=="apply"||arguments[1]=="transaction")) {
                if(arguments[1]=="transaction") {
                    if(arguments.size()!=3)throw std::invalid_argument("bridge transaction ID");
                    reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.transaction(arguments[2])).toJson()),"","");
                } else {
                    if(!enableControl||!bridge.state().hasGeometry())return response(id,3,"","geometry control requires --enable-control and the updated effect\n","control_disabled");
                    if(arguments.size()<4)throw std::invalid_argument("bridge apply OUTPUT WINDOW_ID [WINDOW_ID...]");
                    auto plan=dryRunPlan(bridge.state(),arguments[2],arguments.mid(3));
                    auto request=geometryRequest(plan,bridge.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces));
                    reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.submitGeometry(request)).toJson()),"","");
                }
            }
            else if(arguments.size()==3&&(arguments[0]=="compat-space"||arguments[0]=="compat-ratio"||arguments[0]=="compat-split"||arguments[0]=="compat-zoom"||arguments[0]=="compat-resize"||arguments[0]=="compat-swap")) {
                if(!enableControl||!bridge.state().hasGeometry())return response(id,3,"","layout control requires --enable-control\n","control_disabled");
                if(!bridge.state().hasNoRestore())throw std::invalid_argument("run scripts/reload-effect.sh to update the effect");
                const bool zoom=arguments[0]=="compat-zoom",space=arguments[0]=="compat-space",ratio=arguments[0]=="compat-ratio",split=arguments[0]=="compat-split",edges=arguments[0]=="compat-resize";
                if(zoom&&!bridge.state().hasGeometryZoom())throw std::invalid_argument("zoom-fullscreen requires the updated effect; run scripts/reload-effect.sh");
                QString source=arguments[1],target=arguments[2],output;QJsonObject selected;int matches=0;
                for(auto value:bridge.state().windows()["windows"].toArray()) {auto w=value.toObject();if(((space||source=="focused")&&w["focused"].toBool())||(!space&&source!="focused"&&w["id"]==QJsonValue(source))){selected=w;++matches;}}
                if(matches!=1)throw std::invalid_argument("layout source is missing or ambiguous");
                output=selected["output"].toString();if(!space)source=selected["id"].toString();
                auto context=contexts.show(bridge.state(),output);auto status=bridge.contextStatus(context["id"].toString());auto active=status["active"].toObject();
                if(status["active-count"].toInt()!=1||active["state"]!="applied"||!active["success"].toBool()||active["context-generation"]!=context["generation"]||active["bridge-instance"]!=bridge.state().windows()["instance"])throw std::invalid_argument("layout editing requires a confirmed applied context at the current generation");
                auto previous=contexts.plan(bridge.state(),output);
                if(previous["work-area"]!=active["target-work-area"])throw std::invalid_argument("work area changed; reconcile the context before editing");
                for(auto v:active["target-placements"].toArray()) {auto p=v.toObject();QJsonObject actual;for(auto w:bridge.state().windows()["windows"].toArray())if(w.toObject()["uuid"]==p["uuid"])actual=w.toObject()["frame"].toObject();auto expected=p["frame"].toObject();for(auto field:{"x","y","w","h"})if(!actual.contains(field)||std::abs(actual[field].toDouble()-expected[field].toDouble())>1)throw std::invalid_argument("window moved after the applied layout; reset the context before editing");}
                auto candidate=contexts;QStringList edit;
                if(space) {
                    if(source=="gap"||source=="padding") {
                        const bool gap=source=="gap";auto parsed=compat::parseSpacing(target,gap?1:4);QList<double> current;
                        if(gap)current={context["gap"].toDouble()};else {auto p=context["padding"].toArray();current={p[0].toDouble(),p[2].toDouble(),p[3].toDouble(),p[1].toDouble()};}
                        auto values=compat::resolveSpacing(parsed,current);edit={source,output};
                        if(values==current)edit.clear();
                        else if(gap)edit.append(QString::number(values[0],'g',17));else for(auto index:{0,3,1,2})edit.append(QString::number(values[index],'g',17));
                    } else if(source=="rotate")edit={"rotate",output,target=="90"?"270":target=="270"?"90":target};
                    else if(source=="mirror"){if(target!="x-axis"&&target!="y-axis")throw std::invalid_argument("expected x-axis or y-axis");edit={"mirror-slots",output,target=="x-axis"?"y":"x"};}
                    else if(source=="equalize"){if(target!="both"&&target!="x-axis"&&target!="y-axis")throw std::invalid_argument("invalid equalize axis");edit={"equalize",output};if(target!="both")edit.append(target=="x-axis"?"y":"x");}
                    else throw std::invalid_argument("unsupported space operation");
                } else if(zoom){if(target!="zoom-fullscreen")throw std::invalid_argument("expected zoom-fullscreen");edit={"zoom-fullscreen",output,source};}
                else if(split){if(target!="split")throw std::invalid_argument("expected split");edit={"toggle-split",output,source};}
                else if(edges){auto resize=compat::parseEdgeResize(target);edit={"resize-edges",output,source,resize.handle,QString::number(resize.dx,'g',17),QString::number(resize.dy,'g',17)};}
                else if(ratio){auto parts=target.split(':');if(parts.size()!=2)throw std::invalid_argument("expected abs:VALUE or rel:DELTA");edit={"parent-ratio",output,source,parts[0],parts[1]};}
                else {if(QStringList{"north","east","south","west"}.contains(target))target=contexts.neighbor(bridge.state(),output,source,target)["id"].toString();if(source==target)throw std::invalid_argument("cannot swap a window with itself");edit={"swap",output,source,target};}
                if(!edit.isEmpty())candidate.edit(bridge.state(),edit);
                auto plan=candidate.plan(bridge.state(),output);
                for(auto value:plan["placements"].toArray())if(!value.toObject()["visible"].toBool())throw std::invalid_argument("layout edit does not support hidden stacks");
                auto request=geometryRequest(plan,bridge.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),active);request["supersedes"]=active["id"];
                QJsonObject receipt=active;
                if(candidate.state()!=contexts.state()) {
                    if(bridge.geometryBusy())throw std::invalid_argument("another geometry operation is unresolved; wait for confirmation");
                    save(contextPath,candidate.state());contexts=std::move(candidate);
                    try {receipt=bridge.submitGeometry(request);manager.adopt(context["id"].toString(),receipt);}
                    catch(const std::exception &e){manager.stop(context["id"].toString(),QString::fromUtf8(e.what()));throw;}
                }
                receipt["confirmed-placements"]=plan["placements"];
                reply=response(id,0,QString::fromUtf8(QJsonDocument(receipt).toJson()),"","");
            }
            else if(arguments.size()==3&&arguments[0]=="compat-grid")reply=response(id,0,QString::fromUtf8(QJsonDocument(grids.command(arguments[1],arguments[2])).toJson()),"","");
            else if(arguments.size()==4&&arguments[0]=="compat-scratchpad") {
                if(!enableControl)throw std::invalid_argument("scratchpads require --enable-control");
                QJsonObject selected;const auto op=arguments[1];
                if(op=="assign"||op=="clear"){
                    int matches=0;for(auto v:bridge.state().windows()["windows"].toArray()){auto w=v.toObject();if((arguments[2]=="focused"&&w["focused"].toBool())||w["id"]==QJsonValue(arguments[2])){selected=w;++matches;}}
                    if(matches!=1)throw std::invalid_argument("scratchpad window selector missing or ambiguous");
                }
                if(op=="assign"&&bridge.controlledWindows().contains(selected["uuid"].toString())){
                    auto context=contexts.show(bridge.state(),selected["output"].toString());auto status=manager.status(context["id"].toString());
                    if(!status["enabled"].toBool()||!status["automatic-membership"].toBool())throw std::invalid_argument("assigning a tiled scratchpad requires BSP lifecycle management; set config layout bsp first");
                }
                auto receipt=bridge.submitScratchpad({{"operation",op},{"window",selected["uuid"]},{"label",arguments[3]}});
                if(op=="assign"||op=="clear")scratchpads.overrideWindow(selected["uuid"].toString());
                reply=response(id,0,QString::fromUtf8(QJsonDocument(receipt).toJson()),"","");
            }
            else if(arguments.size()==3&&arguments[0]=="bridge"&&arguments[1]=="scratchpad-status")reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.scratchpadStatus(arguments[2])).toJson()),"","");
            else if(arguments.size()==3&&arguments[0]=="compat-layer")reply=response(id,0,QString::fromUtf8(QJsonDocument(layers.command(arguments[1],arguments[2])).toJson()),"","");
            else if(arguments.size()==3&&arguments[0]=="bridge"&&arguments[1]=="layer-status")reply=response(id,0,QString::fromUtf8(QJsonDocument(bridge.layerStatus(arguments[2])).toJson()),"","");
            else if(arguments==QStringList{"compat-windows"}) {
                if(!bridge.state().connected())throw std::invalid_argument("window query requires a connected bridge");
                QJsonArray result;auto controlled=bridge.controlledWindows();
                for(auto v:bridge.state().windows()["windows"].toArray()) {
                    auto w=v.toObject();bool ok=false;auto numeric=w["id"].toString().toULongLong(&ok);if(!ok||numeric>9007199254740991ULL)throw std::invalid_argument("window ID cannot be represented exactly in query JSON");
                    bool admitted=controlled.contains(w["uuid"].toString());
                    result.append(QJsonObject{{"id",double(numeric)},{"app",w["app"]},{"title",w["title"]},{"frame",w["frame"]},{"scratchpad",w.value("scratchpad").toString()},{"scratchpad-hidden",w.value("scratchpad-hidden").toBool()},{"is-modal",w.value("is-modal").toBool()},{"is-transient",w.value("is-transient").toBool()},{"has-focus",w["focused"]},{"is-floating",!admitted},{"is-minimized",w["minimized"]},{"is-native-fullscreen",w["fullscreen"]},{"has-fullscreen-zoom",contexts.isZoomed(w["uuid"].toString())},{"layer",w.value("layer").toString("normal")},{"sub-layer",w.value("layer").toString("normal")},{"kwabai-output",w["output"]}});
                }
                reply=response(id,0,QString::fromUtf8(QJsonDocument(result).toJson()),"","");
            }
            else if(arguments[0]=="rule") {
                if(!observeKwin)throw std::invalid_argument("rules require --observe-kwin");
                QJsonObject result;
                if(arguments==QStringList{"rule","list-posix"}) {reply=response(id,0,QString::fromUtf8(QJsonDocument(contexts.rulesPosix()).toJson()),"","");}
                else if(arguments==QStringList{"rule","list"})result=contexts.rules();
                else if(arguments.size()==3&&arguments[1]=="test")result=contexts.testRule(bridge.state(),arguments[2]);
                else {if(arguments.size()==9&&arguments[1]=="add-posix"&&!arguments[8].isEmpty()&&(!enableControl||!bridge.state().hasFloatingGrid()))throw std::invalid_argument("grid rules require --enable-control and the updated effect; run scripts/reload-effect.sh");if(arguments.size()>=8&&arguments[1]=="add-posix"&&!arguments[7].isEmpty()&&(!enableControl||!bridge.state().hasScratchpads()))throw std::invalid_argument("scratchpad rules require --enable-control and the updated effect; run scripts/reload-effect.sh");if(arguments.size()>=7&&arguments[1]=="add-posix"&&!arguments[6].isEmpty()&&(!enableControl||!bridge.state().hasLayers()))throw std::invalid_argument("layer rules require --enable-control and the updated effect; run scripts/reload-effect.sh");auto candidate=contexts;candidate.editRules(arguments.mid(1));save(contextPath,candidate.state());contexts=std::move(candidate);result=contexts.rules();result["windows-moved"]=false;}
                if(arguments!=QStringList{"rule","list-posix"})reply=response(id,0,QString::fromUtf8(QJsonDocument(result).toJson()),"","");
            }
            else if(arguments[0]=="context") {
                if(arguments==QStringList{"context","managers"})reply=response(id,0,QString::fromUtf8(QJsonDocument(manager.status()).toJson()),"","");
                else if(arguments==QStringList{"context","manage","all","off"}){manager.stop({});reply=response(id,0,QString::fromUtf8(QJsonDocument(manager.status()).toJson()),"","");}
                else if(arguments.size()==4&&arguments[1]=="manage-id"&&arguments[3]=="off"){manager.stop(arguments[2]);reply=response(id,0,QString::fromUtf8(QJsonDocument(manager.status(arguments[2])).toJson()),"","");}
                else if(arguments==QStringList{"context","list"})reply=response(id,0,QString::fromUtf8(QJsonDocument(contexts.list()).toJson()),"","");
                else {
                    if(arguments.size()<3)throw std::invalid_argument("context OPERATION OUTPUT ...");
                    auto op=arguments[1];QJsonObject result;
                    if(op=="float") {
                        if(arguments.size()!=5||(arguments[4]!="on"&&arguments[4]!="off"))throw std::invalid_argument("context float OUTPUT WINDOW on|off");
                        if(!enableControl)return response(id,3,"","floating control requires --enable-control\n","control_disabled");
                        if(!bridge.state().hasNoRestore())throw std::invalid_argument("run scripts/reload-effect.sh before floating windows");
                        if(!bridge.state().hasGeometryRelease()||!bridge.eventStatus()["ready"].toBool())throw std::invalid_argument("floating control requires geometry-release and ready events; run scripts/reload-effect.sh");
                        auto context=contexts.show(bridge.state(),arguments[2]);auto control=bridge.contextStatus(context["id"].toString());auto active=control["active"].toObject();
                        if(!manager.status(context["id"].toString())["paused-windows"].toArray().isEmpty())throw std::invalid_argument("restore minimized/fullscreen windows before changing floating state in this paused layout");
                        if(control["active-count"].toInt()>1||(!active.isEmpty()&&(active["state"]!="applied"||!active["success"].toBool()||active["bridge-instance"]!=bridge.state().windows()["instance"])))throw std::invalid_argument("finish the active transaction or reset the context before changing floating state");
                        auto candidate=contexts;candidate.edit(bridge.state(),arguments.mid(1));auto plan=candidate.plan(bridge.state(),arguments[2],true);
                        for(auto v:plan["placements"].toArray())if(!v.toObject()["visible"].toBool())throw std::invalid_argument("floating control does not support hidden stacks");
                        auto request=geometryRequest(plan,bridge.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),active);
                        bool changed=candidate.state()!=contexts.state();
                        if(!changed&&(active.isEmpty()||active["context-generation"]==plan["context-generation"])) {result=candidate.show(bridge.state(),arguments[2]);result["unchanged"]=true;}
                        else {
                            if(bridge.geometryBusy())throw std::invalid_argument("another geometry operation is unresolved; wait for confirmation");
                            if(changed){save(contextPath,candidate.state());contexts=std::move(candidate);}
                            if(!active.isEmpty()||arguments[4]=="off") {
                                if(!active.isEmpty())request["supersedes"]=active["id"];
                                request["lifecycle"]=true;
                                try {result=bridge.submitGeometry(request);manager.adopt(context["id"].toString(),result);}
                                catch(const std::exception &e){manager.stop(context["id"].toString(),QString::fromUtf8(e.what()));throw;}
                            } else {result=contexts.show(bridge.state(),arguments[2]);result["state"]="saved";}
                        }
                    }
                    else if(op=="focus"||op=="focus-window") {
                        if(arguments.size()!=4)throw std::invalid_argument("context focus OUTPUT DIRECTION or context focus-window OUTPUT ID|focused");
                        if(!enableControl)return response(id,3,"","focus requires --enable-control\n","control_disabled");
                        bridge.validateFocusReady();
                        auto context=contexts.show(bridge.state(),arguments[2]);auto status=bridge.contextStatus(context["id"].toString());auto active=status["active"].toObject();
                        if(status["active-count"].toInt()!=1||active["state"]!="applied"||!active["success"].toBool()||active["context-generation"]!=context["generation"]||active["bridge-instance"]!=bridge.state().windows()["instance"])throw std::invalid_argument("focus requires a confirmed applied context at the current generation");
                        auto plan=contexts.plan(bridge.state(),arguments[2]);QJsonObject neighbor;
                        if(op=="focus")neighbor=contexts.neighbor(bridge.state(),arguments[2],"focused",arguments[3]);
                        else {
                            auto target=arguments[3];bool numeric=false;auto number=target.toULongLong(&numeric);
                            if(target!="focused"&&(!numeric||number==0||number>9007199254740991ULL||QString::number(number)!=target))throw std::invalid_argument("focus target must be a numeric window ID");
                            QJsonObject source;int focused=0;
                            for(auto value:bridge.state().windows()["windows"].toArray())if(value.toObject()["focused"].toBool()){source=value.toObject();++focused;}
                            const auto sourceUuid=source["uuid"].toString();
                            if(focused!=1)throw std::invalid_argument("focused window is missing or ambiguous");
                            for(auto value:plan["placements"].toArray()) {
                                auto placement=value.toObject();if(!placement["visible"].toBool())continue;
                                if(placement["uuid"]==QJsonValue(sourceUuid))source=placement;
                                if((target=="focused"&&placement["uuid"]==QJsonValue(sourceUuid))||placement["id"]==QJsonValue(target))neighbor=placement;
                            }
                            if(neighbor.isEmpty())throw std::invalid_argument("focus target must be a visible member of the selected context");
                            neighbor["source"]=source;
                        }
                        auto request=focusRequest(neighbor,plan,bridge.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces));
                        if(request["source"]==request["target"])result={{"state","focused"},{"completed",true},{"success",true},{"acknowledged",true},{"unchanged",true},{"bridge-instance",request["bridge-instance"]},{"source",request["source"]},{"target",request["target"]}};
                        else {auto snapshot=bridge.state().windows();snapshot["outputs"]=bridge.state().outputs()["outputs"];validateFocusRequest(request,snapshot);result=bridge.submitFocus(request);}
                        result["confirmed-focus-id"]=neighbor["id"];
                    }
                    else if(op=="neighbor") {
                        if(arguments.size()!=5)throw std::invalid_argument("context neighbor OUTPUT SOURCE DIRECTION");
                        result=contexts.neighbor(bridge.state(),arguments[2],arguments[3],arguments[4]);
                    }
                    else if(op=="manage") {
                        if((arguments.size()!=4&&!(arguments.size()==5&&arguments[3]=="on"&&arguments[4]=="--lifecycle"))||(arguments[3]!="on"&&arguments[3]!="off"))throw std::invalid_argument("context manage OUTPUT on [--lifecycle]|off");
                        auto context=contexts.show(bridge.state(),arguments[2]);auto contextId=context["id"].toString();
                        if(arguments[3]=="on") {
                            if(!enableControl)return response(id,3,"","context management requires --enable-control\n","control_disabled");
                            result=manager.enable(arguments[2],arguments.size()==5);
                        } else {manager.stop(contextId);result=manager.status(contextId);}
                    }
                    else if(op=="geometry") {
                        if(arguments.size()!=3)throw std::invalid_argument("context geometry OUTPUT");
                        result=geometryDiagnostics(bridge,contexts,arguments[2]);
                    }
                    else if(op=="reset") {
                        if(arguments.size()!=3)throw std::invalid_argument("context reset OUTPUT|all");
                        if(!bridge.state().hasContexts()||!bridge.eventStatus()["ready"].toBool()||bridge.geometryBusy())throw std::invalid_argument("context reset requires a connected settled bridge");
                        QStringList outputs;if(arguments[2]=="all")for(auto v:bridge.state().outputs()["outputs"].toArray())outputs<<v.toObject()["id"].toString();else outputs<<arguments[2];
                        auto candidate=contexts;candidate.reset(bridge.state(),outputs);
                        QJsonObject active;
                        for(auto output:outputs){QJsonObject old;try{old=contexts.show(bridge.state(),output);}catch(const std::invalid_argument &){}if(old.isEmpty())continue;auto status=bridge.contextStatus(old["id"].toString());if(status["active-count"].toInt()>1)throw std::invalid_argument("multiple active native layouts; inspect context status before resetting");if(!status["active"].toObject().isEmpty())active=status["active"].toObject();}
                        if(!active.isEmpty()&&!bridge.state().hasGeometryForget())throw std::invalid_argument("reload the native effect before resetting an owned layout");
                        for(auto output:outputs){try{manager.stop(contexts.show(bridge.state(),output)["id"].toString());}catch(const std::invalid_argument &){}}
                        if(!active.isEmpty()){result=bridge.forgetGeometry(active["id"].toString());result["reset-pending"]=true;}
                        else {save(contextPath,candidate.state());contexts=std::move(candidate);QJsonArray rebuilt;for(auto output:outputs)rebuilt.append(contexts.show(bridge.state(),output));result={{"state","reset"},{"completed",true},{"success",true},{"windows-moved",false},{"contexts",rebuilt}};}
                    }
                    else if(op=="reconcile") {
                        if(arguments.size()!=3&&!(arguments.size()==4&&arguments[3]=="--apply"))throw std::invalid_argument("context reconcile OUTPUT [--apply]");
                        result=contexts.reconcile(bridge.state(),arguments[2]);result["windows-moved"]=false;
                        if(arguments.size()==4) {
                            manager.validateEdit(arguments[2],"reconcile");
                            auto candidate=contexts;candidate.edit(bridge.state(),{"reconcile",arguments[2]});
                            save(contextPath,candidate.state());contexts=std::move(candidate);
                            result["dry-run"]=false;result["applied"]=true;result["context"]=contexts.show(bridge.state(),arguments[2]);
                        }
                    }
                    else if(op=="apply"||op=="status") {
                        if(arguments.size()!=3)throw std::invalid_argument("context apply/status OUTPUT");
                        auto context=contexts.show(bridge.state(),arguments[2]);auto status=bridge.contextStatus(context["id"].toString());
                        status["management"]=manager.status(context["id"].toString());status["history"]=context["history"];status["stored-generation"]=context["generation"];status["output"]=context["output"];status["desktop-control"]=enableControl&&bridge.state().hasGeometry();
                        if(op=="status")result=status;
                        else {
                            if(!enableControl||!bridge.state().hasGeometry())return response(id,3,"","context geometry requires --observe-kwin --enable-control\n","control_disabled");
                            if(status["active-count"].toInt()>1)throw std::invalid_argument("multiple active context transactions; inspect bridge transactions before proceeding");
                            auto active=status["active"].toObject();
                            {
                                if(!active.isEmpty()&&active["bridge-instance"]!=bridge.state().windows()["instance"])throw std::invalid_argument("context transaction belongs to a replaced bridge; reset ownership before applying");
                                if(!active.isEmpty()&&(active["state"]!="applied"||!active["success"].toBool()))throw std::invalid_argument("context has an unfinished or failed transaction; inspect context status and reset before applying");
                                if(!active.isEmpty()&&active["context-generation"]==context["generation"]){result=active;result["unchanged"]=true;}
                                else {
                                    auto plan=contexts.plan(bridge.state(),arguments[2],!active.isEmpty()&&active["managed-windows"].toArray().isEmpty());
                                    for(auto v:plan["placements"].toArray())if(!v.toObject()["visible"].toBool())throw std::invalid_argument("stack visibility is not supported by the geometry bridge");
                                    auto request=geometryRequest(plan,bridge.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),active);
                                    if(!active.isEmpty())request["supersedes"]=active["id"];
                                    if(plan["placements"].toArray().isEmpty())request["lifecycle"]=true;
                                    result=bridge.submitGeometry(request);manager.adopt(context["id"].toString(),result);
                                }
                            }
                        }
                    }
                    else if(op=="show"||op=="plan"||op=="commit"||op=="update") {
                        if(arguments.size()!=(op=="update"?4:3))throw std::invalid_argument("context show/plan/commit OUTPUT or context update OUTPUT TRANSACTION_ID");
                        if(op=="show")result=contexts.show(bridge.state(),arguments[2]);
                        else {
                            auto plan=contexts.plan(bridge.state(),arguments[2]);
                            if(op=="plan")result=plan;
                            else {
                                if(!enableControl||!bridge.state().hasGeometry())return response(id,3,"","context commit requires explicit geometry control\n","control_disabled");
                                for(auto v:plan["placements"].toArray())if(!v.toObject()["visible"].toBool())throw std::invalid_argument("stack visibility is not supported by the geometry bridge; unstack before committing");
                                auto request=geometryRequest(plan,bridge.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),op=="update"?bridge.transaction(arguments[3]):QJsonObject{});if(op=="update")request["supersedes"]=arguments[3];result=bridge.submitGeometry(request);result["context-id"]=plan["context-id"];result["context-generation"]=plan["context-generation"];
                            }
                        }
                    } else {manager.validateEdit(arguments[2],op);auto candidate=contexts;candidate.edit(bridge.state(),arguments.mid(1));save(contextPath,candidate.state());contexts=std::move(candidate);result=contexts.show(bridge.state(),arguments[2]);}
                    reply=response(id,0,QString::fromUtf8(QJsonDocument(result).toJson()),"","");
                }
            }
            else if(arguments[0]=="model") {
                if(!simulate)reply=response(id,3,"","model commands require a daemon started with --simulate\n","simulation_required");
                else {
                    if(model.generation==std::numeric_limits<quint64>::max())throw std::invalid_argument("generation exhausted");
                    auto candidate=model;candidate.mutate(arguments.mid(1));++candidate.generation;
                    save(statePath,candidate.state());model=std::move(candidate);
                    reply=response(id,0,"","","");
                }
            } else if(arguments[0]=="-m"||arguments[0]=="--message")reply=response(id,3,"","live window commands unavailable: no acknowledged KWin bridge\n","bridge_unavailable");
            else throw std::invalid_argument("unsupported command; use doctor, inspect or model");
            replay.insert(id,{digest,reply});replayOrder.enqueue(id);
            if(replayOrder.size()>128)replay.remove(replayOrder.dequeue());
            return reply;
        }catch(const std::invalid_argument &e){return response(id,2,"",QString::fromUtf8(e.what())+"\n","invalid_request");}
        catch(const std::exception &e){return response(id,1,"",QString::fromUtf8(e.what())+"\n","state_commit_failed");}
    };
    executeTransfer=[&](const QStringList &arguments) {
        QScopedValueRollback<bool> internal(internalTransfer,true);QJsonArray args;for(auto a:arguments)args.append(a);
        auto reply=dispatch({{"version",1},{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"args",args}});
        if(reply["status"].toInt()!=0)throw std::runtime_error(reply["stderr"].toString().trimmed().toStdString());
        auto parsed=QJsonDocument::fromJson(reply["stdout"].toString().toUtf8());if(!parsed.isObject())throw std::runtime_error("invalid internal transfer stage response");return parsed.object();
    };
    QObject::connect(&server,&QLocalServer::newConnection,&app,[&] {
        while(auto *socket=server.nextPendingConnection()) {
            struct ucred peer{};socklen_t size=sizeof(peer);
            if(active>=32 || getsockopt(int(socket->socketDescriptor()),SOL_SOCKET,SO_PEERCRED,&peer,&size)!=0 || peer.uid!=geteuid()) {socket->abort();socket->deleteLater();continue;}
            ++active;socket->setReadBufferSize(ipc::MaxMessage+5);
            auto state=std::make_shared<Session>();auto *timer=new QTimer(socket);timer->setSingleShot(true);
            QObject::connect(timer,&QTimer::timeout,socket,[socket]{socket->abort();});timer->start(ipc::DeadlineMs);
            QObject::connect(socket,&QLocalSocket::disconnected,&app,[&,socket]{--active;socket->deleteLater();});
            auto consume=[&,socket,state] {
                if(state->done)return;
                state->bytes+=socket->readAll();
                if(state->bytes.size()<4)return;
                auto length=qFromBigEndian<quint32>(state->bytes.constData());
                QJsonObject reply;
                if(!length||length>ipc::MaxMessage)reply=response("",2,"","invalid frame length\n","invalid_frame");
                else if(state->bytes.size()<4+length)return;
                else if(state->bytes.size()!=4+length)reply=response("",2,"","one request per connection\n","invalid_frame");
                else {
                    try{reply=dispatch(ipc::parse(state->bytes.mid(4)));}
                    catch(const std::exception &e){reply=response("",2,"",QString::fromUtf8(e.what())+"\n","invalid_frame");}
                }
                state->done=true;
                try{socket->write(ipc::frame(reply));}
                catch(const std::exception &){socket->write(ipc::frame(response(reply.value("id").toString(),2,"","response exceeds frame limit\n","response_too_large")));}
                std::cerr<<QJsonDocument(QJsonObject{{"event","command"},{"id",reply.value("id")},{"generation",reply.value("generation")},{"status",reply.value("status")},{"error",reply.value("error")}}).toJson(QJsonDocument::Compact).constData()<<'\n';
                socket->disconnectFromServer();
            };
            QObject::connect(socket,&QLocalSocket::readyRead,&app,consume);
            consume();
        }
    });
    int signalPipe[2];if(pipe2(signalPipe,O_NONBLOCK|O_CLOEXEC)!=0)throw std::runtime_error("cannot create signal channel");
    signalWrite=signalPipe[1];struct sigaction action{};action.sa_handler=terminate;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,nullptr);sigaction(SIGINT,&action,nullptr);
    QSocketNotifier notifier(signalPipe[0],QSocketNotifier::Read);
    QObject::connect(&notifier,&QSocketNotifier::activated,&app,[&]{app.quit();});
    std::cout<<QJsonDocument(QJsonObject{{"ready",true},{"socket",socketPath},{"mode",simulate?"simulation":(observeKwin?"observation":"disconnected")}}).toJson(QJsonDocument::Compact).constData()<<std::endl;
    int status=app.exec();
    for(auto *socket:server.findChildren<QLocalSocket *>()) {socket->abort();delete socket;}
    server.close();signalWrite=-1;::close(signalPipe[0]);::close(signalPipe[1]);return status;
 }catch(const std::invalid_argument &e){std::cerr<<"kwabaid: "<<e.what()<<'\n';return 2;}
 catch(const std::exception &e){std::cerr<<"kwabaid: "<<e.what()<<'\n';return 1;}
}
