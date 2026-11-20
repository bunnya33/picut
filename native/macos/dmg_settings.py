import os
import subprocess

files = [defines["app"]]
symlinks = {"Applications": "/Applications"}
icon_locations = {"picut.app": (132, 152), "Applications": (388, 152)}
window_rect = ((180, 140), (520, 264))
icon_size = 88
text_size = 14
background = defines["background"]
badge_icon = defines["app"] + "/Contents/Resources/picut.icns"
format = "UDZO"


def create_hook(mount_point, _options):
    app = os.path.join(mount_point, os.path.basename(defines["app"]))
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", app], check=True)
