#pragma once
#include "client.h"
#include "store.h"
#include "manager.h"
#include <QTimer>
namespace kwabai {
class GridPolicy {
public:
    GridPolicy(BridgeClient &,ContextStore &,ContextManager &,bool enabled);
    void pause(bool value){paused_=value;}
    QJsonObject command(const QString &selector,const QString &grid);
    QJsonObject status() const;
private:
    void step();
    QString signature(const QJsonObject &) const;
    QJsonObject submit(const QJsonObject &,const QString &grid);
    BridgeClient &bridge_;ContextStore &contexts_;ContextManager &manager_;bool enabled_,paused_=false;
    QTimer timer_;QElapsedTimer clock_;QString instance_,pending_,pendingWindow_;
    QMap<QString,QString> seen_,errors_;
};
}
