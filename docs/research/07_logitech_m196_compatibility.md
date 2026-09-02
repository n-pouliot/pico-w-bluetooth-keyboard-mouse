# Logitech M196 compatibility investigation

Date: 2026-09-02

## Primary-source device facts

Logitech identifies the M196 as model M-R0114, connection type Bluetooth Low
Energy 5.0, VID/PID `046d:b03f`, 133 Hz report rate, and a three-button mouse:

- <https://support.logi.com/hc/en-150/articles/21966295454231-Specification-M196>

Logitech's pairing procedure is to hold the bottom on/off/pairing button for
three seconds until its LED blinks:

- <https://support.logi.com/hc/en-us/articles/21966294479895-Getting-Started-M196>

These facts establish that the M196 uses the bridge's BLE transport and that
the user's pairing-button procedure was correct. Logitech does not publish the
mouse's precise SMP key-size, authentication, or LE Secure Connections
requirements on those pages.

## Standards evidence

The Bluetooth SIG HOGP implementation-conformance statement makes
Unauthenticated Pairing, LE Security Mode 1 Level 2, mandatory for a central
Report/Boot Host; authenticated Level 3 pairing is optional:

- <https://files.bluetooth.com/wp-content/uploads/2024/10/HOGP.ICS.p8.pdf>

The initial bridge required a 16-byte LE Secure Connections bond for every
role. That was a security-hardened subset, not the full mandatory HOGP host
baseline. The compatibility policy now keeps the keyboard requirement strict
while allowing a mouse bond with any valid BLE encryption key size from 7 to
16 bytes and without requiring the Secure Connections flag.

## Physical evidence and conclusion

1. The passive-scan release did not enroll the M196.
2. Active scanning caused a visible operation but still rejected it.
3. The failure-stage image repeatedly displayed stage 2 (security).
4. The mouse-only Level 2 compatibility image paired the M196 and produced
   mouse input on Windows.
5. The LED showed mouse-only readiness, then solid readiness after the saved MX
   Mechanical reconnected; both devices produced input.

This sequence isolates the original failure to the bridge's overly narrow
mouse security policy. It does not reveal whether this individual M196 used a
shorter key, legacy pairing, or another Level 2 combination; an SMP trace would
be needed to distinguish those. The broader policy is standards-based and
should improve compatibility with other ordinary BLE HOGP mice.

The tradeoff is explicit: mouse Just Works has no authenticated peer identity,
and legacy pairing may provide weaker protection than LE Secure Connections.
The 180-second first-run enrollment window, one-candidate serialization, saved
identity, and ignoring unknown devices outside enrollment contain exposure.
The authenticated keyboard policy is unchanged.
