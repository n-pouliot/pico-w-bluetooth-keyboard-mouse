# Adversarial review report

## Scope and verdict

A separate red-team reviewer attacked immutable commit `7561691` across callback
dispatch, connection ordering, pairing, descriptors, persistence, cross-core
release behavior, USB protocol transitions, suspend, and packaging. Its initial
**NOT READY** verdict reported two CRITICAL, five HIGH, seven MEDIUM, and three
LOW findings. Remediation landed in `1cb8f15` and `2b4255c`; packaging and
recovery-document corrections followed in the final artifact commit.

The software-only disposition is **READY FOR HARDWARE TEST** with one accepted
BTstack metadata residual and two explicit hardware-only evidence boundaries.

## Findings and disposition

| ID | Severity | Attack or failure | Disposition |
|---:|---|---|---|
| RT-1 | CRITICAL | HIDS notifications used a callback packet type rejected by the app. | CLOSED — corrected, bounds-checked, and policy-tested (`1cb8f15`). |
| RT-2 | CRITICAL | Multiple HIDS services could trigger unsafe shared-map cleanup. | CLOSED — one-service preflight before HIDS discovery (`1cb8f15`). |
| RT-3 | HIGH | Rejected Just Works candidates could exhaust the finite bond database. | CLOSED — only the exact, newly occupied candidate slot can be removed (`2b4255c`). |
| RT-4 | HIGH | One peer could steal the other peer's global operation timer. | CLOSED — context/state/token-owned timers (`1cb8f15`). |
| RT-5 | HIGH | Late completion after cancellation could attach to a newer attempt. | CLOSED — peer matching and cancel-pending state (`1cb8f15`). |
| RT-6 | HIGH | Malformed release followed by silence could retain a key or button. | CLOSED — release and quarantine on first malformed active-role report (`1cb8f15`). |
| RT-7 | HIGH | Tracked UF2s/docs belonged to the old single-device baseline. | CLOSED — stale files removed; four clean-build, reproduced, hashed PRE_HARDWARE_TEST/maintenance files packaged. |
| RT-8 | MEDIUM | Address-resolution start failure blocked later RPA work. | CLOSED — checked start, timeout token, and backoff (`1cb8f15`). |
| RT-9 | MEDIUM | Corrupt/partially cleared role state could leave an orphan bond. | CLOSED for recovery design — clear-all removes and verifies every database entry; docs require clear-all after interrupted/failed role clear. Physical power-loss test remains open. |
| RT-10 | MEDIUM | A candidate could authorize after the 120-second deadline. | CLOSED — live deadline checked at confirmation and commit (`1cb8f15`). |
| RT-11 | MEDIUM | `SET_PROTOCOL` on one USB interface resynced both roles. | CLOSED — per-interface resync (`1cb8f15`). |
| RT-12 | MEDIUM | BLE stayed active during USB suspend. | SOURCE CLOSED — suspend quiesces BLE and resume restarts it (`1cb8f15`); physical current is NOT TESTED. |
| RT-13 | MEDIUM | Duplicate input characteristics sharing one Report ID are ambiguous because the pinned HIDS API omits value-handle metadata. | ACCEPTED RESIDUAL — strict framing/length and immediate invalid-input disconnect contain it; compatibility is a hardware gate. |
| RT-14 | MEDIUM | Specific GAP Appearance conflicts were discarded. | CLOSED — preserved and checked against classified role (`1cb8f15`). |
| RT-15 | LOW | Boot mouse included buttons outside the three-button boot shape. | CLOSED — boot protocol masks to three buttons (`1cb8f15`). |
| RT-16 | LOW | Mouse accumulation saturation contradicted a lossless claim. | CLOSED — overflow fails safely and docs describe only bounded normal chunking (`1cb8f15`). |
| RT-17 | LOW | `CAFE:4008` is not an allocated production USB identity. | ACCEPTED LIMITATION — private experimental testing only. |
| RT-18 | MEDIUM | Rejected radio power-off could clear manager state and leave no recovery branch. | CLOSED — restart intent persists and retries after backoff; pure transition regression test added (`2b4255c`). |

## Scenario and automated coverage

Review reasoned through both connection orders; all saved-role combinations;
sleep/reconnect; security failures; unknown and competing candidates; late
events; RPA failure; held input then disconnect/malformed data; multiple Report
IDs; malformed and multi-service maps; stale/corrupt records; reboot; USB
protocol changes; suspend; and stale packaging.

Host tests cover parser, persistence, mailbox, callback, Appearance, address,
radio, and exact bond-removal policies. They do not compile the full BTstack
state machine into a simulated controller. CYW43439 timing, real flash power
loss, TinyUSB reset/suspend behavior, current, and console behavior remain
separate hardware evidence gaps rather than claimed passes.

## Positive controls retained

- fixed USB keyboard and mouse grammar rather than BLE-derived descriptors;
- strict CRC-protected role records and 16-byte bond checks;
- bounded allocation-free Report Map parsing;
- generation-tagged cross-core release barriers; and
- no controller emulation, proprietary console protocol, or runtime storage.

No physical hardware was used. Xbox compatibility remains **NOT TESTED**.
