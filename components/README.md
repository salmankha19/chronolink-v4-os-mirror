# Components

This repository uses ESP-IDF's flat component layout: every component
is a direct child of `components/`. ESP-IDF does not support nested
component directories, so layer membership is by naming convention
and by the tables in `docs/Architecture.md` section 1, not by folder
nesting.

| Component | Layer |
|---|---|
| `chronolink_hal`, `chronolink_driver_sdk` | HAL (2) |
| `hil` | HIL (3) |
| `core_os`, `event_bus` | Kernel (4) |
| `state_manager`, `remote_bridge`, `svc_network`, `svc_time` | Services (5) |
| `display_manager`, `font_engine`, `font`, `ui_producer` | Application (6) |
| `boot_manager` | Orchestrator (not a layer) |
| `audio_test` | Dev/test (Kconfig-gated, not shipped) |

## Naming convention

New service components use the `svc_` prefix. Two services predate
the convention (`state_manager`, `remote_bridge`); they are listed
above and may be renamed in a future cleanup PR.

Application-band components do not use a prefix. The app band is the
top of the stack; its callers are all in `main/` or in the component
itself.

## Adding a new component

Pick the layer, then:

- Service: name it `svc_<purpose>` (e.g. `svc_config`, `svc_prayer_engine`)
- HAL: add a source file to `chronolink_hal/src/hal_<peripheral>.c`
- HIL adapter: add `<role>_api.c` inside `hil/src/`
- Application: use a descriptive name (`ui_producer`, `clock_face`)
- Update this table and `docs/Architecture.md` section 1 in the same
  commit that adds the component
