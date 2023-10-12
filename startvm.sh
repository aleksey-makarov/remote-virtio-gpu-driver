#!/usr/bin/env bash
mkdir -p ./xchg

TMPDIR=$(pwd)
USE_TMPDIR=1
export TMPDIR USE_TMPDIR

TTY_FILE="./xchg/tty.sh"
read -r rows cols <<< "$(stty size)"

cat << EOF > "${TTY_FILE}"
export TERM=xterm-256color
stty rows $rows cols $cols
reset
EOF

stty intr ^] # send INTR with Control-]
nixGLMesa run-nixos-vm
stty intr ^c
