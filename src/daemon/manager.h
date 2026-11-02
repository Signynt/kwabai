#pragma once
#include "client.h"
#include "store.h"
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
namespace kwabai {
// Global BSP policy discovers contexts; explicit controllers remain available for diagnostics.
class ContextManager {
public:
    ContextManager(BridgeClient &bridge,ContextStore &contexts,std::function<void(const QJsonObject &)> persist);
    QString layout() const {return layout_;}
    void setLayout(const QString &layout);
    void pauseConfiguration(bool paused) {configurationPaused_=paused;if(!paused)QTimer::singleShot(0,&timer_,[this]{step();});}
    QJsonObject layoutStatus() const;
    bool mouseDropBusy() const {return !drop_.isEmpty();}
    QJsonObject mouseDropStatus() const {auto status=drop_.isEmpty()?lastDrop_:drop_;if(!drop_.isEmpty())status["elapsed-ms"]=dropClock_.elapsed();return status;}
    void pauseForTransfer(bool paused) {paused_=paused;}
    QJsonObject enable(const QString &output,bool lifecycle=false);
    void armArrival(const QJsonObject &context,const QString &windowUuid);
    void adopt(const QString &contextId,const QJsonObject &receipt);
    void stop(const QString &contextId,const QString &reason={});
    QJsonObject status(const QString &contextId={}) const;
    void validateEdit(const QString &output,const QString &operation) const;
private:
    void step();
    bool stepPolicy();
    void stepOutputDrop();
    void reconcile(const QString &output);
    struct Slot {
        QString bridgeInstance,contextId,outputId,desktop,activity,transactionId,interactionId,state="disabled",error;
        bool enabled=false,lifecycle=false,resuming=false,arrivalOnly=false;
        bool windowSuspended=false;
        QJsonArray pausedWindows,recoveryWindows,isolatedWindows;
        QString recoveryId,adaptedTransaction;
        QElapsedTimer retryClock;int retries=0;
        QElapsedTimer resumeClock;
        QSet<QString> arrivals;
    };
    QJsonObject describe(const Slot &slot) const;
    void step(Slot &slot);
    bool recoverChangedWindows(Slot &slot,const QJsonObject &job);
    bool finishWindowRecovery(Slot &slot);
    void adaptGeometry(Slot &slot,const QJsonObject &job);
    bool pauseForWindowState(Slot &slot,const QJsonObject &job,const QJsonObject &context,const QJsonObject &output);
    void beginOutputDrop(Slot &source,const QJsonObject &job,const QJsonObject &window,const QJsonObject &gesture,const QJsonObject &output,bool preview=false);
    bool interact(Slot &slot,const QJsonObject &job,const QJsonObject &output);
    void validateOwnedFrames(const QJsonObject &job,bool allowClosed) const;
    std::function<void(const QJsonObject &)> persist_;
    QJsonObject submit(const QString &output,const QJsonObject &active,bool lifecycle);
    BridgeClient &bridge_;
    ContextStore &contexts_;
    QTimer timer_;
    QMap<QString,Slot> slots_;
    QElapsedTimer dropClock_;
    std::optional<ContextStore> dropCandidate_;
    QJsonObject drop_,lastDrop_;
    bool paused_=false;
    QString layout_="float";
    bool configurationPaused_=false,releaseAll_=false;
    QMap<QString,QString> policyErrors_,policyReleases_;
    QSet<QString> visiblePolicyKeys_;
};
}
