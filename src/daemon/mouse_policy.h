#pragma once
#include <QRectF>
#include <QString>
#include <array>
#include <algorithm>
namespace kwabai {
// The central half swaps; the nearest normalized edge inserts. Normalization
// keeps all four zones usable on both wide and portrait tiles.
inline QString mouseDropDirection(QRectF tile,QPointF pointer) {
    const double x=(pointer.x()-tile.left())/tile.width(),y=(pointer.y()-tile.top())/tile.height();
    std::array<std::pair<double,QString>,4> edges{{{x,"west"},{1-x,"east"},{y,"north"},{1-y,"south"}}};
    auto nearest=std::min_element(edges.begin(),edges.end(),[](const auto &a,const auto &b){return a.first<b.first;});
    return nearest->first<0.25?nearest->second:QString{};
}
}
