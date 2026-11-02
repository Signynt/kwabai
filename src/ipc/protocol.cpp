#include "protocol.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <algorithm>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QUuid>
#include <QtEndian>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
namespace kwabai::ipc {
QString defaultSocket() {
    auto configured=qEnvironmentVariable("KWABAI_SOCKET");
    if(!configured.isEmpty()){if(!QDir::isAbsolutePath(configured))throw std::invalid_argument("KWABAI_SOCKET must be absolute");return configured;}
    auto runtime=qEnvironmentVariable("XDG_RUNTIME_DIR");
    if(runtime.isEmpty())throw std::runtime_error("XDG_RUNTIME_DIR is unset; provide --socket in a private directory");
    return runtime+"/kwabai/control.sock";
}
void privateDirectory(const QString &path,bool create) {
    auto encoded=QFile::encodeName(path);struct stat st{};
    if(lstat(encoded.constData(),&st)!=0) {
        if(!create || ::mkdir(encoded.constData(),0700)!=0 || lstat(encoded.constData(),&st)!=0)
            throw std::runtime_error("cannot access private socket/state directory");
    }
    if(!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))
        throw std::runtime_error("socket/state directory must be owned by you with mode 0700");
}
QByteArray frame(const QJsonObject &message) {
    auto payload=QJsonDocument(message).toJson(QJsonDocument::Compact);
    if(payload.size()>MaxMessage)throw std::invalid_argument("IPC message exceeds 1 MiB");
    QByteArray bytes(4, '\0');qToBigEndian<quint32>(quint32(payload.size()),bytes.data());return bytes+payload;
}
QJsonObject parse(const QByteArray &payload) {
    QJsonParseError error;auto doc=QJsonDocument::fromJson(payload,&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject())throw std::invalid_argument("invalid protocol JSON");
    return doc.object();
}
QJsonObject request(const QString &path,const QStringList &args) {
    QLocalSocket socket;QElapsedTimer timer;timer.start();
    auto remaining=[&]{return std::max(1,DeadlineMs-int(timer.elapsed()));};
    socket.connectToServer(path);
    if(!socket.waitForConnected(remaining()))throw std::runtime_error("daemon unavailable: "+socket.errorString().toStdString());
    auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonArray arguments;for(auto &arg:args)arguments.append(arg);
    auto outgoing=frame({{"version",1},{"id",id},{"args",arguments}});
    if(socket.write(outgoing)!=outgoing.size())throw std::runtime_error("cannot queue command");
    while(socket.bytesToWrite())if(timer.elapsed()>=DeadlineMs||!socket.waitForBytesWritten(remaining()))throw std::runtime_error("command write timed out");
    QByteArray bytes;quint32 length=0;
    for(;;) {
        bytes+=socket.readAll();
        if(bytes.size()>=4) {
            length=qFromBigEndian<quint32>(bytes.constData());
            if(!length||length>MaxMessage)throw std::runtime_error("invalid response length");
            if(bytes.size()>=4+length)break;
        }
        if(timer.elapsed()>=DeadlineMs||!socket.waitForReadyRead(remaining()))throw std::runtime_error("daemon response timed out or disconnected");
    }
    if(bytes.size()!=4+length)throw std::runtime_error("unexpected trailing response data");
    auto reply=parse(bytes.mid(4));
    if(reply.value("version")!=QJsonValue(1)||reply.value("id")!=QJsonValue(id)||!reply.value("status").isDouble()
        ||!reply.value("stdout").isString()||!reply.value("stderr").isString())throw std::runtime_error("invalid response envelope");
    int status=reply.value("status").toInt(-1);if(status<0||status>3)throw std::runtime_error("invalid response status");
    return reply;
}
}
