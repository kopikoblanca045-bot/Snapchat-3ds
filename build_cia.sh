#!/usr/bin/env bash
# Builds camera.cia. Needs: devkitARM + libctru (devkitPro), makerom, bannertool in PATH.
set -e
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}

make clean
make

bannertool makebanner -i banner.png -a audio.wav -o banner.bnr
bannertool makesmdh -s "FilterCam" -l "Camera with filters, stickers and beauty" -p "Homebrew" -i icon.png -o icon.icn
makerom -f cia -o camera.cia -elf camera.elf -rsf camera.rsf -icon icon.icn -banner banner.bnr -exefslogo -target t

echo "Done: camera.cia"
