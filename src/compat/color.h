#pragma once
#include <QString>
#include <stdexcept>
namespace kwabai::compat {
inline QString insertFeedbackColor(const QString &text) {
    if(text.size()!=10||!text.startsWith("0x",Qt::CaseInsensitive))throw std::invalid_argument("expected color 0xAARRGGBB");
    for(auto c:text.mid(2))if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))throw std::invalid_argument("expected color 0xAARRGGBB");
    return text.toLower();
}
inline bool isInsertFeedbackSetting(const QString &name) {
    return name=="insert_window_border_color"||name=="insert_feedback_color";
}
inline bool isPreviewSetting(const QString &name) {return isInsertFeedbackSetting(name)||name=="window_border_radius";}
inline int windowBorderRadius(const QString &text) {
    if(text.isEmpty())throw std::invalid_argument("window_border_radius must be an integer from 0 to 20");
    for(auto c:text)if(c<'0'||c>'9')throw std::invalid_argument("window_border_radius must be an integer from 0 to 20");
    bool ok=false;auto value=text.toUInt(&ok);if(!ok||value>20)throw std::invalid_argument("window_border_radius must be an integer from 0 to 20");return int(value);
}
}
