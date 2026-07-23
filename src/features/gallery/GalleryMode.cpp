#include "GalleryMode.h"
#include "Gfx.h"
#include "Net.h"
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <TJpg_Decoder.h>

GalleryMode g_galleryMode;

static PhotoItem s_emptyPhoto = {"", 0};

// TJpgDec callback: push decoded JPEG pixels directly to the ST7789 display
static bool jpgDrawCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  Arduino_GFX* g = gfxDev();
  if (!g) return false;
  g->draw16bitBeRGBBitmap(x, y, bitmap, w, h);
  return true;
}

const PhotoItem& GalleryMode::photoAt(uint8_t idx) const {
  if (idx < m_photoCount) return m_photos[idx];
  return s_emptyPhoto;
}

void GalleryMode::scanPhotos() {
  m_photoCount = 0;
  if (!LittleFS.exists("/photos")) {
    LittleFS.mkdir("/photos");
  }

  Dir dir = LittleFS.openDir("/photos");
  while (dir.next()) {
    if (m_photoCount >= MAX_PHOTOS) break;
    String name = dir.fileName();
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) name = name.substring(lastSlash + 1);
    String lowerName = name;
    lowerName.toLowerCase();
    if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg") || lowerName.endsWith(".raw")) {
      m_photos[m_photoCount].name = "/photos/" + name;
      m_photos[m_photoCount].size = dir.fileSize();
      m_photoCount++;
    }
  }
}

void GalleryMode::begin(const Settings& s) {
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpgDrawCallback);
  scanPhotos();
  m_currentIdx = m_photoCount ? 0 : -1;
  m_nextRotateMs = millis() + (uint32_t)s.gallery.rotateSec * 1000UL;
}

void GalleryMode::invalidate(const Settings& s) {
  scanPhotos();
  if (m_photoCount > 0) {
    if (m_currentIdx < 0 || m_currentIdx >= m_photoCount) m_currentIdx = 0;
  } else {
    m_currentIdx = -1;
  }
  if (s.mode == MODE_GALLERY) {
    renderCurrent(s);
  }
}

void GalleryMode::wake(const Settings& s) {
  scanPhotos();
  if (m_photoCount > 0 && (m_currentIdx < 0 || m_currentIdx >= m_photoCount)) {
    m_currentIdx = 0;
  }
  m_nextRotateMs = millis() + (uint32_t)s.gallery.rotateSec * 1000UL;
  renderCurrent(s);
}

void GalleryMode::nextPhoto(const Settings& s) {
  if (m_photoCount == 0) {
    m_currentIdx = -1;
    return;
  }

  if (s.gallery.randomOrder && m_photoCount > 1) {
    m_currentIdx = (m_currentIdx + 1 + (rand() % (m_photoCount - 1))) % m_photoCount;
  } else {
    m_currentIdx = (m_currentIdx + 1) % m_photoCount;
  }
}

void GalleryMode::service(const Settings& s) {
  if (s.mode != MODE_GALLERY && s.mode != MODE_CAROUSEL) return;

  if (m_photoCount > 0 && (int32_t)(millis() - m_nextRotateMs) >= 0) {
    m_nextRotateMs = millis() + (uint32_t)s.gallery.rotateSec * 1000UL;
    nextPhoto(s);
    renderCurrent(s);
  }
}

void GalleryMode::renderCurrent(const Settings& s) {
  if (m_photoCount == 0 || m_currentIdx < 0 || m_currentIdx >= m_photoCount) {
    gfxClear();
    gfxDrawCentered("NO PHOTOS", 100, 3, C_WHITE);
    gfxDrawCentered("Upload via Web UI", 140, 2, C_GRAY);
    return;
  }

  const PhotoItem& item = m_photos[m_currentIdx];
  String lowerName = item.name;
  lowerName.toLowerCase();

  // JPEG: decode directly to display (10-30 KB, fits in heap)
  if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) {
    gfxClear();
    // TJpgDec reads from LittleFS directly
    TJpgDec.drawFsJpg((int32_t)0, (int32_t)0, item.name, LittleFS);
    return;
  }

  // RAW RGB565 fallback (115 KB)
  if (item.size == 115200) {
    File f = LittleFS.open(item.name, "r");
    if (!f) {
      gfxClear();
      gfxDrawCentered("ERROR", 100, 3, C_RED);
      return;
    }
    uint8_t rowBuf[480];
    for (int y = 0; y < 240; y++) {
      if (f.read(rowBuf, 480) == 480) {
        for (int x = 0; x < 240; x++) {
          uint16_t color = (rowBuf[x * 2] << 8) | rowBuf[x * 2 + 1];
          gfxDrawPixel(x, y, color);
        }
      }
    }
    f.close();
    return;
  }

  // Unsupported format
  gfxClear();
  String nameOnly = item.name;
  int lastSlash = nameOnly.lastIndexOf('/');
  if (lastSlash >= 0) nameOnly = nameOnly.substring(lastSlash + 1);
  gfxDrawCentered("UNSUPPORTED", 80, 3, C_RED);
  gfxDrawCentered(nameOnly.c_str(), 120, 2, C_WHITE);
}
