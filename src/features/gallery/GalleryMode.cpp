#include "GalleryMode.h"
#include "Gfx.h"
#include "Net.h"
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>
#include <new>

GalleryMode g_galleryMode;

// JPEG/RAW decoding writes scanlines directly to the panel. A short backlight
// fade hides that unavoidable SPI rasterisation without allocating a second
// 240x240 framebuffer (which would be unsafe on the ESP8266). The panel is
// already black while the next image is decoded, then fades back in quickly.
static void galleryFade(uint8_t from, uint8_t to, bool inverted) {
  static const uint8_t kSteps = 6;
  for (uint8_t i = 0; i <= kSteps; i++) {
    int value = from + ((int)to - (int)from) * i / kSteps;
    gfxSetBrightness((uint8_t)constrain(value, 0, 100), inverted);
    if (i < kSteps) delay(10);
  }
}

static PhotoItem s_emptyPhoto = {"", 0};
static File s_gifFile;
static int16_t s_gifX = 0;
static int16_t s_gifY = 0;

static void* gifOpenFile(const char* name, int32_t* size) {
  s_gifFile = LittleFS.open(name, "r");
  if (!s_gifFile) return nullptr;
  *size = (int32_t)s_gifFile.size();
  return &s_gifFile;
}

static void gifCloseFile(void* handle) {
  File* file = static_cast<File*>(handle);
  if (file) file->close();
}

static int32_t gifReadFile(GIFFILE* gifFile, uint8_t* buffer, int32_t length) {
  File* file = static_cast<File*>(gifFile->fHandle);
  int32_t remaining = gifFile->iSize - gifFile->iPos;
  if (remaining < length) length = remaining;
  if (!file || length <= 0) return 0;
  int32_t count = (int32_t)file->read(buffer, length);
  gifFile->iPos = (int32_t)file->position();
  return count;
}

static int32_t gifSeekFile(GIFFILE* gifFile, int32_t position) {
  File* file = static_cast<File*>(gifFile->fHandle);
  if (!file || !file->seek(position)) return gifFile->iPos;
  gifFile->iPos = (int32_t)file->position();
  return gifFile->iPos;
}

// AnimatedGIF supplies one indexed scanline at a time. Opaque runs are sent
// directly to the ST7789; transparent runs leave the previous frame intact.
static void gifDrawLine(GIFDRAW* draw) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  int width = draw->iWidth;
  if (draw->iX + width > 240) width = 240 - draw->iX;
  int y = s_gifY + draw->iY + draw->y;
  if (y < 0 || y >= 240 || s_gifX + draw->iX >= 240 || width <= 0) return;

  uint8_t* pixels = draw->pPixels;
  uint16_t* palette = draw->pPalette;
  if (draw->ucDisposalMethod == 2) {
    for (int x = 0; x < width; x++)
      if (pixels[x] == draw->ucTransparent) pixels[x] = draw->ucBackground;
    draw->ucHasTransparency = 0;
  }

  static uint16_t row[240];
  if (!draw->ucHasTransparency) {
    for (int x = 0; x < width; x++) row[x] = palette[pixels[x]];
    gfxDraw16bitRGBBitmapBatch(s_gifX + draw->iX, y, row, width, 1);
    return;
  }

  int x = 0;
  while (x < width) {
    while (x < width && pixels[x] == draw->ucTransparent) x++;
    int start = x;
    while (x < width && pixels[x] != draw->ucTransparent) {
      row[x - start] = palette[pixels[x]];
      x++;
    }
    if (x > start) gfxDraw16bitRGBBitmapBatch(s_gifX + draw->iX + start, y, row, x - start, 1);
  }
}

// TJpgDec callback: push decoded JPEG pixels directly to the ST7789 display
static bool jpgDrawCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (!gfxDev()) return false;
  gfxDraw16bitBeRGBBitmapBatch(x, y, bitmap, w, h);
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
    if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg") || lowerName.endsWith(".gif") || lowerName.endsWith(".raw")) {
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
  m_currentIdx = -1;
  m_nextRotateMs = millis() + (uint32_t)s.gallery.rotateSec * 1000UL;
}

void GalleryMode::invalidate(const Settings& s) {
  closeGif();
  scanPhotos();
  if (m_photoCount == 0) {
    m_currentIdx = -1;
  }
  if (s.mode == MODE_GALLERY) {
    renderCurrent(s);
  }
}

void GalleryMode::wake(const Settings& s) {
  closeGif();
  scanPhotos();
  if (m_photoCount > 0) {
    nextPhoto(s);
  }
  m_nextRotateMs = millis() + (uint32_t)s.gallery.rotateSec * 1000UL;
  renderCurrent(s);
}

void GalleryMode::nextPhoto(const Settings& s) {
  closeGif();
  if (m_photoCount == 0) {
    m_currentIdx = -1;
    return;
  }

  if (s.gallery.randomOrder && m_photoCount > 1) {
    m_currentIdx = (rand() % m_photoCount);
  } else {
    m_currentIdx = (m_currentIdx + 1) % m_photoCount;
  }
}

void GalleryMode::service(const Settings& s) {
  if (s.mode != MODE_GALLERY && s.mode != MODE_CAROUSEL) return;

  serviceGif();
  if (m_photoCount > 0 && (int32_t)(millis() - m_nextRotateMs) >= 0) {
    m_nextRotateMs = millis() + (uint32_t)s.gallery.rotateSec * 1000UL;
    nextPhoto(s);
    renderCurrent(s);
  }
}

void GalleryMode::closeGif() {
  if (m_gif) {
    if (m_gifActive) m_gif->close();
    delete m_gif;
    m_gif = nullptr;
  }
  m_gifActive = false;
  m_nextGifFrameMs = 0;
}

bool GalleryMode::openGif(const String& path) {
  closeGif();
  gfxClear();
  m_gif = new (std::nothrow) AnimatedGIF();
  if (!m_gif) return false;
  m_gif->begin(LITTLE_ENDIAN_PIXELS);
  if (!m_gif->open(path.c_str(), gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDrawLine)) {
    delete m_gif;
    m_gif = nullptr;
    return false;
  }
  s_gifX = max(0, (240 - m_gif->getCanvasWidth()) / 2);
  s_gifY = max(0, (240 - m_gif->getCanvasHeight()) / 2);
  m_gifActive = true;
  m_nextGifFrameMs = millis();
  serviceGif();
  return true;
}

void GalleryMode::serviceGif() {
  if (!m_gifActive || (int32_t)(millis() - m_nextGifFrameMs) < 0) return;
  int32_t delayMs = 0;
  bool frameOk = false;
  if (m_gif) {
    gfxBeginBitmapBatch();
    frameOk = m_gif->playFrame(false, &delayMs);
    gfxEndBitmapBatch();
  }
  if (!frameOk) {
    // Loop the same file until galleryRotateSec advances to the next item.
    String path = m_photos[m_currentIdx].name;
    closeGif();
    m_gif = new (std::nothrow) AnimatedGIF();
    if (!m_gif) return;
    m_gif->begin(LITTLE_ENDIAN_PIXELS);
    if (!m_gif->open(path.c_str(), gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDrawLine)) {
      delete m_gif;
      m_gif = nullptr;
      return;
    }
    m_gifActive = true;
    delayMs = 20;
  }
  if (delayMs < 20) delayMs = 20;
  m_nextGifFrameMs = millis() + (uint32_t)delayMs;
}

void GalleryMode::renderCurrent(const Settings& s) {
  uint8_t restoreBrightness = gfxBrightness();
  bool fade = restoreBrightness > 0;
  if (fade) {
    galleryFade(restoreBrightness, 0, s.backlightInverted);
    delay(12);
  }

  renderCurrentNow(s);

  if (fade) galleryFade(0, restoreBrightness, s.backlightInverted);
}

void GalleryMode::renderCurrentNow(const Settings& s) {
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
    closeGif();
    uint16_t width = 0, height = 0;
    JRESULT sizeResult = TJpgDec.getFsJpgSize(&width, &height, item.name, LittleFS);
    // Uploaded photos are normalized to 240x240, so clearing first only doubles
    // display traffic. Preserve the previous frame until decode starts. Legacy
    // smaller images still receive a clean black background.
    if (sizeResult != JDR_OK || width < 240 || height < 240) gfxClear();
    gfxBeginBitmapBatch();
    JRESULT result = TJpgDec.drawFsJpg((int32_t)0, (int32_t)0, item.name, LittleFS);
    gfxEndBitmapBatch();
    if (result != JDR_OK) {
      gfxClear();
      gfxDrawCentered("JPEG ERROR", 96, 3, C_RED);
      gfxDrawCentered("Check file", 132, 1, C_GRAY);
    }
    return;
  }

  if (lowerName.endsWith(".gif")) {
    if (openGif(item.name)) return;
    gfxClear();
    gfxDrawCentered("GIF ERROR", 96, 3, C_RED);
    gfxDrawCentered("Check size / format", 132, 1, C_GRAY);
    return;
  }

  // RAW RGB565 fallback (115 KB)
  if (item.size == 115200) {
    closeGif();
    File f = LittleFS.open(item.name, "r");
    if (!f) {
      gfxClear();
      gfxDrawCentered("ERROR", 100, 3, C_RED);
      return;
    }
    // One aligned 480-byte row buffer is enough; draw16bitBeRGBBitmap writes
    // its bytes unchanged, matching the legacy RAW big-endian format.
    uint16_t rowPixels[240];
    uint8_t* rowBytes = reinterpret_cast<uint8_t*>(rowPixels);
    bool complete = gfxDev() != nullptr;
    gfxBeginBitmapBatch();
    for (int y = 0; y < 240; y++) {
      if (f.read(rowBytes, 480) == 480) {
        gfxDraw16bitBeRGBBitmapBatch(0, y, rowPixels, 240, 1);
      } else { complete = false; break; }
    }
    gfxEndBitmapBatch();
    f.close();
    if (!complete) {
      gfxClear();
      gfxDrawCentered("RAW ERROR", 96, 3, C_RED);
    }
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
