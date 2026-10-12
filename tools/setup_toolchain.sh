#!/bin/bash
# Sets up devkitARM, libctru, libctrpf and 3gxtool in a fresh MSYS2 install.
# The devkitPro signing key and keyring have to be installed first, see
# https://devkitpro.org/wiki/devkitPro_pacman
set -e
conf=/etc/pacman.conf

add_repo() {
    if ! grep -Fxq "[$1]" "$conf"; then
        printf '\n[%s]\nServer = %s\n%s' "$1" "$2" "${3:+$3$'\n'}" >> "$conf"
        echo "added package source $1"
    fi
}

# the Server line of dkp-windows needs a literal $arch
add_repo dkp-libs 'https://pkg.devkitpro.org/packages'
add_repo dkp-windows 'https://pkg.devkitpro.org/packages/windows/$arch/'
add_repo thepixellizeross-lib 'https://thepixellizeross.gitlab.io/packages/any' 'SigLevel = Optional'
add_repo thepixellizeross-win 'https://thepixellizeross.gitlab.io/packages/x86_64/win' 'SigLevel = Optional'

pacman -Syu --noconfirm
pacman -S --noconfirm --needed 3ds-dev libctrpf 3gxtool
