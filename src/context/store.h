#pragma once
#include "tree.h"
#include "rules.h"
#include "state.h"
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStringList>
namespace kwabai {
class ContextStore {
public:
    bool floatPopups() const {return rules_.floatPopups();}
    void setFloatPopups(const QString &value);
    bool isZoomed(const QString &uuid) const;
    void clearZoom();
    int globalSpacing(const QString &name) const;
    void setGlobalSpacing(const BridgeState &bridge,const QString &name,const QString &value);
    void useGlobalSpacing(const BridgeState &bridge);
    void prepareAutomatic(const BridgeState &bridge,const QString &output);
    int windowBorderRadius() const {return windowBorderRadius_;}
    void setWindowBorderRadius(const QString &radius);
    QString insertFeedbackColor() const {return insertFeedbackColor_;}
    void setInsertFeedbackColor(const QString &color);
    QJsonObject layerRule(const QJsonObject &window) const {return rules_.evaluate(window);}
    void editRules(const QStringList &a) {rules_.edit(a);}
    QJsonArray rulesPosix() const {return rules_.listPosix();}
    QJsonObject rules() const {return rules_.list();}
    QJsonObject testRule(const BridgeState &b,const QString &selector) const;
    void exchangeOutputs(const BridgeState &bridge,const QString &sourceOutput,const QString &sourceWindow,const QString &targetOutput,const QString &targetWindow);
    void reset(const BridgeState &bridge,const QStringList &outputs);
    void edit(const BridgeState &bridge,const QStringList &arguments);
    QJsonObject stageDesktopAdmission(const BridgeState &bridge,const QString &output,const QString &windowUuid,const QString &desktop,bool activity=false);
    QJsonObject list() const;
    QJsonObject show(const BridgeState &bridge,const QString &output) const;
    QJsonObject neighbor(const BridgeState &bridge,const QString &output,const QString &source,const QString &direction) const;
    QJsonObject reconcile(const BridgeState &bridge,const QString &output,bool removeHidden=false,bool removeIneligible=false) const;
    QJsonObject plan(const BridgeState &bridge,const QString &output,bool allowEmpty=false) const;
    QJsonObject state() const;
    static ContextStore restore(const QJsonObject &state);
private:
    void editImpl(const BridgeState &,const QStringList &);
    struct Revision {Tree tree;double gap;Insets padding;};
    struct Context {QString id,output,desktop,activity;Tree tree;QMap<WindowId,QString> members;QSet<QString> floating,zoomed;quint64 generation=0;double gap=10;Insets padding;std::vector<Revision> undo,redo;};
    QMap<QString,Context> contexts_;
    RuleSet rules_;
    QMap<QString,int> spacing_{{"top_padding",0},{"bottom_padding",0},{"left_padding",0},{"right_padding",0},{"window_gap",0}};
    bool globalSpacing_=false;
    int windowBorderRadius_=0;
    QString insertFeedbackColor_="0xffd75f5f";
    WindowId nextWindow_=1;
    static QJsonObject output(const BridgeState &,const QString &selector);
    static QString key(const QJsonObject &output);
    WindowId member(const Context &,const BridgeState &,const QString &selector) const;
};
}
