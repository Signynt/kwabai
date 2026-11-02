#pragma once
#include <QObject>
#include "events.h"
#include "geometry.h"
#include "focus.h"
#include "layers.h"
#include "scratchpads.h"
#include "desktops.h"
#include <QDBusContext>
#include <QElapsedTimer>
#include <QTimer>
#include <QString>
namespace KWin {class EffectWindow;}
class KwabaiReadOnlyBridge : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kwabai.Bridge1")
public:
    explicit KwabaiReadOnlyBridge(QObject *parent=nullptr,bool previewSupported=false);
    ~KwabaiReadOnlyBridge() override;
public Q_SLOTS:
    QString Snapshot();
    QString DropPreview(const QString &request);
    QString MoveActivity(const QString &request);
    QString ActivityTransaction(const QString &id);
    QString MoveDesktop(const QString &request);
    QString DesktopTransaction(const QString &id);
    QString Scratchpad(const QString &request);
    QString SetLayer(const QString &request);
    QString Focus(const QString &request);
    QString FocusStatus(const QString &id);
    QString Apply(const QString &request);
    QString Transaction(const QString &id);
    QString Forget(const QString &id);
    QString Restore(const QString &id);
    QString Events(const QString &instance,const QString &after);
Q_SIGNALS:
    void Changed(const QString &instance);
    void DropPreviewChanged(const QJsonObject &preview);
private:
    void observe(KWin::EffectWindow *window);
    void publishPreview();
    void updateGeometryInterval();
    bool previewSupported_=false,previewTracking_=false;
    QJsonObject lastPreview_{{"visible",false}};
    quint64 sequence_=0;
    QString instance_;
    kwabai::EventJournal journal_;
    QByteArray content_,outputContent_;
    QTimer refresh_,geometryTimer_;
    QElapsedTimer geometryClock_;
    kwabai::GeometryTransactions geometry_;
    kwabai::FocusRequests focus_;
    kwabai::LayerRequests layers_;
    QMap<QString,QJsonObject> scratchVisibility_;
    kwabai::Scratchpads scratchpads_;
    kwabai::DesktopMoves desktopMoves_;
    kwabai::MembershipMoves activityMoves_;
    bool exported_=false;
};
