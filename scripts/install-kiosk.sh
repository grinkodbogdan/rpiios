#!/usr/bin/env bash
# Make the Pi boot straight into the emulated iPhone OS, full screen.
# Run with sudo after build-emulator.sh and fetch-firmware.sh.
#   sudo scripts/install-kiosk.sh            install and enable
#   sudo scripts/install-kiosk.sh --remove   back to the normal console
set -euo pipefail
here="$(dirname "$(readlink -f "$0")")"
source "$here/config.sh"

[[ $EUID -eq 0 ]] || die "run as root (sudo $0)"

if [[ "${1:-}" == "--remove" ]]; then
    systemctl disable --now rpiios.service 2>/dev/null || true
    rm -f /etc/systemd/system/rpiios.service
    systemctl daemon-reload
    systemctl enable --now getty@tty1.service
    log "Kiosk removed; console login restored on tty1"
    exit 0
fi

[[ -x "$QEMU_BIN" ]] || die "emulator not built; run build-emulator.sh first"
[[ -d "$NAND_DIR/cs0" ]] || die "firmware missing; run fetch-firmware.sh first"

# The service runs /opt/rpiios/scripts/run-ios.sh, so the scripts must live
# there even if this repo was cloned elsewhere.
if [[ "$here" != "$RPIIOS_HOME/scripts" ]]; then
    log "Copying scripts to $RPIIOS_HOME/scripts"
    mkdir -p "$RPIIOS_HOME/scripts"
    install -m 0755 "$here"/*.sh "$RPIIOS_HOME/scripts/"
fi

if ! id rpiios &>/dev/null; then
    log "Creating service user 'rpiios'"
    useradd --system --home-dir "$RPIIOS_HOME" --shell /usr/sbin/nologin rpiios
fi
for g in video render input audio; do
    getent group "$g" >/dev/null && usermod -aG "$g" rpiios
done
mkdir -p "$RPIIOS_HOME/log"
chown -R rpiios:rpiios "$RPIIOS_HOME/log"
chmod -R a+rX "$RPIIOS_HOME"

# SDL's KMS/DRM backend needs the full KMS driver (default on current
# Raspberry Pi OS; older images may still use the legacy firmware driver).
config_txt=/boot/firmware/config.txt
[[ -f $config_txt ]] || config_txt=/boot/config.txt
if [[ -f $config_txt ]] && ! grep -q '^dtoverlay=vc4-kms-v3d' "$config_txt"; then
    log "Enabling vc4-kms-v3d in $config_txt (takes effect after reboot)"
    sed -i 's/^dtoverlay=vc4-fkms-v3d/#&/' "$config_txt"
    printf '\n[all]\ndtoverlay=vc4-kms-v3d\n' >> "$config_txt"
fi

# Keep the screen from blanking under the emulator.
cmdline=/boot/firmware/cmdline.txt
[[ -f $cmdline ]] || cmdline=/boot/cmdline.txt
if [[ -f $cmdline ]] && ! grep -q 'consoleblank=0' "$cmdline"; then
    sed -i '1 s/$/ consoleblank=0/' "$cmdline"
fi

log "Installing systemd unit"
install -m 0644 "$here/../systemd/rpiios.service" /etc/systemd/system/rpiios.service
systemctl daemon-reload
systemctl disable getty@tty1.service 2>/dev/null || true
systemctl enable rpiios.service

log "Done. Reboot to boot into iPhone OS: sudo reboot"
log "Logs: journalctl -u rpiios -f   and   $RPIIOS_HOME/log/serial.log"
log "SSH still works for maintenance; undo with: sudo $0 --remove"
