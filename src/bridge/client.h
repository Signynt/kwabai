#pragma once
#include "state.h"
#include "events.h"
#include <QMap>
#include <QSet>
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
namespace kwabai {
class BridgeClient : public QObject {
    Q_OBJECT
public:
    explicit BridgeClient(QObject *parent=nullptr);
    void start();
    // A completed revision greater than this token started after refresh was requested.
    quint64 refresh();
    quint64 snapshotRevision() const {return snapshotRevision_;}
    QJsonObject submitScratchpad(QJsonObject request);
    QJsonObject scratchpadStatus(const QString &id) const;
    QJsonObject submitLayer(QJsonObject request);
    QJsonObject layerStatus(const QString &id) const;
    void validateFocusReady() const;
    void setDropPreview(QJsonObject request);
    void clearDropPreview(const QString &contextId={});
    QJsonObject dropPreviewStatus() const {return previewStatus_;}
    QJsonObject submitDesktopMove(QJsonObject request,bool activity=false);
    QJsonObject desktopTransaction(const QString &id) const;
    QJsonObject latestDesktopMove() const {return latestMembershipMove(false);}
    QJsonObject latestActivityMove() const {return latestMembershipMove(true);}
    QJsonObject submitFocus(QJsonObject request);
    QJsonObject focusStatus(const QString &id) const;
    bool geometryBusy() const;
    QJsonObject submitGeometry(QJsonObject request);
    QJsonObject forgetGeometry(const QString &id);
    QJsonObject transaction(const QString &id) const;
    QJsonObject retentionStatus() const;
    QSet<QString> controlledWindows() const;
    QJsonObject contextStatus(const QString &contextId) const;
    QJsonObject eventStatus() const;
    QJsonObject events() const {return events_.events();}
    const BridgeState &state() const {return state_;}
Q_SIGNALS:
    void updated();
private Q_SLOTS:
    void onChanged(const QString &instance);
private:
    QMap<QString,QJsonObject> scratchpadJobs_;QStringList scratchpadOrder_;quint64 scratchpadSequence_=0;
    QMap<QString,QJsonObject> layerJobs_;QStringList layerOrder_;quint64 layerSequence_=0;
    void sendDropPreview();
    QJsonObject previewWanted_,previewStatus_,previewSent_;
    bool previewPending_=false;
    quint64 previewSequence_=0;
    QElapsedTimer previewClock_;
    void finishPoll();
    void poll();
    void fetchEvents(quint64 epoch);
    void geometryCall(const QString &method,const QString &id,const QVariant &argument);
    void pollGeometry();
    void pollFocus();
    QJsonObject latestMembershipMove(bool activity) const;
    void pollDesktops();
    void desktopCall(const QString &method,const QString &id,const QVariant &argument);
    QMap<QString,QJsonObject> desktopJobs_;
    QSet<QString> desktopBusy_;
    QStringList desktopOrder_;
    quint64 desktopSequence_=0;
    void focusCall(const QString &method,const QString &id,const QVariant &argument);
    QMap<QString,QJsonObject> focusJobs_;
    QSet<QString> focusBusy_;
    QStringList focusOrder_;
    quint64 focusSequence_=0;
    quint64 failures_=0;QString lastFailure_;
    void failed(const QString &reason);
    BridgeState state_;
    EventCursor events_;
    quint64 resyncs_=0;
    QMap<QString,QJsonObject> jobs_;
    QSet<QString> busy_;
    QStringList jobOrder_;
    quint64 requestSequence_=0;
    void makeRoom();
    QMap<QString,QString> latestContextJobs_;
    QTimer timer_,notificationTimer_;
    bool refreshRequested_=false,urgentRefreshRequested_=false;
    quint64 notifications_=0;
    bool pending_=false;
    quint64 epoch_=0,snapshotRevision_=0,snapshotRequestRevision_=0;
};
}
