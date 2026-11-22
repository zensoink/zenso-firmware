# Domain Vocabulary

**Firmware Version**:
The semantic version string identifying the embedded software running on a physical Device.
_Avoid_: Software version, build number, OS version, image version

**Firmware Release**:
An official compiled firmware asset package published with cryptographic checksums in a release repository.
_Avoid_: Firmware build, system update file, ROM, flash image

**Over-The-Air (OTA) Update**:
The automated or manual procedure whereby a physical Device downloads, verifies, flashes, and reboots into new firmware.
_Avoid_: Device flash, remote reflash, wireless install, firmware push

**OTA Handshake**:
The bidirectional protocol during Device Check-In that exchanges update directives and device telemetry between server and hardware.
_Avoid_: Firmware negotiation, update polling, OTA check

**Update Status**:
The lifecycle progression state of a firmware update on a device (`idle`, `pending`, `updating`, `success`, `failed`).
_Avoid_: Upgrade status, flash state, deployment progress, update stage

**Auto-Update**:
A per-device configuration that automatically schedules an Over-The-Air Update whenever a newer compatible firmware release is available.
_Avoid_: Automatic patch, self-update, rolling update, background update

**Firmware Proxy**:
An authenticated server streaming endpoint that delivers firmware binaries directly to IoT devices, shielding them from third-party hosting redirects.
_Avoid_: Firmware relay, binary mirror, download gateway, asset bridge

**Safe Boot**:
The dual-bank flash verification mechanism ensuring newly flashed firmware must prove health before cancellation of automatic hardware rollback.
_Avoid_: Safe mode, rescue boot, recovery startup
