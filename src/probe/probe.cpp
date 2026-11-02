#include "bridge.h"
#include "kwabai-build-version.h"
#include "drop_overlay.h"
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>
#include <outline.h>
#include <QDBusConnection>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QDebug>
#include <QScopedValueRollback>

namespace {
QJsonObject rect(const KWin::RectF &r) {
    return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};
}
bool eligible(KWin::Window *w) {
    return w && !w->isDeleted() && !w->isInternal() && !w->isOutline() && w->isNormalWindow()
        && !w->isSpecialWindow() && !w->isMinimized() && !w->isFullScreen()
        && w->maximizeMode() == KWin::MaximizeRestore
        && w->isOnCurrentDesktop() && w->isOnCurrentActivity()
        && w->isMovable() && w->isResizable();
}
}

class KwabaiProbe : public KWin::Effect {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kwabai.Probe1")
public:
    KwabaiProbe() {
        auto *bridge=new KwabaiReadOnlyBridge(this,KWin::effects->openglContext()!=nullptr);
        connect(bridge,&KwabaiReadOnlyBridge::DropPreviewChanged,this,[this](const QJsonObject &preview){
            auto old=dropFrame;auto r=preview["frame"].toObject();dropFrame=preview["visible"].toBool()?QRectF(r["x"].toDouble(),r["y"].toDouble(),r["w"].toDouble(),r["h"].toDouble()):QRectF{};dropRadius=preview["radius"].toInt();dropWindow=preview["window"].toString();dropColor=QColor::fromRgba(preview.value("color").toString("0xffd75f5f").mid(2).toUInt(nullptr,16));
            if(!old.isEmpty())KWin::effects->addRepaint(KWin::RectF(old.adjusted(-2,-2,2,2)));
            if(!dropFrame.isEmpty())KWin::effects->addRepaint(KWin::RectF(dropFrame.adjusted(-2,-2,2,2)));
        });
        auto bus = QDBusConnection::sessionBus();
        // KWin owns org.kde.KWin. Export only a namespaced object on its connection.
        if (!bus.registerObject("/org/kwabai/Probe", this,
                QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals))
            qWarning() << "kwabai: cannot export probe object";
        for (auto *w : KWin::effects->stackingOrder()) observe(w);
        connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &KwabaiProbe::observe);
        connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, [this](KWin::EffectWindow *w) {
            if (w->window() == first || w->window() == second) Disarm();
        });
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, &KwabaiProbe::HidePreview);
    }
    ~KwabaiProbe() override {
        KWin::effects->makeOpenGLContextCurrent();dropOverlay.releaseShader();
        Disarm();if(!dropFrame.isEmpty())KWin::effects->addRepaint(KWin::RectF(dropFrame.adjusted(-2,-2,2,2)));
        QDBusConnection::sessionBus().unregisterObject("/org/kwabai/Probe");
    }
    bool isActive() const override {return !dropFrame.isEmpty()&&dropColor.alpha()>0;}
    void prePaintScreen(KWin::ScreenPrePaintData &data) override {
        // An overlay cannot blend onto retained pixels: the scene beneath it must
        // be painted again. The generic pass also disables opaque-window culling
        // so the destination is painted before the dragged window covers it.
        // This full-output pass is used only while a drop preview is visible.
        data.mask|=PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
        KWin::effects->prePaintScreen(data);
    }
    bool paintScreen(const KWin::RenderTarget &target,const KWin::RenderViewport &viewport,int mask,const KWin::Region &damage,KWin::LogicalOutput *screen) override {
        QScopedValueRollback<bool> pass(dropPainted,false);
        return KWin::effects->paintScreen(target,viewport,mask,damage,screen);
    }
    bool paintWindow(const KWin::RenderTarget &target,const KWin::RenderViewport &viewport,KWin::EffectWindow *w,int mask,const KWin::Region &damage,KWin::WindowPaintData &data) override {
        // Insert once into the bottom-to-top pass, immediately below the moving
        // window (including its decoration and shadow). Never repaint that window
        // separately or alter its opacity, elevation, input or cancellation.
        if(!dropPainted&&!dropFrame.isEmpty()&&w->window()->internalId().toString(QUuid::WithoutBraces)==dropWindow&&w->window()->isInteractiveMove()) {
            dropPainted=true;
            dropOverlay.paint(target,viewport,dropFrame,dropColor,dropRadius);
        }
        return KWin::effects->paintWindow(target,viewport,w,mask,damage,data);
    }
public Q_SLOTS:
    QString Snapshot() const {
        QJsonArray windows;
        for (auto *ew : KWin::effects->stackingOrder()) {
            auto *w = ew->window();
            if (!eligible(w)) continue;
            windows.append(QJsonObject{{"uuid",w->internalId().toString()},
                {"app",w->resourceClass()},{"title",w->caption()},
                {"frame",rect(w->frameGeometry())},
                {"minimum",QJsonObject{{"w",w->minSize().width()},{"h",w->minSize().height()}}}});
        }
        return QString::fromUtf8(QJsonDocument(windows).toJson(QJsonDocument::Compact));
    }
    QString Arm(const QString &a, const QString &b) {
        if (a == b) return "error: select two different UUIDs";
        KWin::Window *wa = nullptr, *wb = nullptr;
        for (auto *ew : KWin::effects->stackingOrder()) {
            auto *w = ew->window();
            if (!eligible(w)) continue;
            if (w->internalId() == QUuid(a)) wa = w;
            if (w->internalId() == QUuid(b)) wb = w;
        }
        if (!wa || !wb) return "error: UUID missing or window ineligible";
        if (wa->isInteractiveMove() || wa->isInteractiveResize() || wb->isInteractiveMove() || wb->isInteractiveResize())
            return "error: finish the interactive operation before arming";
        Disarm();
        first = wa; second = wb;
        originalFirst = wa->frameGeometry(); originalSecond = wb->frameGeometry();
        return "armed";
    }
    QString Swap() {
        if (!eligible(first) || !eligible(second)) return "error: arm two eligible windows first";
        if (first->isInteractiveMove() || first->isInteractiveResize() || second->isInteractiveMove() || second->isInteractiveResize())
            return "error: interactive operation in progress";
        auto a = first->frameGeometry(), b = second->frameGeometry();
        if (a.width() < second->minSize().width() || a.height() < second->minSize().height()
            || b.width() < first->minSize().width() || b.height() < first->minSize().height())
            return "error: minimum size prevents swap";
        changed = true;
        first->moveResize(b); second->moveResize(a);
        return "requested: inspect Snapshot for asynchronous client acknowledgement";
    }
    QString PreviewRight() {
        if (!eligible(second)) return "error: arm two eligible windows first";
        auto r = second->frameGeometry();
        r.setX(r.x() + r.width()/2);
        KWin::workspace()->outline()->show(r.toRect());
        preview = true; timer.start(1500);
        return "preview requested for 1500ms";
    }
    void HidePreview() {
        timer.stop();
        if (preview) KWin::workspace()->outline()->hide();
        preview = false;
    }
    void Restore() {
        HidePreview();
        if (changed) {
            if (eligible(first)) first->moveResize(originalFirst);
            if (eligible(second)) second->moveResize(originalSecond);
        }
        changed = false;
    }
    void Disarm() { Restore(); first.clear(); second.clear(); }
Q_SIGNALS:
    void Trace(const QString &json);
private:
    void observe(KWin::EffectWindow *ew) {
        auto *w = ew->window();
        if (!w || w->isInternal()) return;
        connect(w, &KWin::Window::interactiveMoveResizeStarted, this, [this,w] {
            if (w != first && w != second) return;
            trace("dragStart",w); // Observation only; KWin retains cancellation ownership.
        });
        connect(w, &KWin::Window::interactiveMoveResizeStepped, this, [this,w] {
            if (w == first || w == second) trace("dragStep",w);
        });
        connect(w, &KWin::Window::interactiveMoveResizeFinished, this, [this,w] {
            if (w != first && w != second) return;
            HidePreview(); trace("dragFinish",w);
        });
    }
    void trace(const QString &event, KWin::Window *w) {
        emit Trace(QString::fromUtf8(QJsonDocument(QJsonObject{{"event",event},
            {"uuid",w->internalId().toString()},{"frame",rect(w->frameGeometry())}}).toJson(QJsonDocument::Compact)));
    }
    DropOverlay dropOverlay;
    QRectF dropFrame;QString dropWindow;QColor dropColor;int dropRadius=0;bool dropPainted=false;
    QPointer<KWin::Window> first, second;
    KWin::RectF originalFirst, originalSecond;
    QTimer timer;
    bool changed = false, preview = false;
};
KWIN_EFFECT_FACTORY_ENABLED(KwabaiProbe, "kwabai-metadata.json", return false;)
#include "probe.moc"
