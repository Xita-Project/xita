# Updater trust store

`release-ca.pem` is the unmodified Mozilla certificate bundle distributed by curl,
retrieved on 2026-09-26. Source: https://curl.se/ca/cacert.pem.
The SHA-256 was checked against curl's published checksum:
`a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`.
License: MPL-2.0, in `LICENSES/Mozilla-MPL-2.0.txt`.

This becomes `app0:release-ca.pem` in the VPK and is covered by the package
contract. Refreshing it requires a full VPK release, not a runtime-only update.
