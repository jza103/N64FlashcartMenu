#!/bin/bash
# Deploy the built menu to the SC64 over USB from WSL, via the Windows-side
# `sc64deployer server 0.0.0.0:9064` TCP bridge (WSL2 can't see USB serial directly).
#
# Prereqs on the Windows host (leave the server running):
#   sc64deployer.exe list                  # confirm the cart is seen
#   sc64deployer.exe server 0.0.0.0:9064   # start the bridge
#
# The WSL->Windows gateway IP is dynamic (changes across reboots), so we derive
# it from the default route each run rather than hardcoding it.
#
# TWO deploy modes:
#   ./deploy.sh ram   (default) upload into cart RAM + reboot (debug send-file).
#                     Fast for iterating. On the Analogue 3D you must trigger a
#                     "reset" from its menu to see it. Does NOT survive a true
#                     power-off (reads from SD on real power-on).
#   ./deploy.sh sd    write /sc64menu.n64 directly to the SD card so a TRUE
#                     power-on boots it. REQUIRES the SD be unlocked: put the
#                     console at the Analogue menu with NO cart loaded first
#                     (the running menu locks the SD otherwise).
set -e

MODE="${1:-ram}"
GW=$(ip route | grep default | awk '{print $3}' | head -1)
if [ -z "$GW" ]; then
    echo "Could not determine WSL->Windows gateway from 'ip route'." >&2
    exit 1
fi
echo "Windows host (sc64deployer server) at: $GW:9064  [mode: $MODE]"

DOCKER_COMMON=(--rm --add-host=host.docker.internal:"$GW" -v "$PWD:/work" -w /work)
R="sc64deployer --remote host.docker.internal:9064"

case "$MODE" in
    ram)
        docker run "${DOCKER_COMMON[@]}" \
            -e REMOTE="host.docker.internal:9064" \
            n64menu-deployer ./remotedeploy.sh -dur
        ;;
    sd)
        echo "Ensure the console is at the Analogue menu with NO cart loaded (SD must be unlocked)."
        docker run "${DOCKER_COMMON[@]}" n64menu-deployer bash -c \
            "$R sd upload output/N64FlashcartMenu.n64 /sc64menu.n64 && echo '--- SD file now:' && $R sd stat /sc64menu.n64"
        ;;
    *)
        echo "Unknown mode '$MODE' (use 'ram' or 'sd')." >&2
        exit 1
        ;;
esac
