#!/bin/sh
# Optional exclusions for ~/.config/kwabai/kwabairc. Labels are optional.
kwabai -m rule --add 'app=^krunner$' manage=off
kwabai -m rule --add 'app=^yakuake$' manage=off
kwabai -m rule --add 'app=^spectacle$' manage=off
kwabai -m rule --add 'app=^kded5$' manage=off
kwabai -m rule --add 'app=^xwaylandvideobridge$' manage=off
kwabai -m rule --add 'app=^plasmashell$' manage=off
kwabai -m rule --add 'app=^ksplashqml$' manage=off
kwabai -m rule --add 'app=^org\.kde\.plasmashell$' manage=off
kwabai -m rule --add 'app=^org\.kde\.polkit-kde-authentication-agent-1$' manage=off
kwabai -m rule --add 'app=^org\.kde\.kruler$' manage=off
kwabai -m rule --add 'app=^kruler$' manage=off
kwabai -m rule --add 'app=^kwin_wayland$' manage=off
kwabai -m rule --add 'app=^ksmserver-logout-greeter$' manage=off
kwabai -m rule --add 'app=^steam_app_2357570$' manage=off
kwabai -m rule --add 'app=^gamescope$' manage=off

# Dolphin Properties only; leave normal file browser windows managed.
kwabai -m rule --add 'app=^(org\.kde\.dolphin|dolphin)$' 'title=^Properties for ' manage=off
