#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QString>
namespace kwabai {
class BridgeState {
public:
    void accept(const QByteArray &json);
    void disconnect(const QString &reason);
    QJsonObject status() const;
    QJsonObject windows() const;
    QJsonObject outputs() const;
    QJsonObject desktops() const;
    QJsonObject activities() const;
    bool hasActivityMove() const {return hasGeometryRelease()&&capabilities_.contains("activity-move");}
    bool hasMouseOutputSwap() const {return hasMouseOutputDrop()&&hasOutputMove()&&capabilities_.contains("mouse-output-swap");}
    bool hasMouseOutputPreview() const {return hasDropPreview()&&hasMouseOutputDrop()&&capabilities_.contains("mouse-output-preview");}
    bool hasGeometryReconcile() const {return hasGeometryUpdate()&&capabilities_.contains("geometry-reconcile");}
    bool hasGeometryZoom() const {return hasGeometryUpdate()&&capabilities_.contains("geometry-zoom");}
    bool hasFloatingGrid() const {return hasGeometryRetention()&&capabilities_.contains("floating-grid");}
    bool hasScratchpads() const {return connected_&&capabilities_.contains("scratchpads");}
    bool hasLayers() const {return connected_&&capabilities_.contains("layers");}
    bool hasNoRestore() const {return hasGeometry()&&capabilities_.contains("geometry-no-restore");}
    bool hasHiddenRelease() const {return hasGeometryRelease()&&capabilities_.contains("geometry-hidden-release");}
    bool hasGeometryForget() const {return hasGeometryRetention()&&capabilities_.contains("geometry-forget");}
    bool hasMouseOutputDrop() const {return hasMouse()&&hasGeometryRelease()&&capabilities_.contains("mouse-output-drop");}
    bool hasDropPreview() const {return hasMouse()&&capabilities_.contains("drop-preview");}
    bool hasMouse() const {return hasGeometryUpdate()&&capabilities_.contains("mouse");}
    bool hasDesktopMove() const {return hasGeometryRelease()&&capabilities_.contains("desktop-move");}
    bool hasOutputMove() const {return hasGeometryRetention() && capabilities_.contains("output-move");}
    bool hasGeometryRelease() const {return hasGeometryLifecycle() && capabilities_.contains("geometry-release");}
    bool hasFocus() const {return connected_ && capabilities_.contains("focus");}
    int geometryLimit() const {return hasGeometry()?geometryLimit_:0;}
    bool hasGeometryLifecycle() const {return hasGeometryRetention() && capabilities_.contains("geometry-lifecycle");}
    bool hasGeometryRetention() const {return hasGeometryUpdate() && capabilities_.contains("geometry-retention");}
    bool hasGeometryUpdate() const {return hasGeometry() && capabilities_.contains("geometry-update");}
    bool hasGeometry() const {return connected_ && capabilities_.contains("geometry");}
    bool hasEvents() const {return connected_ && capabilities_.contains("events");}
    quint64 eventSequence() const {return eventSequence_;}
    bool hasContexts() const {return connected_ && capabilities_.contains("contexts");}
    bool connected() const { return connected_; }
private:
    int geometryLimit_=2;
    bool connected_=false,enabled_=false;
    QString instance_,version_,error_="observation disabled";
    quint64 sequence_=0,nextId_=1,eventSequence_=0;
    QJsonArray windows_,outputs_,desktops_,activities_,capabilities_;
    QByteArray content_;
    QMap<QString,quint64> ids_;
};
}
