#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
namespace kwabai {
// Bounded compositor journal. Event ordinals are independent of snapshot sequence.
class EventJournal {
public:
    explicit EventJournal(QString instance):instance_(std::move(instance)){}
    void append(const QString &type,const QJsonObject &data={});
    QJsonObject since(const QString &instance,quint64 cursor) const;
    quint64 latest() const {return latest_;}
private:
    QString instance_;
    quint64 latest_=0;
    QJsonArray records_;
};
class EventCursor {
public:
    void baseline(const QString &instance,quint64 cursor);
    // False means the journal explicitly requires a snapshot resync.
    bool accept(const QByteArray &json);
    QJsonObject status() const;
    QJsonObject events() const;
    QString instance() const {return instance_;}
    quint64 cursor() const {return cursor_;}
    void clear();
private:
    QString instance_;
    quint64 cursor_=0;
    QJsonArray records_;
    bool ready_=false;
};
}
