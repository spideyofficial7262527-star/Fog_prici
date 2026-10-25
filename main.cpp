#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <AnimatedGIF.h>

// User's ESP32 DevKit V1 + 2.8" ILI9341 TFT wiring.
// CS=5, DC=2, RST=4, SCK=18, MOSI=23, MISO=19
static constexpr int TFT_CS   = 5;
static constexpr int TFT_DC   = 2;
static constexpr int TFT_RST  = 4;
static constexpr int TFT_SCLK = 18;
static constexpr int TFT_MOSI = 23;
static constexpr int TFT_MISO = 19;

static constexpr char AP_SSID[] = "Mochi-GIF";
static constexpr char AP_PASS[] = "mochigif123";
static constexpr char GIF_PATH[] = "/anim.gif";
static constexpr char TEMP_PATH[] = "/anim.tmp";

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);
AnimatedGIF gif;

File gifFile;
File uploadFile;
volatile bool uploading = false;
int xOffset = 0;
int yOffset = 0;
uint16_t lineBuf[320];

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mochi GIF</title>
<style>
body{font:16px system-ui;background:#10131b;color:#eef;margin:0;padding:22px}
main{max-width:520px;margin:auto;background:#1c2230;padding:22px;border-radius:18px}
h1{margin-top:0}p{color:#c6cede}input,button{width:100%;box-sizing:border-box;margin:10px 0;padding:14px;border:0;border-radius:10px}
button{background:#63e6be;color:#10131b;font-weight:800}#status{white-space:pre-wrap}
.small{font-size:13px;color:#aab4c8}
</style></head><body><main><h1>🍡 Mochi GIF Player</h1>
<p>Choose an animated GIF. The ESP32 stores it in flash and plays it on your TFT.</p>
<form id="f"><input id="gif" type="file" accept=".gif,image/gif" required>
<button>Upload & Play</button></form><p id="status">Ready.</p>
<p class="small">For best results use a small GIF (recommended 320×240 or smaller, under about 1 MB). GIF frames are decoded on the ESP32; no frame-header generation needed.</p>
<script>
document.getElementById('f').addEventListener('submit',async e=>{
 e.preventDefault();let file=document.getElementById('gif').files[0];if(!file)return;
 let s=document.getElementById('status');s.textContent='Uploading '+file.name+'...';
 let d=new FormData();d.append('gif',file,file.name);
 try{let r=await fetch('/upload',{method:'POST',body:d});s.textContent=await r.text();}
 catch(err){s.textContent='Upload failed: '+err;}
});
</script></main></body></html>
)HTML";

void *GIFOpenFile(const char *filename, int32_t *size) {
  (void)filename;
  if (gifFile) gifFile.close();
  gifFile = LittleFS.open(GIF_PATH, "r");
  if (!gifFile) return nullptr;
  *size = (int32_t)gifFile.size();
  return (void *)&gifFile;
}

void GIFCloseFile(void *handle) {
  File *f = static_cast<File *>(handle);
  if (f && *f) f->close();
}

int32_t GIFReadFile(GIFFILE *file, uint8_t *buffer, int32_t length) {
  File *f = static_cast<File *>(file->fHandle);
  if (!f || !*f || length <= 0) return 0;
  int32_t remaining = file->iSize - file->iPos;
  if (remaining <= 0) return 0;
  if (length > remaining) length = remaining;
  int32_t got = (int32_t)f->read(buffer, (size_t)length);
  file->iPos = (int32_t)f->position();
  return got;
}

int32_t GIFSeekFile(GIFFILE *file, int32_t position) {
  File *f = static_cast<File *>(file->fHandle);
  if (!f || !*f) return 0;
  if (position < 0) position = 0;
  f->seek((uint32_t)position);
  file->iPos = (int32_t)f->position();
  return file->iPos;
}

void GIFDraw(GIFDRAW *draw) {
  if (!draw || !draw->pPixels || !draw->pPalette) return;

  int y = draw->iY + draw->y + yOffset;
  if (y < 0 || y >= tft.height()) return;

  int sourceX = draw->iX;
  int destX = sourceX + xOffset;
  int width = draw->iWidth;
  if (width <= 0) return;

  uint8_t *pixels = draw->pPixels;
  uint16_t *palette = draw->pPalette;

  // Clip horizontal bounds while preserving GIF source pixel indexing.
  int start = 0;
  if (destX < 0) start = -destX;
  int end = width;
  if (destX + end > tft.width()) end = tft.width() - destX;
  if (start >= end) return;

  for (int i = start; i < end; ++i) {
    uint8_t index = pixels[i];
    if (draw->ucHasTransparency && index == draw->ucTransparent) {
      lineBuf[i - start] = 0xFFFF; // placeholder; transparent pixels are handled below
    } else {
      lineBuf[i - start] = palette[index];
    }
  }

  // Draw opaque runs only, preserving pixels from previous frames beneath transparent pixels.
  int i = start;
  while (i < end) {
    while (i < end && draw->ucHasTransparency && pixels[i] == draw->ucTransparent) ++i;
    int runStart = i;
    while (i < end && !(draw->ucHasTransparency && pixels[i] == draw->ucTransparent)) ++i;
    int runLength = i - runStart;
    if (runLength > 0) {
      for (int j = 0; j < runLength; ++j) {
        lineBuf[j] = palette[pixels[runStart + j]];
      }
      tft.drawRGBBitmap(destX + runStart, y, lineBuf, runLength, 1);
    }
  }
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleUploadStream() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploading = true;
    if (uploadFile) uploadFile.close();
    LittleFS.remove(TEMP_PATH);
    uploadFile = LittleFS.open(TEMP_PATH, "w");
    Serial.printf("Upload started: %s\n", upload.filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        Serial.println("WARNING: partial write to LittleFS");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    size_t size = LittleFS.exists(TEMP_PATH) ? LittleFS.open(TEMP_PATH, "r").size() : 0;
    if (size > 0) {
      LittleFS.remove(GIF_PATH);
      if (LittleFS.rename(TEMP_PATH, GIF_PATH)) {
        Serial.printf("GIF saved: %u bytes\n", (unsigned)size);
      } else {
        Serial.println("ERROR: could not rename uploaded GIF");
      }
    }
    uploading = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    LittleFS.remove(TEMP_PATH);
    uploading = false;
    Serial.println("Upload aborted");
  }
}

void handleUploadComplete() {
  if (LittleFS.exists(GIF_PATH)) {
    server.send(200, "text/plain", "Upload saved! The new GIF will play on the TFT shortly.");
  } else {
    server.send(500, "text/plain", "Upload failed. Check GIF size and available flash storage.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1); // landscape: 320 x 240
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 25);
  tft.println("Mochi GIF");
  tft.setTextSize(1);
  tft.setCursor(10, 58);
  tft.println("Starting Wi-Fi...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    tft.fillScreen(ILI9341_RED);
    tft.setCursor(10, 20);
    tft.println("LittleFS failed");
    while (true) delay(1000);
  }

  gif.begin(GIF_PALETTE_RGB565_LE);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Wi-Fi AP: "); Serial.println(AP_SSID);
  Serial.print("Open: http://"); Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleUploadComplete, handleUploadStream);
  server.begin();

  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(10, 15);
  tft.setTextSize(2);
  tft.println("Mochi GIF ready");
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.println("Wi-Fi: Mochi-GIF");
  tft.setCursor(10, 68);
  tft.println("Pass: mochigif123");
  tft.setCursor(10, 86);
  tft.println("Open 192.168.4.1");
}

void playGifOnce() {
  if (!LittleFS.exists(GIF_PATH) || uploading) return;

  if (!gif.open((char *)GIF_PATH, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    Serial.printf("GIF open failed, error %d\n", gif.getLastError());
    delay(500);
    return;
  }

  int canvasW = gif.getCanvasWidth();
  int canvasH = gif.getCanvasHeight();
  xOffset = (tft.width() - canvasW) / 2;
  yOffset = (tft.height() - canvasH) / 2;

  // Keep oversized animations centered; draw callback clips to the display.
  Serial.printf("Playing GIF %dx%d\n", canvasW, canvasH);

  int frameDelay = 40;
  while (!uploading && gif.playFrame(false, &frameDelay, nullptr)) {
    server.handleClient();
    if (frameDelay < 10) frameDelay = 10;
    if (frameDelay > 1000) frameDelay = 1000;
    delay(frameDelay);
    yield();
  }

  gif.close();
}

void loop() {
  server.handleClient();
  if (!uploading) {
    if (LittleFS.exists(GIF_PATH)) {
      playGifOnce(); // loops the GIF continuously by reopening it
    } else {
      delay(5);
    }
  } else {
    delay(1);
  }
}
