# CEC Injector Mate Compatibility Capture

## Capture

- State: Completed
- Start: 2024-08-29T00:00:00.000Z
- End: 2024-08-29T00:00:00.600Z
- Duration: 0 s
- Platform: macOS
- Mate revision: deadbeef
- Mate version: 0.0.99

## Device

- DeviceId: compat-device
- Session: Connected
- Logical address: Playback 1 (4)
- Wake supported: yes
- Wake configured: yes
- Wake armed: no

## Display

- Display summary: Guest TV
- EDID available: yes
- EDID bytes: 256
- Physical Address status: Available
- Physical Address: 1.0.0.0

## Configuration

- Revision: 7
- power_on_boot: yes
- power_on_resume: yes
- standby_on_suspend: yes
- standby_on_shutdown: yes
- suspend_on_tv_standby: yes
- wake_host_on_tv_source_request: yes
- remote_enabled: no
- remote_profile: SteamOsParity

## Capabilities

- configuration: yes
- remote_input: no
- remote_wake: no
- tv_power_actions: yes
- cec_monitor: yes

## Timeline

- [00:00.020] Status: Session: Connected -> Disconnected
- [00:00.100] RX 0:F 36 TV standby
- [00:00.120] TX 4:0 82 10 00 Active Source
- [00:00.200] Action request #41 MakeActiveSource
- [00:00.350] Action #41 MakeActiveSource -> Timeout (135 ms) TV switched anyway
- [00:00.375] Physical result for #41: Worked — TV switched to Mac HDMI
- [00:00.400] Note: TV switched physically
- [00:00.450] Marker: Active Source test

## Action Summary

- Action #41 MakeActiveSource -> Timeout (135 ms) TV switched anyway
  Physical: Worked — TV switched to Mac HDMI

## CEC Summary

- RX frames: 1
- TX frames: 1
- Retained event rows: 2
- Dropped event rows: 0
- Opcode frequency:
  36: 1
  82: 1
- Semantic frequency:
  Active Source: 1
  TV standby: 1

## Notes

- [00:00.400] TV switched physically

## Compatibility Summary

- Final session state: Disconnected
- Final Physical Address status: Available
- Final Physical Address: 1.0.0.0
- MakeActiveSource backend: Timeout; physical: Worked
