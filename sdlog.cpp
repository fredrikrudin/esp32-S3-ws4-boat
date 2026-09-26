/* Logging to the Serial Monitor, to a ring buffer in PSRAM (readable at /log in
   the browser) and, if switched on, to the TF card.
   The card is mounted in setup() (when the log or the CSV log is switched on) or
   with the Mount button in Settings; logf() itself never mounts or unmounts.
   The card uses SDMMC in 1-bit mode: GPIO2 clock, GPIO1 command, GPIO4 data,
   so no chip-select pin is needed. Those pins are shared with the display's
   configuration SPI, which is only used while the panel starts up. */
#include "app.h"
#include <SD_MMC.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_log.h>
#include "WS_CH32_IO.h"

#define SD_CLK 2  // also LCD_SCL: the panel only uses it while starting up
#define SD_CMD 1  // also LCD_SDA
#define SD_D0 4
/* The V4 board wires the card for SPI (SCK 2, MOSI 1, MISO 4); chip select is
   handled on the board. The SD library is handed a pin it can toggle
   harmlessly (GPIO44, RS485 TX, unused here). */
#define SD_SPI_CS 44
#define LOG_RING 12288  // bytes kept for the web view (smaller = less PSRAM traffic)
#define SD_FLUSH_MS 5000  // how often the file is flushed

static SemaphoreHandle_t log_mtx;
static bool sd_spi = false;  // true = mounted over SPI, false = SD (SDMMC) mode
static SemaphoreHandle_t mount_mtx;  // one mount at a time (setup and Settings buttons)
static char *ring = nullptr;  // in PSRAM
static size_t ring_len = 0;   // bytes used (grows to LOG_RING, then wraps)
static size_t ring_pos = 0;
static bool ring_wrapped = false;

static bool sd_mounted = false;
static char sd_msg[64] = "Not started";
static File log_file;
static char log_name[32] = "/boat.log";
static uint32_t last_flush = 0;
static uint32_t dropped = 0;

static void ring_write(const char *s, size_t n) {
  if (!ring) return;
  for (size_t i = 0; i < n; i++) {
    ring[ring_pos++] = s[i];
    if (ring_pos >= LOG_RING) {
      ring_pos = 0;
      ring_wrapped = true;
    }
  }
  ring_len = ring_wrapped ? LOG_RING : ring_pos;
}

/* Mounting happens in setup() and from the Settings buttons, never inside logf():
   it can take several seconds, and logf() holds the log lock.

   Waveshare's own SD demo sets the three pins and mounts in 1-bit mode, but it
   also waits 3 s after starting the CH32 chip, which powers parts of the board.
   Mounting too early is the most likely reason a card is not found, so the same
   wait is honoured here, and slower bus speeds are tried before giving up.
   The slow part runs without the log lock, so logging carries on meanwhile. */
/* GPIO2 and GPIO1 are also the display's setup bus (SCK and MOSI). After the
   panel has started they stay configured as plain outputs, which stops the
   card answering, so they are released and given pull-ups first. */
static void release_pins() {
  gpio_reset_pin((gpio_num_t)SD_CLK);
  gpio_reset_pin((gpio_num_t)SD_CMD);
  gpio_reset_pin((gpio_num_t)SD_D0);
  pinMode(SD_CMD, INPUT_PULLUP);
  pinMode(SD_D0, INPUT_PULLUP);
  delay(5);
}

/* SPI attempt, expander untouched: this is what works on the V4 board */
static bool try_mount_spi_raw(uint32_t freq) {
  release_pins();
  SPI.end();
  SPI.begin(SD_CLK, SD_D0, SD_CMD, -1);  // SCK, MISO, MOSI
  if (SD.begin(SD_SPI_CS, SPI, freq) && SD.cardType() != CARD_NONE) {
    sd_spi = true;
    return true;
  }
  SD.end();
  sd_spi = false;
  return false;
}

/* SPI attempt with one expander bit pulled low (for other board revisions) */
static bool try_mount_spi(uint8_t cs_bits, uint32_t freq) {
  WS_CH32_IO::writeRegister(Wire, WS_CH32_IO::REG_OUTPUT, WS_CH32_IO::OUT_DISPLAY_ON & ~cs_bits);
  delay(30);
  return try_mount_spi_raw(freq);
}

/* SD (SDMMC) mode attempt; 0 = library default speed */
static bool try_mount_sdmmc(int freq) {
  sd_spi = false;
  release_pins();
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  bool ok = freq ? SD_MMC.begin("/sdcard", true, false, freq) : SD_MMC.begin("/sdcard", true);
  if (ok && SD_MMC.cardType() != CARD_NONE) return true;
  SD_MMC.end();
  return false;
}

/* Tries every combination and logs what works: both bus modes, both direction
   register values, every expander bit. Reachable from Settings -> SD card. */
bool sd_log_probe() {
  uint8_t dir = 0, out = 0;
  bool have_dir = WS_CH32_IO::readRegister(Wire, WS_CH32_IO::REG_DIRECTION, &dir);
  bool have_out = WS_CH32_IO::readRegister(Wire, WS_CH32_IO::REG_OUTPUT, &out);
  logf("SD probe: CH32 direction=0x%02X (%s), output=0x%02X (%s)", dir, have_dir ? "read" : "read failed",
       out, have_out ? "read" : "read failed");

  const uint8_t bits[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
  const uint8_t dirs[] = { 0xFF, 0x00 };
  bool found = false;

  esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
  esp_log_level_set("sdmmc_sd", ESP_LOG_NONE);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);
  logf("SD probe: starting (SPI first, then SD mode)");

  if (try_mount_spi_raw(400000)) {
    logf("SD probe: >>> SPI works with the expander untouched <<<");
    found = true;
  }
  for (uint8_t d : dirs) {
    if (found) break;
    WS_CH32_IO::writeRegister(Wire, WS_CH32_IO::REG_DIRECTION, d);
    delay(20);
    for (uint8_t b : bits) {
      if (try_mount_spi(b, 400000)) {
        logf("SD probe: >>> SPI works: direction 0x%02X, bit 0x%02X low <<<", d, b);
        found = true;
        break;
      }
      WS_CH32_IO::writeRegister(Wire, WS_CH32_IO::REG_OUTPUT, WS_CH32_IO::OUT_DISPLAY_ON | b);
      delay(30);
      if (try_mount_sdmmc(SDMMC_FREQ_PROBING)) {
        logf("SD probe: >>> SD mode works: direction 0x%02X, bit 0x%02X high <<<", d, b);
        found = true;
        break;
      }
      logf("SD probe: direction 0x%02X, bit 0x%02X -> no card either way", d, b);
    }
  }
  logf("SD probe: finished - %s", found ? "card found" : "no card in any combination");
  esp_log_level_set("sdmmc_common", ESP_LOG_ERROR);
  esp_log_level_set("sdmmc_sd", ESP_LOG_ERROR);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_ERROR);

  if (!found) {  // put the expander back as it was
    if (have_dir) WS_CH32_IO::writeRegister(Wire, WS_CH32_IO::REG_DIRECTION, dir);
    WS_CH32_IO::writeRegister(Wire, WS_CH32_IO::REG_OUTPUT, have_out && out ? out : WS_CH32_IO::OUT_DISPLAY_ON);
    return false;
  }
  return sd_log_mount();  // open the log file on whatever mounted
}

bool sd_log_mount() {
  if (sd_mounted) return true;
  if (!mount_mtx) return false;              // log_begin() not called yet
  xSemaphoreTake(mount_mtx, portMAX_DELAY);
  if (sd_mounted) {                          // mounted by someone else while we waited
    xSemaphoreGive(mount_mtx);
    return true;
  }

  if (millis() < 3200) {  // as in Waveshare's example: let the board settle
    logf("SD: waiting for the board to settle before mounting");
    delay(3200 - millis());
  }

  /* SPI first: that is how the V4 board is wired. SD mode stays as a fallback. */
  bool ok = false;
  for (uint32_t f : { 20000000u, 4000000u, 400000u }) {
    if (try_mount_spi_raw(f)) {
      logf("SD: mounted over SPI at %lu kHz", (unsigned long)(f / 1000));
      ok = true;
      break;
    }
  }
  if (!ok) {
    for (int f : { 0, SDMMC_FREQ_PROBING }) {
      if (try_mount_sdmmc(f)) {
        logf("SD: mounted in SD mode");
        ok = true;
        break;
      }
    }
  }

  if (!ok) {
    strlcpy(sd_msg, "No card found", sizeof(sd_msg));
    logf("SD: no card found - use Probe card in Settings");
    xSemaphoreGive(mount_mtx);
    return false;
  }

  uint64_t mb = (sd_spi ? SD.cardSize() : SD_MMC.cardSize()) / (1024 * 1024);
  File f = sd_fs().open(log_name, FILE_APPEND);
  if (!f) {
    if (sd_spi) SD.end();
    else SD_MMC.end();
    strlcpy(sd_msg, "Card found, but cannot write", sizeof(sd_msg));
    logf("SD: card found (%llu MB) but the file could not be opened - FAT32?", (unsigned long long)mb);
    xSemaphoreGive(mount_mtx);
    return false;
  }
  static bool header_written = false;  // once per boot, not once per mount
  if (!header_written) {
    f.printf("\n--- board started, %lu ms ---\n", (unsigned long)millis());
    header_written = true;
  }

  /* publish under the log lock: from here on logf() writes to the file */
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  log_file = f;
  last_flush = millis();
  sd_mounted = true;
  snprintf(sd_msg, sizeof(sd_msg), "Logging to %s (card %llu MB, %s)", log_name, (unsigned long long)mb,
           sd_spi ? "SPI" : "SD mode");
  xSemaphoreGive(log_mtx);

  logf("SD: mounted, card %llu MB (%s)", (unsigned long long)mb, sd_spi ? "SPI" : "SD mode");
  xSemaphoreGive(mount_mtx);
  return true;
}

fs::FS &sd_fs() {
  return sd_spi ? (fs::FS &)SD : (fs::FS &)SD_MMC;
}

void sd_log_unmount() {
  if (!sd_mounted || !log_mtx) return;
  xSemaphoreTake(log_mtx, portMAX_DELAY);  // not while logf() is writing
  if (sd_mounted) {
    log_file.close();
    SD_MMC.end();
    sd_mounted = false;
    strlcpy(sd_msg, "Card not in use", sizeof(sd_msg));
  }
  xSemaphoreGive(log_mtx);
}

bool sd_log_ok() {
  return sd_mounted;
}

const char *sd_log_status() {
  return sd_msg;
}

size_t sd_log_size() {
  return sd_mounted ? log_file.size() : 0;
}

void log_begin() {
  log_mtx = xSemaphoreCreateMutex();
  mount_mtx = xSemaphoreCreateMutex();
  ring = (char *)heap_caps_malloc(LOG_RING, MALLOC_CAP_SPIRAM);
  if (!ring) USBSerial.println("Log ring buffer allocation failed");
}

void logf(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
  if (n && buf[n - 1] != '\n') {  // every entry is one line
    buf[n++] = '\n';
    buf[n] = 0;
  }

  USBSerial.print(buf);
  if (!log_mtx) return;

  if (xSemaphoreTake(log_mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
    dropped++;
    return;
  }
  ring_write(buf, n);
  /* Only writes: mounting is done by setup() and the Settings buttons, and the card
     stays mounted when this switch is off, because the CSV log may still use it. */
  if (feat_sdlog && sd_mounted) {
    log_file.print(buf);
    if (millis() - last_flush > SD_FLUSH_MS) {
      log_file.flush();
      last_flush = millis();
    }
  }
  xSemaphoreGive(log_mtx);
}

/* Copies the ring buffer, oldest first, into the caller's String */
void log_dump(String &out) {
  if (!ring || !log_mtx) return;
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  out.reserve(ring_len + 64);
  if (ring_wrapped) out.concat(ring + ring_pos, LOG_RING - ring_pos);
  out.concat(ring, ring_pos);
  xSemaphoreGive(log_mtx);
}

/* ---------- card management (all called from the UI) ---------- */
const char *sd_log_name() {
  return log_name;
}

/* Card type, size and how much is used */
void sd_card_info(char *out, size_t n) {
  if (!sd_mounted) {
    snprintf(out, n, "No card mounted");
    return;
  }
  const char *type = "unknown";
  switch (sd_spi ? SD.cardType() : SD_MMC.cardType()) {
    case CARD_MMC: type = "MMC"; break;
    case CARD_SD: type = "SDSC"; break;
    case CARD_SDHC: type = "SDHC"; break;
    default: break;
  }
  uint64_t total = (sd_spi ? SD.totalBytes() : SD_MMC.totalBytes()) / (1024 * 1024);
  uint64_t used = (sd_spi ? SD.usedBytes() : SD_MMC.usedBytes()) / (1024 * 1024);
  snprintf(out, n, "%s card, %llu MB used of %llu MB", type, (unsigned long long)used, (unsigned long long)total);
}

/* Starts a new file: boat.log, boat-1.log, boat-2.log ... */
bool sd_log_new_file() {
  if (!sd_mounted) return false;
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  log_file.close();
  for (int i = 1; i < 100; i++) {
    char name[32];
    snprintf(name, sizeof(name), "/boat-%d.log", i);
    if (!sd_fs().exists(name)) {
      strlcpy(log_name, name, sizeof(log_name));
      break;
    }
  }
  log_file = sd_fs().open(log_name, FILE_WRITE);
  bool ok = (bool)log_file;
  snprintf(sd_msg, sizeof(sd_msg), ok ? "Logging to %s" : "Could not create %s", log_name);
  xSemaphoreGive(log_mtx);
  return ok;
}

/* Deletes every log file except the one being written */
int sd_log_delete_old() {
  if (!sd_mounted) return 0;
  int n = 0;
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  File root = sd_fs().open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = String("/") + f.name();
    bool is_log = name.endsWith(".log");
    f.close();
    if (is_log && name != String(log_name)) {
      if (sd_fs().remove(name.c_str())) n++;
    }
  }
  root.close();
  xSemaphoreGive(log_mtx);
  return n;
}

/* Files on the card, one per line, for the web page */
void sd_list_files(String &out) {
  if (!sd_mounted) {
    out = "No card mounted";
    return;
  }
  File root = sd_fs().open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    out += f.name();
    out += '\t';
    out += String((uint32_t)f.size());
    out += '\n';
    f.close();
  }
  root.close();
}
