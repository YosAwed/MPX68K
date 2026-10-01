# dmgbuild settings for the MPX68K release disk image.
# Driven by make-dmg.sh: dmgbuild -s dmg_settings.py -D app=... -D background=...
import os

application = defines["app"]            # noqa: F821 (provided by dmgbuild)
appname = os.path.basename(application)

format = "UDZO"
filesystem = "HFS+"
size = None                             # computed from the contents

files = [application]
symlinks = {"Applications": "/Applications"}

background = defines["background"]      # noqa: F821
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
window_rect = ((200, 120), (640, 400))  # matches the 640x400 background
default_view = "icon-view"
icon_size = 112
text_size = 13
icon_locations = {
    appname: (170, 190),
    "Applications": (470, 190),
}
