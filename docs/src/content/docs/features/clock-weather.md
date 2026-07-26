---
title: Clock and weather
description: Two clock layouts, real weather data and the three-day forecast screen.
---

## Screens

**Large clock** draws the current time as the dominant element, with date and
weekday above it and the device IP in the bottom bar.

**Clock + current weather** adds a compact weather status mark, configured
location, temperature, humidity, date, weekday and IP without reducing the clock
to an unreadable size.

**Three-day weather** is one screen with equal Today, Tomorrow and Day after
cards. Each card shows condition, average temperature, min/max and humidity.

## Data and errors

The location is requested from `wttr.in` as JSON; no API key is required.
Firmware retains the last valid result through a temporary network error and
refreshes no faster than once per minute. If there is no valid data, the screen
shows a short actionable error instead of invented temperatures.

Timezone controls time, date and weekday. IP always comes from the active device
network interface.

## Preview

The virtual device uses the same 240×240 content model and a representative
palette. It is intended for configuration and flow testing. Browser text is
anti-aliased while the ST7789 uses bitmap glyphs, so exact metrics and edge
pixels can differ on physical hardware.
