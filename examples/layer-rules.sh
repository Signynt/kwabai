#!/bin/sh
# Optional additions to ~/.config/kwabai/kwabairc, after upgrading the effect.
set -eu
kwabai -m rule --add label=system-settings 'app=^(systemsettings|org\.kde\.systemsettings)$' 'title=.*' manage=off layer=above
kwabai -m rule --add label=info-windows 'title=.*Info' manage=off layer=above
# Ordinary floating windows already appear above automatic tiles. Explicit
# layer=above additionally puts these matches above ordinary floating windows.
