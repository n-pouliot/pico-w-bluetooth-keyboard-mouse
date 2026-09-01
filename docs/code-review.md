# Independent code-review report

## Scope and verdict

An independent reviewer audited Git object `7561691` against pinned Pico SDK
2.2.0 and BTstack. The initial verdict was **NOT APPROVED**: two CRITICAL, four
HIGH, three MEDIUM, and one LOW finding correctly blocked release.

Remediation landed in `1cb8f15` and `2b4255c`. The source was rebuilt with
strict project warnings, host tests, sanitizers, static analysis, and two clean
Pico W builds. Every source finding below is closed for the software-only gate;
the artifact remains PRE_HARDWARE_TEST because no physical behavior was
verified.

## Findings and resolution

| ID | Severity | Finding | Resolution and status |
|---:|---|---|---|
| CR-1 | CRITICAL | Normal BTstack notification callbacks were rejected, so accepted devices forwarded no input. | CLOSED — both documented callback packet forms and event framing are accepted and policy-tested (`1cb8f15`). |
| CR-2 | CRITICAL | Multiple HIDS services could enter unsafe shared Report Map cleanup in the pinned client. | CLOSED — application preflight requires exactly one HIDS service and BTstack is configured for one (`1cb8f15`). |
| H-1 | HIGH | Descriptor compilation used about 1,416 B of a default 2 KiB Core-1 stack. | CLOSED — static scratch, explicit 8 KiB Core-1 stack and 128 B canary; largest observed project frame 160 B (`1cb8f15`). |
| H-2 | HIGH | One global timer could be stolen or cancelled by the other device context. | CLOSED — timers carry context, state, and attempt-token ownership (`1cb8f15`). |
| H-3 | HIGH | Late connection completion could be assigned to a newer attempt. | CLOSED — exact peer address/type matching and a retained cancel-pending state (`1cb8f15`). |
| H-4 | HIGH | Valid traffic on one Report ID could mask malformed traffic and retain input from another. | CLOSED — first malformed active-role report releases and quarantines the role (`1cb8f15`). |
| M-1 | MEDIUM | Failed RPA-resolution start left a global request pending forever. | CLOSED — checked start, token-owned timeout, and bounded backoff (`1cb8f15`). |
| M-2 | MEDIUM | Missing disconnect completion could permanently consume the operation slot. | CLOSED — bounded retry, radio-cycle recovery, persistent restart intent, and retry after a rejected power request (`1cb8f15`, `2b4255c`). |
| M-3 | MEDIUM | A rejected newly bonded candidate could leave an orphan; the first cleanup revision could sweep unrelated new entries. | CLOSED — removal now requires the exact identity and index in a slot proven empty before enrollment; unrelated-slot policy tests pass (`2b4255c`). |
| L-1 | LOW | Specific keyboard/mouse GAP Appearance was discarded before role classification. | CLOSED — bounded Appearance parsing and role-conflict rejection (`1cb8f15`). |

## Areas reviewed without a separate defect

The review found no separate source-level issue in mailbox generation/release
barriers or TinyUSB completion-token handling under documented callback
ordering. Full BTstack controller timing is not host-simulated; physical reset,
suspend, transfer failure, and disconnect tests remain mandatory.

## Independent build findings

The initial verifier showed static two-interface USB descriptors and same-day
repeatability but rejected the stale upstream binaries and stale documentation.
Those files were removed. Two new empty-directory builds reproduced all four
final UF2 hashes; `release/SHA256SUMS.txt` records the package.

UF2 metadata embeds the build date, so cross-date byte identity is not claimed.
SDK revisions must still be checked before release builds. `CAFE:4008` remains
an experimental private-test VID/PID, and a legal configuration descriptor does
not prove physical current.

## Physical evidence boundary

No reviewer connected a Pico W, BLE keyboard, BLE mouse, PC USB analyzer, power
meter, or Xbox. BLE interoperability, suspend current, PC behavior, and Xbox
acceptance remain **NOT TESTED**.
