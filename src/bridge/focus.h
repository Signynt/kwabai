#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <functional>
namespace kwabai {
QJsonObject focusRequest(const QJsonObject &neighbor,const QJsonObject &plan,const QJsonObject &windows,const QString &id);
void validateFocusRequest(const QJsonObject &request,const QJsonObject &snapshot);
class FocusRequests {
public:
    using Active=std::function<QString()>;
    using Activate=std::function<bool(const QString &)>;
    FocusRequests(Active active,Activate activate):active_(std::move(active)),activate_(std::move(activate)){}
    QJsonObject submit(const QString &owner,const QJsonObject &request,const QJsonObject &snapshot,qint64 now);
    QJsonObject status(const QString &owner,const QString &id) const;
    bool tick(qint64 now);
    void release(const QString &owner);
private:
    struct Job {QString owner,source,target,state="pending",error;QByteArray digest;qint64 since=0;};
    Active active_;Activate activate_;
    QMap<QString,Job> jobs_;QStringList order_;QMap<QString,quint64> sequences_;
};
}
