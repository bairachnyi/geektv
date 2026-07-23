#pragma once
#include "Mode.h"
#include "Settings.h"
#include <Arduino.h>

struct PhotoItem {
  String name;
  size_t size;
};

class GalleryMode : public DisplayMode {
public:
  const char* id() const override { return "gallery"; }
  uint8_t modeConst() const override { return MODE_GALLERY; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

  uint8_t photoCount() const { return m_photoCount; }
  const PhotoItem& photoAt(uint8_t idx) const;

private:
  void scanPhotos();
  void nextPhoto(const Settings& s);
  void renderCurrent(const Settings& s);

  PhotoItem m_photos[MAX_PHOTOS];
  uint8_t   m_photoCount = 0;
  int8_t    m_currentIdx = -1;
  uint32_t  m_nextRotateMs = 0;
};

extern GalleryMode g_galleryMode;
