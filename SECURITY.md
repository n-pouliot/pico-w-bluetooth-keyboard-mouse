# Security policy

## Reporting a vulnerability

Please use GitHub's private
[security advisory form](https://github.com/n-pouliot/xbox-pico/security/advisories/new)
for a suspected vulnerability. Do not publish Bluetooth keys, device addresses,
private diagnostics, or a working exploit in a public issue.

Useful reports include the affected commit/UF2 hash, board model, peripheral,
reproduction steps, observed LED pattern, and expected impact.

## Security boundary

The bridge accepts unknown BLE candidates only during a bounded enrollment
window for an empty role. Keyboards require authenticated Secure Connections;
mice use unauthenticated encrypted Just Works and may use legacy fallback for
interoperability. Mouse enrollment therefore does not protect against an active
nearby impersonator. Keep unintended devices out of pairing mode during setup.

This project does not implement Xbox controller authentication, console
modification, or a security bypass. Reports requesting those features are out
of scope.

The project is beta, non-commercial software supplied without warranty under
the terms in [LICENSE.TXT](LICENSE.TXT).
