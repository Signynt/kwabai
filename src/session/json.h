#pragma once
#include "tree.h"
#include <QByteArray>
namespace kwabai {
QByteArray encodeSnapshot(const Tree &tree);
Tree decodeSnapshot(const QByteArray &bytes);
}
