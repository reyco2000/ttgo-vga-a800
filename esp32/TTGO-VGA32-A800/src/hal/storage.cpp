/* microSD on its own HSPI bus, mounted at /sd so the core's fopen() calls
 * reach it through the ESP-IDF VFS. */
#include <SD.h>
#include <SPI.h>
#include "hal.h"
#include "../../config.h"

static SPIClass sd_spi(HSPI);

bool Storage_Init(void) {
  sd_spi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  return SD.begin(PIN_SD_CS, sd_spi, SD_FREQ_HZ, "/sd") ||
         SD.begin(PIN_SD_CS, sd_spi, 1000000, "/sd");
}

/* Builds MEDIA_DIR "/<first line of AUTORUN_FILE>"; false when there is
 * nothing to run. */
bool Storage_ReadAutorun(char *path, int size) {
  FILE *f = fopen(AUTORUN_FILE, "r");
  if (!f)
    return false;
  char name[96] = {0};
  bool ok = fgets(name, sizeof(name), f) != NULL;
  fclose(f);
  name[strcspn(name, "\r\n")] = 0;
  if (!ok || !name[0])
    return false;
  snprintf(path, (size_t)size, "%s/%s", MEDIA_DIR, name);
  return true;
}
