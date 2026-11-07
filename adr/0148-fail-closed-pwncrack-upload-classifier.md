---
id: '0148'
title: "Fail-closed pwncrack upload-response classifier over the measured JSON contract"
type: architecture
status: accepted
date: 2026-11-07
supersedes: []
superseded_by: []
---

## Context

`PwncrackService::uploadHandshake` decided success with a substring heuristic: accept any HTTP
2xx whose lowercased response lacked `error`/`invalid`/`denied`/`unauthorized`. Its own comment
admitted the guess ("The body wording isn't documented, so we log the full response above for
refinement during field testing"). That is **fail-open**: a reworded 2xx, a Cloudflare HTML 200
interstitial, or a body truncated before any keyword all pass the check, so the capture is marked
`pwncrackStatus = UPLOADED` when pwncrack never took it. Once marked, the sync never re-queues it,
so the capture is stranded: never cracked, never retried.

The response contract is now known, not guessed. The sibling project esp32-wifi-sapper ran a live
four-leg probe against `POST pwncrack.org/upload_handshake` on 2026-11-05 (its `ADR-0109`
amendment, `artifacts/pwncrack-response-probe.txt`) and re-verified it on air from an ESP32 device
on 2026-11-06. Measured:

- **Accept** and **re-upload** both return HTTP 200 `{"success": true}` (a ~16-byte JSON object).
  pwncrack has **no duplicate signal**: a fresh submit and its re-submit are indistinguishable.
- **Bad key** returns HTTP 400 `{"error": "Invalid session key."}`. Auth is validated
  **synchronously**, so a misprovisioned key fails loud on the upload reply.
- **A malformed hashline** also returns HTTP 200 `{"success": true}`: content is validated
  **asynchronously** server-side, so the upload reply cannot distinguish a crackable line from junk.

The malformed-line leg means the success token is not a crackability guarantee, only an
"accepted for processing" one. That is acceptable here for the same reason it was in the sibling:
the firmware only ever uploads a well-formed line (`buildEapolLine`/`buildPmkidLine`,
`test_hc22000_format` pins them against golden vectors), and the `.22000`/`.pcap` persist on SD
with WPA-SEC classifying the `.pcap` on its own independent path. Unlike WPA-SEC, where a
false-accept would have dropped the only copy of a capture (the hazard the sibling's `ADR-0076`
addressed), a pwncrack false-accept here loses **no** handshake. It still strands the capture at
pwncrack, which is the defect this ADR closes.

## Decision

Replace the substring heuristic with a pure, host-tested classifier,
`pwncrackUploadAccepted(httpStatus, body, len) -> bool` in `src/modules/network/pwncrack_response.*`,
and call it from `uploadHandshake` after splitting the response body from its headers.

1. **Fail-closed, positive-only.** It returns `true` **only** when `httpStatus` is 200 **and** the
   body is a JSON *object* whose `success` is boolean `true`. Everything else returns `false`
   (non-200, nullptr/empty/over-bound body, non-JSON, a non-object JSON root, a `success` that is
   missing, `false`, or not a real boolean), so the capture is kept at `NOT_UPLOADED` and retried,
   never assumed uploaded. The HTTP 400 bad-key case therefore fails loud as a reject.

2. **Parse JSON, not substrings.** A Cloudflare HTML challenge served with HTTP 200 does not parse
   and is rejected, where a substring search for "success" could false-accept it. `success` must be
   a real JSON boolean: a string `"true"` or a numeric `1` is rejected, so a truthy-but-wrong-typed
   field never slips through.

3. **A plain `bool`, not an enum.** There are exactly two measured outcomes and no duplicate
   branch, so `bool` is the honest shape (KISS). The caller maps `true` to
   `setPwncrackStatus(UPLOADED)` + `UploadResult::sent()`, `false` to
   `UploadResult::failed("server rejected")`, with the full response already at `LOG_D`.

4. **Bounded parse, no heap sized to the reply.** A body over `kPwncrackResponseMax` (256 bytes;
   the measured bodies are 16 to 32) is rejected before the parser is touched, so nothing is ever
   allocated proportional to the reply. This is the same no-PSRAM TLS discipline that governs the
   rest of the upload path on this chip.

This stays two concrete classifiers, not a shared interface: WPA-SEC classifies hcxpcapngtool's
verbose **text** with positive-token matching, pwncrack classifies a **JSON** object. The two
contracts have nothing to factor, so at n=2 the rule of three keeps them separate siblings (the
same call the sibling's `ADR-0109` made).

## Consequences

- A pwncrack upload is marked `UPLOADED` only on the one measured success shape. A server reword,
  an HTML interstitial, a truncated body, or a 400 bad key now keeps the capture queued and
  retried, and a bad key surfaces as a repeated visible reject instead of a silent false "sent".
- The classifier is unit-tested on the native lane against the four measured response bodies plus
  the fail-closed cases (`test/test_pwncrack_response`), with no device needed. Its fixtures are
  frozen on measured data, not a guessed schema.
- If pwncrack ever grows a real duplicate or per-line validity signal, the bool widens to an enum
  and this ADR is superseded, with a fresh probe as the evidence. Until measured, the contract
  stays exactly what the probe showed.
