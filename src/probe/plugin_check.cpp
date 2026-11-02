#include <QCoreApplication>
#include <config-kwin.h>
#include <QFileInfo>
#include <QPluginLoader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <KPluginMetaData>
#include <effect/effect.h>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc!=3)return 2;
    const QString path=QString::fromLocal8Bit(argv[1]);
    const auto expected=QString::fromLocal8Bit(argv[2]);
    KPluginMetaData metadata(path);
    if(!metadata.isValid()||metadata.pluginId()!=expected||QFileInfo(path).fileName()!=expected+".so") {
        std::cerr<<"plugin discovery identity mismatch\n";return 1;
    }
    const auto raw=metadata.rawData();
    if(raw.value("X-Kwabai-KWin-Version").toString()!=QString::fromLatin1(KWIN_PLUGIN_VERSION_STRING)
        ||raw.value("X-Kwabai-Effect-Id").toString()!=expected) {
        std::cerr<<"plugin build metadata does not match installed KWin; rebuild required\n";return 1;
    }
    bool discovered=false;
    for(const auto &plugin:KPluginMetaData::findPlugins(QFileInfo(path).absolutePath()))
        if(plugin.pluginId()==expected&&plugin.fileName()==metadata.fileName())discovered=true;
    if(!discovered){std::cerr<<"plugin not discoverable by canonical ID\n";return 1;}
    QPluginLoader loader(path);
    // Resolve every imported function before any effect is constructed. Lazy
    // binding previously hid a private KWin symbol until it killed the desktop.
    loader.setLoadHints(loader.loadHints()|QLibrary::ResolveAllSymbolsHint);
    if(loader.metaData().value("IID").toString()!=QString::fromLatin1(EffectPluginFactory_iid)) {
        std::cerr<<"plugin KWin ABI IID mismatch\n";return 1;
    }
    auto *factory=qobject_cast<KWin::EffectPluginFactory *>(loader.instance());
    if(!factory){std::cerr<<loader.errorString().toStdString()<<'\n';return 1;}
    // Instantiate only the plugin factory, never an effect outside a compositor.
    if(!factory->isSupported()||factory->enabledByDefault()){std::cerr<<"unexpected factory flags\n";return 1;}
    std::cout<<QJsonDocument(QJsonObject{{"kwin-version",QString::fromLatin1(KWIN_PLUGIN_VERSION_STRING)},{"effect-id",expected}}).toJson(QJsonDocument::Compact).constData()<<'\n';
}
