#pragma once
#include "manager.h"
#include <QElapsedTimer>
namespace kwabai {
// One serialized, observable workflow. Completed stages are never replayed.
class OutputTransfers {
public:
    using Execute=std::function<QJsonObject(const QStringList &)>;
    OutputTransfers(BridgeClient &bridge,ContextStore &contexts,ContextManager &manager,Execute execute);
    QJsonObject start(const QString &window,const QString &destination,bool desktop=false,bool activity=false);
    QJsonObject status(const QString &id) const;
    QJsonObject latest() const {return order_.isEmpty()?QJsonObject{}:jobs_[order_.last()];}
    bool active() const {return !activeId_.isEmpty();}
private:
    void step();
    void submit(const QString &phase,const QStringList &command);
    void finish(const QString &error={});
    void refresh(const QString &phase);
    void validateContext() const;
    QJsonObject window() const;
    BridgeClient &bridge_;ContextStore &contexts_;ContextManager &manager_;Execute execute_;
    QTimer timer_;QElapsedTimer elapsed_;
    QMap<QString,QJsonObject> jobs_;QStringList order_;
    QString activeId_,uuid_,sourceId_,destinationId_,sourceContext_,destinationContext_,bridgeInstance_,transactionId_;
    QJsonObject source_,destination_;
    quint64 snapshotRevision_=0;
    bool desktop_=false,activity_=false;
    QString desktopId_,transactionKind_;
};
}
