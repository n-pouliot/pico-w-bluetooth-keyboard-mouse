# Risks and limitations

## Release status

The artifact remains **PRE-HARDWARE TEST**. Pico W boot and both intended BLE
peripherals have partial PC passes, and both roles passed in The Sims 4 on Xbox
Series X. Complete USB inspection, reconnect/stress, actual current, latency,
and broader Xbox game behavior have not been tested.

## Accepted compatibility limits

| Severity | Limitation | Behavior |
|---|---|---|
| MEDIUM | Only displayed fixed-passkey and Just Works association models are supported | Keyboard responder-input pairing uses public code `739241`; Numeric Comparison, Pico-input passkey, and OOB are declined. Mouse legacy Just Works fallback is accepted; keyboard legacy pairing is rejected. |
| MEDIUM | Exactly one HIDS service instance per peripheral | Multi-HIDS devices are rejected to avoid unsafe shared-store ambiguity. |
| MEDIUM | USB keyboard is 6KRO | Compatible NKRO input is reduced to six keys; >6 reports ErrorRollOver. |
| MEDIUM | Consumer/media/system/macro/vendor inputs are not forwarded | Structurally valid unrelated collections are ignored. |
| MEDIUM | Absolute mice, digitizers, and fields wider than 16 bits are unsupported | Candidate is rejected inertly. |
| MEDIUM | No BLE keyboard LED output | Caps/Num/Scroll LEDs on the wireless keyboard may not track the host. |
| MEDIUM | Re-pairing requires maintenance UF2 then normal UF2 | Chosen to avoid runtime BOOTSEL/QSPI access across two cores. |
| MEDIUM | The keyboard passkey is fixed and mouse Just Works has no peer authentication | The code is public. Keep all unintended devices out of pairing mode; mouse legacy fallback and encryption alone do not remove impersonation/MITM risk. |
| LOW | Experimental VID/PID `CAFE:4008` | Suitable for private testing, not a production USB identity. |
| LOW | One-second reconnect backoff is fixed | Avoids aggressive loops but may feel slower than a tuned per-device strategy. |
| LOW | Mouse motion uses bounded 32-bit accumulation | Normal bursts are distance-preserving and chunked; an extreme overflow is a fault, not a lossless guarantee. |

## Pico W functionality — unverified

- two simultaneous real HOGP connections and sustained notification traffic;
- CYW43439 scan/connection stability while USB is polled at 1 ms;
- stack high-water marks on both cores;
- LED behavior and TLV maintenance behavior;
- current before USB configuration, during radio activity, and during suspend;
- whether suspend/resume radio quiescing meets USB current limits in practice;
- unplugging power during a real TLV page migration.

The compiled resource budget has comfortable static headroom, but it is not a
substitute for these tests.

## Peripheral compatibility — unverified

Names and Appearance are discovery hints only; final classification uses the
Report Map. Devices may still be incompatible because they:

- are Bluetooth Classic rather than BLE HOGP;
- require Numeric Comparison, Pico-input passkey, or OOB; keyboards that only
  support legacy security are also rejected;
- expose multiple HIDS services;
- expose ambiguous duplicate input characteristics for one Report ID;
- use unsupported or malformed descriptors;
- combine keyboard and mouse roles in one device;
- rely on output reports, vendor features, or consumer controls;
- do not advertise while sleeping without a wake action.

## PC USB compatibility — partially verified

Windows accepted keyboard and mouse input through the fixed USB device. A full
descriptor-tree capture must still confirm exactly two interfaces and no extra
class. Boot protocol mouse omits wheel/pan until the host selects report
protocol, as required by the boot mouse shape.

## Xbox compatibility — partially verified

Keyboard input worked when the user connected the Pico to an Xbox Series X. The
dashboard ignored the mouse, which matches Microsoft's documented distinction:
keyboard navigation works on Xbox, while mouse navigation is limited to select
games/apps. Both keyboard and mouse then worked in The Sims 4. Other titles and
the exact console software version remain unrecorded. The firmware intentionally
does not emulate a controller or bypass licensed-accessory authentication.

## Persistence and recovery residual risks

- Power loss after storing a BTstack bond but before committing a role record
  can leave an inert orphan bond. An enrollment attempt that hits that orphan
  requires the clear-all maintenance image for deterministic recovery.
- A corrupted application role record can be deleted, but cannot safely select
  its corresponding BTstack key; clear-all removes every database entry instead
  of guessing which unrelated bond belongs to it.
- BOOTSEL ROM recovery handles bad application flash, not physical damage or an
  unsuitable/charge-only cable.
- Maintenance status LED semantics have not been observed on hardware. Reflash
  normal firmware regardless; never use a maintenance image as normal runtime.

## Latency

USB requests 1 ms polling. BLE connection parameters request 12.5–15 ms with
zero slave latency, but peripherals may negotiate differently. Bounded parsing
should add much less than a frame after receipt. High mouse deltas are split over
several USB frames by design, preserving distance rather than clamping. Actual
end-to-end latency is **NOT MEASURED**.

## License

Inherited BlueKitchen demo terms in `LICENSE.TXT` include a non-commercial
condition. This is a legal/redistribution limitation, not a technical one.
