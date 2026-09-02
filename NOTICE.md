# Attribution and project lineage

`pico-w-bluetooth-keyboard-mouse` is a derivative work built from Shiomachi Software's
[`picow_ble_usb_hid_bridge`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge).

The exact starting point was:

- upstream tag: [`20260830_7`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/tree/20260830_7);
- upstream commit: [`2c6a303d1f172e56b271283af978efdcc483a389`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/commit/2c6a303d1f172e56b271283af978efdcc483a389);
- upstream author/publisher: [Shiomachi Software](https://github.com/shiomachisoft).

The upstream Git history through that commit remains in this repository. An
`upstream` remote is also configured in the development checkout. A GitHub
“fork” badge is repository metadata, not a licensing requirement; this
standalone derivative retains both source history and explicit attribution.

## What came from upstream

The upstream project established the Raspberry Pi Pico W/Pico 2 W foundation
for operating as a BLE HID central and forwarding input to a USB host. It uses
Raspberry Pi's Pico SDK integration of BlueKitchen BTstack and TinyUSB's
`dev_hid_composite` example lineage.

The upstream README also credits
[`mateibarbu19`](https://github.com/mateibarbu19) for changes in its 20260810
release.

## What this derivative adds

The work after the baseline replaces the original single-device dynamic HID
pass-through design with:

- simultaneous BLE keyboard and mouse connections;
- two fixed, boot-capable USB HID interfaces;
- bounded Report Map parsing and canonical input translation;
- independent role persistence, reconnection, and release barriers;
- fixed-passkey keyboard enrollment and broader HOGP mouse compatibility;
- role-specific maintenance firmware and LED failure diagnostics;
- native tests, static/build reviews, reproducible artifacts, and beginner
  documentation; and
- physical Windows 11 and Xbox Series X / The Sims 4 validation.

## License and names

All inherited notices and license terms are reproduced in [LICENSE.TXT](LICENSE.TXT).
In particular, the inherited BlueKitchen demo terms restrict use to personal,
non-commercial purposes and prohibit commercial purpose or monetary gain.

No upstream author or dependency provider endorses this derivative. Xbox is a
trademark of Microsoft; Logitech product names are trademarks of Logitech.
