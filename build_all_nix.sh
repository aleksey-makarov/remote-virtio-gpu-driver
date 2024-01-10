#!/usr/bin/env bash

YELLOW=$(tput setaf 3)
RESET=$(tput sgr0)

function go()
{
	package="$1"
	echo "${YELLOW}${package}${RESET}"
	nix build ".#$package" -o "result-$package"
}

go libvirtiolo
go libvirtiolo-dev
go libvirtiolo-debug
go virtio-lo
go virtio-lo-dev
go vduse

