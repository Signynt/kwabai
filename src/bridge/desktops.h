#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QStringList>
#include <functional>
namespace kwabai {
QJsonObject desktopDestination(const QJsonArray &catalog,const QString &selector);
void validateDesktopMove(const QJsonObject &request,const QJsonObject &snapshot);
QJsonObject activityDestination(const QJsonArray &catalog,const QString &selector);
void validateActivityMove(const QJsonObject &request,const QJsonObject &snapshot);
// Desktop and Activity writes share validation, confirmation and bounded failure handling.
class MembershipMoves {
public:
    using Read=std::function<QJsonObject(const QString &)>;
    using Write=std::function<bool(const QString &,const QJsonArray &)>;
    using Available=std::function<bool(const QString &)>;
    MembershipMoves(Read read,Write write,Available available,bool activity=false):read_(std::move(read)),write_(std::move(write)),available_(std::move(available)),activity_(activity){}
    QJsonObject submit(const QString &owner,const QJsonObject &request,const QJsonObject &snapshot,qint64 now);
    QJsonObject status(const QString &owner,const QString &id) const;
    bool tick(qint64 now);
    bool pending() const;
    void release(const QString &owner,qint64 now);
    void teardown();
private:
    struct Job {QString owner,window,state="pending",error,output;QJsonArray original,target,other;QByteArray digest;qint64 since=0;int matching=0;};
    void rollback(Job &job,qint64 now,const QString &error);
    bool untouched(const Job &job,const QJsonObject &window) const;
    QString membership() const {return activity_?"activities":"desktops";}
    QString otherMembership() const {return activity_?"desktops":"activities";}
    QString targetField() const {return activity_?"activity":"desktop";}
    Read read_;Write write_;Available available_;
    bool activity_=false;
    QMap<QString,Job> jobs_;QStringList order_;QMap<QString,quint64> sequences_;
};
using DesktopMoves = MembershipMoves;
}
