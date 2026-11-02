#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <functional>
#include <optional>
#include <utility>
namespace kwabai {
// Use exported Window setters, then observe the result. WindowRules check
// methods are private ABI even though installed KWin headers declare them.
bool setLayerFlags(const QString &layer,
    const std::function<std::optional<std::pair<bool,bool>>()> &read,
    const std::function<void(bool)> &above,const std::function<void(bool)> &below);
class LayerRequests {
public:
    using Read=std::function<QString(const QString &)>;
    using Write=std::function<bool(const QString &,const QString &)>;
    using Managed=std::function<bool(const QString &)>;
    using Priority=std::function<int(const QString &)>; // 0 floating/tiled, 1 zoom, 2 scratchpad
    using Stack=std::function<QStringList()>; // bottom to top, visible windows
    using Restack=std::function<bool(const QString &,const QString &)>;
    LayerRequests(Read read,Write write,Managed managed,Priority priority={},Stack stack={},Restack restack={}):read_(std::move(read)),write_(std::move(write)),managed_(std::move(managed)),priority_(std::move(priority)),stack_(std::move(stack)),restack_(std::move(restack)){}
    QJsonObject submit(const QString &owner,const QJsonObject &request,const QJsonObject &snapshot);
    QJsonObject describe(const QString &uuid) const;
    bool tick();
    void release(const QString &owner);
    void teardown();
private:
    struct Entry {QString owner,mode,baseline,last,error;bool suppressed=false;};
    struct Job {QString owner;QByteArray digest;QJsonObject result;};
    QMap<QString,Entry> entries_;
    QMap<QString,Job> jobs_;
    QStringList order_;
    QMap<QString,quint64> sequences_;
    Read read_;Write write_;Managed managed_;Priority priority_;Stack stack_;Restack restack_;
    QString automatic(const QString &uuid,const QString &baseline) const;
    bool orderPriorities();
};
}
