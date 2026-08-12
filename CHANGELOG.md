# Changelog

All notable changes to Sibyl are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.0] - 2026-08-11

First release.

### Added

- **Signal identification.** Capture a Sub-GHz transmission and rank it against ten
  device classes: gate remote, car key fob, tyre sensor, weather sensor, doorbell,
  remote socket, alarm sensor, blind motor, meter/telemetry and industrial remote.
- **Two-track answers.** Anything the Flipper's own decoder stack recognises is named
  outright. Anything it does not is fingerprinted from the raw pulse train.
- **Honesty ceilings.** A fingerprint can never be promoted to `CONFIRMED`, and a
  decoded encoder chip (Princeton, EV1527, Holtek, KeeLoq) is reported as a chip with a
  shortlist rather than passed off as a product.
- **Feature extraction** measuring the symbol quantum Te, quantisation fit, distinct
  pulse widths, duty cycle, estimated bit length, repeat count and inter-repeat gap —
  pure integer logic, no floats.
- **Raw burst capture** with segmentation and an acceptance test that uses the app's own
  Te fit as its noise gate, so nothing needs tuning per band or per gain setting.
- **Result screen** with three pages: the answer, the shortlist with scores, and the
  evidence — including the captured packet drawn as a logic trace.
- **Device library** with a four-part explainer per class (what it is, how it works,
  security, and what Sibyl cannot tell you), browsable without capturing anything.
- **Listening screen** with a scrolling carrier trace, per-packet marks and an honest
  rejected-noise counter.
- **Find band**, sweeping all sixteen bands and scoring each against its own noise floor.
  Adopting the winner drops straight into listening on it.
- **Auto modulation**, walking the AM and FM presets while nothing is landing, so an FSK
  device is not silently inaudible under an OOK preset.
- **Session list** of what has been identified, held in RAM only.
- Sixteen bands including 433.42 (Somfy RTS) and 434.42, persistent settings, and
  configurable sound, vibro and LED feedback.

### Notes

- Listen-only. Sibyl never transmits, replays or clones, and does not store codes.
- ~4,800 host checks run under ASan/UBSan in CI, alongside firmware builds against both
  the release and dev SDK channels.
- The README screenshots are generated from the real classifier's own output rather than
  drawn by hand, so they cannot drift from what the app does.

[1.0]: https://github.com/at0m-b0mb/Sibyl-FlipperZero/releases/tag/v1.0
