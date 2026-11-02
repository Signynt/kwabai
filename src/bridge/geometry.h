#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QRectF>
#include <QStringList>
#include <functional>
#include <optional>
namespace kwabai {
QJsonObject geometryRequest(const QJsonObject &plan,const QJsonObject &windows,const QString &id,const QJsonObject &active={});
QJsonObject outputDestination(const QJsonArray &outputs,const QJsonObject &source,const QString &selector);
QJsonObject outputMoveRequest(const QJsonObject &window,const QJsonObject &source,const QJsonObject &destination,const QJsonObject &snapshot,const QString &id);
void validateGeometryRequest(const QJsonObject &request,const QJsonObject &snapshot);
class GeometryTransactions {
public:
    using Read=std::function<std::optional<QRectF>(const QString &)>;
    using Write=std::function<bool(const QString &,const QRectF &)>;
    using Output=std::function<QString(const QString &)>;
    using Raise=std::function<void(const QString &)>;
    GeometryTransactions(Read read,Write write,Output output={},Raise raise={}):read_(std::move(read)),write_(std::move(write)),output_(std::move(output)),raise_(std::move(raise)){}
    QJsonObject apply(const QString &owner,const QJsonObject &request,const QJsonObject &snapshot,qint64 now);
    QJsonObject forget(const QString &owner,const QString &id);
    QJsonObject restore(const QString &owner,const QString &id,qint64 now);
    QJsonObject status(const QString &owner,const QString &id) const;
    bool tick(qint64 now);
    bool pending() const;
    int pollInterval() const {return pending()?16:40;}
    bool owns(const QString &uuid) const;
    // Tiled ownership is independent of the automatic stacking priority.
    bool tiles(const QString &uuid) const;
    bool zoomed(const QString &uuid) const;
    void beginInteraction(const QString &uuid,const QString &kind,qint64 now);
    void finishInteraction(const QString &uuid,const QPointF &pointer,qint64 now);
    QJsonObject interaction(const QString &uuid,qint64 now) const;
    QJsonObject setDropPreview(const QString &owner,const QJsonObject &request,const QJsonObject &snapshot,qint64 now);
    QJsonObject dropPreview(const QJsonObject &snapshot,qint64 now) const;
    void release(const QString &owner,qint64 now);
    void teardown();
private:
    struct Window {QString uuid;QRectF target,rollback;bool released=false,passive=false,settlePosition=false;bool zoomed=false;int positionSamples=0;bool positionCorrected=false;QRectF positionSample{};int observedSamples=0;QRectF observedFrame{};};
    struct Transaction {QString lineage;QString targetOutput,originalOutput;QString owner,state="pending",error,previous,replacement,contextId;quint64 generation=0;bool floatingFrame=false,reconcile=false;QByteArray digest;QList<Window> windows;qint64 since=0;int matching=0,positionCandidates=0,positionSizeMatches=0;};
    struct Interaction {QString id,previous,kind,state="active";QRectF start,end;QPointF pointer;qint64 since=0;};
    QJsonObject preview_;
    QString previewOwner_;
    qint64 previewUntil_=0;
    QMap<QString,quint64> previewSequences_;
    QMap<QString,Interaction> interactions_;
    QMap<QString,Transaction> transactions_;
    QStringList order_;
    QMap<QString,quint64> ownerSequences_;
    void makeRoom();
    Read read_;Write write_;Output output_;Raise raise_;
    void rollbackWrites(Transaction &t,qint64 now);
    void confirmApply(Transaction &t);
    void retirePrevious(Transaction &t);
    QString activeId(QString id) const;
};
}
