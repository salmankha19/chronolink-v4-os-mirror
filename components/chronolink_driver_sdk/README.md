# ChronoLink Driver SDK

## Purpose
Public SDK for registering custom display backends at runtime.

## Usage
Fill a `chronolink_display_driver_t`, call `chronolink_driver_register()`, then call `HAL_Display_Init()`.

## Template
See `template/template_custom_display.c` for a minimal starting point.

## Limitation
Only one custom driver can be registered at a time. For multiple custom drivers, extend the router later.
