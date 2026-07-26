---
title: Gallery
description: Upload and display JPEG photos and animated GIF files.
---

Gallery files live in LittleFS and are managed from the Gallery tab. Upload
accepts `.jpg`, `.jpeg` and `.gif`, rejects unsafe names and stops at 600 KB.
For predictable quality use 240×240 assets.

JPEG can be cropped to fill or contained with letterboxing. Animated GIF is
decoded frame by frame and respects the file delay values; it loops until the
gallery advances to the next item. Transparent pixels preserve the background.

The settings preview uses a representative photo frame because browser preview
cannot read a physical device's private LittleFS file until the file is uploaded
to that device.
