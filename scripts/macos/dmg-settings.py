# Consumed by dmgbuild; no Finder UI scripting is needed.
import os

application = defines['app']
readme = defines['readme']
files = [application, readme]
symlinks = {'Applications': '/Applications'}
format = 'UDZO'
filesystem = 'HFS+'
volume_name = 'bbhouse-qt'
window_rect = ((200, 160), (660, 400))
background = '#f3f5fa'
icon_locations = {
    os.path.basename(application): (165, 155),
    'Applications': (490, 155),
    os.path.basename(readme): (330, 305),
}
icon_size = 96
text_size = 14
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
default_view = 'icon-view'
include_icon_view_settings = True
include_list_view_settings = False
