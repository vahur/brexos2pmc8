# brexos2pmc8

Brexos2pmc8 is a bridge application, which turns Bresser EXOS2 mount into Explore Scientific PMC8 compatible
mount. It is useful in cases when native Bresser EXOS2 support is not available, but ES PMC8 is, like in ASIAIR.

Handcontroller is not used and must be disconnected as otherwise both, the bridge app and the controller, will try to
manage mount's motors.

## Hardware requirements

* USB to serial cable with 5V signal level. I've been using https://ftdichip.com/products/ttl-234x-5v-we/. It is a wire
ended version, so RJ45 plug needs to be crimped to the wires.

### RJ45 plug pinout

| Pin | Signal | TTL-234x wire color |
|-----|--------|---------------------|
| 4   | RXD    | Yellow              |
| 5   | TXD    | Orange              |
| 8   | GND    | Black               |

## Installation on ASIAIR

1. Disconnect hand controller from the mount
2. Connect USB to TTL serial cable to DEC motor's second port and
to ASIAIR's free USB port.
3. Log into ASIAIR and make root fs writable
```
sudo mount / -o rw,remount
```
4. Copy brexos2pmc8 binary to ASIAIR's /usr/local/bin directory.
5. Create udev rule file for USB to serial converter. Replace vendor id and product id to match your serial converter.
You can use `lsusb` utility to find these values.

/etc/udev/rules.d/99-brexos2.rules:
```
# Bresser EXOS2 Mount
SUBSYSTEM=="tty", ACTION=="add" ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6015", SYMLINK+="brexos2", TAG+="systemd"
```
6. Create systemd service for bridge app.

/home/pi/brexos2pmc8.service:
```
[Unit]
Description=Bresser EXOS2 to PMC8 Bridge
After=dev-brexos2.device
BindsTo=dev-brexos2.device

[Service]
Type=simple
ExecStart=/usr/local/bin/brexos2pmc8

[Install]
WantedBy=dev-brexos2.device
```
7. Link and enable the service:
```
sudo systemctl link /home/pi/brexos2pm8.service 
sudo systemctl enable brexos2pm8
```
8. Make root fs read-only and reboot:
```
sudo mount / -o ro,remount
sudo systemctl reboot
```

9. Open ASIAIR app and go to mounts page.
10. Select "Explore Scientific EXOS2" from the list.
11. Select "Wi-Fi" as interface, IP = 127.0.0.1 and port = 8888.
12. Turn on the connect toggle switch.
