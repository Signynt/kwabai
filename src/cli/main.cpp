#include "tree.h"
#include "json.h"
#include "protocol.h"
#include "client.h"
#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <unistd.h>
#include <vector>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);auto args=app.arguments();
    const char *usage="Usage: kwabai layout-demo [--portrait] [--save FILE] [--load FILE]\nDaemon: kwabai [--socket PATH] doctor|inspect|rule list|rule test ID|rule add LABEL float/tile --app GLOB [--title GLOB]|rule remove LABEL|context OPERATION OUTPUT...|context undo OUTPUT|context redo OUTPUT|context padding OUTPUT PIXELS|context padding OUTPUT EDGE PIXELS|context padding OUTPUT TOP RIGHT BOTTOM LEFT|context resize OUTPUT SOURCE x/y DELTA|context balance OUTPUT [x/y]|context rotate OUTPUT 90/180/270|context mirror OUTPUT x/y|context neighbor OUTPUT SOURCE DIRECTION|context focus OUTPUT DIRECTION|bridge activities|bridge activity-transaction ID|context send-activity WINDOW ACTIVITY_UUID|context transfer-activity WINDOW ACTIVITY_UUID|bridge desktops|bridge desktop-transaction ID|context send-desktop WINDOW DESKTOP|context transfer-output WINDOW OUTPUT|context transfer-desktop WINDOW DESKTOP|context transfer-status ID|context send-output WINDOW OUTPUT|context float OUTPUT WINDOW on/off|bridge focus-status ID|context managers|context manage all off|context manage-id ID off|context manage OUTPUT on [--lifecycle]|off|context apply OUTPUT|context status OUTPUT|context geometry OUTPUT|context update OUTPUT TRANSACTION_ID|context reset OUTPUT|all|context reconcile OUTPUT [--apply]|bridge apply OUTPUT ID [ID]|bridge transaction ID|bridge events|bridge status|bridge windows|bridge outputs|bridge plan OUTPUT ID...|model OPERATION...\nService: kwabai --install-service|--start-service|--stop-service|--restart-service|--uninstall-service|--service-status\nConfiguration startup: kwabai -c FILE\nScratchpads: kwabai -m window [ID] --scratchpad [LABEL|recover]; kwabai -m window --toggle LABEL; kwabai -m rule --add app=REGEX scratchpad=LABEL\nUse -m/--message for the supported yabai-compatible command subset. Live control requires an enabled daemon.\n";
    QString socketPath;
    if(args.size()>1 && args[1]=="--socket") {
        if(args.size()<4){std::cerr<<"kwabai: --socket needs PATH and command\n";return 2;}
        socketPath=args[2];args.removeAt(1);args.removeAt(1);
    }
    if(args.size()==1||args[1]=="--help") {std::cout<<usage;return 0;}
    try {
        const QStringList services{"--install-service","--uninstall-service","--start-service","--stop-service","--restart-service","--service-status"};
        if(services.contains(args[1])||args[1]=="--config"||args[1]=="-c") {
            if(!socketPath.isEmpty())throw std::invalid_argument("service/config startup uses the default socket; use start-session.sh for custom sessions");
            const bool config=args[1]=="--config"||args[1]=="-c";
            if(args.size()!=(config?3:2))throw std::invalid_argument("unexpected service/config arguments");
            const auto bin=QCoreApplication::applicationDirPath();const QString script=config?"start-session.py":"service.py";
            QString helper=QDir(bin).absoluteFilePath("../libexec/kwabai/"+script);
            if(!QFileInfo::exists(helper))helper=QString(KWABAI_HELPER_SOURCE_DIR)+"/"+script;
            if(!QFileInfo::exists(helper))throw std::runtime_error("session helper is missing from this installation");
            QStringList options{helper};
            if(config)options<<"--enable-control"<<"--config"<<args[2];
            else options<<(args[1]=="--service-status"?"status":args[1].mid(2).section('-',0,0));
            options<<"--build-dir"<<bin;
            QList<QByteArray> command{"python3"};for(const auto &option:options)command.append(option.toLocal8Bit());
            std::vector<char *> argv;for(auto &part:command)argv.push_back(part.data());argv.push_back(nullptr);
            ::execvp(argv[0],argv.data());throw std::runtime_error("could not execute session helper");
        }
        if(args[1]=="-m"||args[1]=="--message") {
            auto result=kwabai::compat::run(args.mid(2),[&](const QStringList &command){return kwabai::ipc::request(socketPath.isEmpty()?kwabai::ipc::defaultSocket():socketPath,command);});
            std::cout<<result.output.toStdString();std::cerr<<result.error.toStdString();return result.status;
        }
        if(args[1]!="layout-demo") {
            if(args[1]!="rule"&&args[1]!="context"&&args[1]!="bridge"&&args[1]!="doctor"&&args[1]!="inspect"&&args[1]!="model"&&args[1]!="-m"&&args[1]!="--message")throw std::invalid_argument("command not implemented; use --help");
            auto call=[&](const QStringList &command){return kwabai::ipc::request(socketPath.isEmpty()?kwabai::ipc::defaultSocket():socketPath,command);};
            auto reply=call(args.mid(1));
            if(args.mid(1).size()==3&&args[1]=="context"&&args[2]=="reset")for(int pass=0;pass<64&&reply["status"].toInt()==0;++pass){
                auto reset=QJsonDocument::fromJson(reply["stdout"].toString().toUtf8()).object();if(!reset["reset-pending"].toBool())break;
                auto transaction=reset["id"].toString();QElapsedTimer deadline;deadline.start();
                do {if(deadline.elapsed()>5000)throw std::runtime_error("context reset ownership release timed out; inspect context status");QThread::msleep(20);auto status=call({"bridge","transaction",transaction});if(status["status"].toInt()!=0)throw std::runtime_error(status["stderr"].toString().toStdString());reset=QJsonDocument::fromJson(status["stdout"].toString().toUtf8()).object();}while(!reset["completed"].toBool());
                if(!reset["success"].toBool()||reset["state"]!="forgotten")throw std::runtime_error(("context reset could not release ownership: "+reset["error"].toString()).toStdString());
                reply=call(args.mid(1));
            }
            std::cout<<reply.value("stdout").toString().toStdString();std::cerr<<reply.value("stderr").toString().toStdString();return reply.value("status").toInt();
        }
        bool portrait=false;QString save,load;
        for(qsizetype i=2;i<args.size();++i) {
            if(args[i]=="--portrait")portrait=true;
            else if(args[i]=="--save" || args[i]=="--load") {
                auto key=args[i];if(++i>=args.size())throw std::invalid_argument("file argument missing");
                auto &destination=key=="--save"?save:load;
                if(!destination.isEmpty())throw std::invalid_argument("duplicate file option");
                if(args[i].isEmpty() || args[i].startsWith("--"))throw std::invalid_argument("file argument missing");
                destination=args[i];
            } else throw std::invalid_argument("unknown option");
        }
        kwabai::Tree tree;
        if(!load.isEmpty()) {
            QFile f(load);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("cannot open snapshot");
            auto bytes=f.read(1024*1024+1);tree=kwabai::decodeSnapshot(bytes);
        } else {
            tree.add(1);tree.add(2,1,kwabai::Direction::East,0.6);
            tree.add(3,1,kwabai::Direction::South,0.55);
            tree.add(4,2,kwabai::Direction::South,0.5);
            tree.add(5,3,kwabai::Direction::East,0.65);
        }
        auto area=portrait?kwabai::Rect{-1080,0,1080,1920}:kwabai::Rect{0,0,2560,1440};
        auto placements=tree.layout(area);
        if(!save.isEmpty()) {
            QSaveFile file(save);if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("cannot open snapshot destination");
            auto bytes=kwabai::encodeSnapshot(tree);
            if(file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("snapshot save failed");
        }
        QJsonArray windows;
        for(auto p:placements)windows.append(QJsonObject{{"id",QString::number(qulonglong(p.window))},{"leaf",QString::number(qulonglong(p.leaf))},
            {"frame",QJsonObject{{"x",p.frame.x},{"y",p.frame.y},{"w",p.frame.width},{"h",p.frame.height}}},{"visible",p.visible}});
        std::cout<<QJsonDocument(QJsonObject{{"mode","offline-demo"},{"windows",windows}}).toJson(QJsonDocument::Indented).constData();
        return 0;
    }catch(const std::invalid_argument &e){std::cerr<<"kwabai: "<<e.what()<<'\n';return 2;}
    catch(const std::exception &e){std::cerr<<"kwabai: "<<e.what()<<'\n';return 1;}
}
