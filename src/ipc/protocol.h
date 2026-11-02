#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
namespace kwabai::ipc {
constexpr quint32 MaxMessage = 1024 * 1024;
constexpr int DeadlineMs = 3000;
QString defaultSocket();
void privateDirectory(const QString &path, bool create);
QByteArray frame(const QJsonObject &message);
QJsonObject parse(const QByteArray &payload);
QJsonObject request(const QString &socket, const QStringList &args);
}
