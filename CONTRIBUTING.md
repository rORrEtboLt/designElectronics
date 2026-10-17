# Contributing

Thanks for helping improve designElectronics.

## Contributor License Agreement

Before a pull request can be merged, you need to sign the
[Contributor License Agreement](CLA.md). The CLA bot will comment on your
first pull request with a link; signing once covers all future
contributions. You keep the copyright to your work; the CLA lets the
project be maintained and relicensed in the future.

## What to contribute

Parts are the easiest way to contribute: add a generator in `src/partlib.cpp`
(three small vector drawings + pin positions) and register it in
`generators()` and `partLibrary()`.

## Before you send a pull request

- Run `--selftest` and make sure it passes.
- Keep changes focused; one feature or fix per pull request.
- If you add third-party code or assets, say where they come from and what
  license they use. Only permissive licenses (MIT, BSD, zlib, Apache-2.0,
  CC0, OFL for fonts) can be accepted.
