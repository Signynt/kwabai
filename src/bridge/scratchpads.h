#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <functional>
namespace kwabai {
// Labels belong to the compositor session, surviving daemon reconnects.
class Scratchpads {
public:
    using Read=std::function<QJsonObject(const QString &)>;
    using Write=std::function<bool(const QString &,bool,bool)>;
    Scratchpads(Read read,Write write,std::function<bool(const QString &)> busy):read_(std::move(read)),write_(std::move(write)),busy_(std::move(busy)){}
    static bool validLabel(const QString &);
    QJsonObject describe(const QString &) const;
    QJsonArray decorate(QJsonArray windows,const std::function<QJsonObject(const QString &)> &interaction={}) const;
    QJsonObject submit(const QString &owner,const QJsonObject &,const QJsonObject &snapshot);
    void prune();
    void teardown();
    void release(const QString &owner);
private:
    Read read_;Write write_;std::function<bool(const QString &)> busy_;
    QMap<QString,QString> labels_;
    struct Job {QString owner;QByteArray digest;QJsonObject result;};
    QMap<QString,Job> jobs_;QStringList order_;QMap<QString,quint64> sequences_;
};
}
