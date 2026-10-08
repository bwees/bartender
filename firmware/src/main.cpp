#include "pins_config.h"
#include "LovyanGFX_Driver.h"
#include "gt911.h"
#include "i2c_bus.h"
#include "led_ring.h"

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <SPI.h>

#include <stdbool.h>

#include <esp_mac.h>

#include <string>

#include "domain.h"
#include "portal.h"
#include "settings.h"
#include "portal_server.h"
#include "pour_hw.h"
#include "sd_fs.h"
#include "state_lock.h"
#include "tof.h"
#include "storage.h"
#include "ui/ui.h"

LGFX gfx;

namespace {

constexpr int POUR_TASK_PRIORITY = 6;
constexpr int POUR_TASK_CORE = 1;
constexpr int POUR_TICK_MS = 10;

SdFs sdFs;
Pca9685 pumps;
Tof tof;
MillisClock pourClock;
bartender::Library library;
bartender::Config config;
pour::Scheduler scheduler(pourClock, pumps);

std::string apSuffix() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return suffix;
}

// The portal is the only way to configure the machine, so it has to come up even
// when the card has nothing on it - otherwise a blank card is unrecoverable
// without pulling it. Defaults are written back only when a file is genuinely
// absent: overwriting a file that failed to parse would destroy the copy the
// storage module's .bak recovery still might rescue.
void loadState() {
  if (!sdFs.mount()) {
    Serial.println("SD mount failed - running on defaults, nothing will persist");
    config = bartender::defaultConfig(apSuffix());
    library = bartender::defaultLibrary();
    return;
  }

  const auto libStatus = bartender::loadLibrary(sdFs, portal::kRecipesPath, library);
  const auto cfgStatus = bartender::loadConfig(sdFs, portal::kConfigPath, config);
  Serial.printf("recipes: %d, config: %d\n", (int)libStatus, (int)cfgStatus);

  if (cfgStatus == bartender::LoadStatus::missing) {
    config = bartender::defaultConfig(apSuffix());
    Serial.printf("no config on card - writing default, AP is %s\n",
                  config.wifi.ssid.c_str());
    if (!bartender::saveConfig(sdFs, portal::kConfigPath, config))
      Serial.println("failed to write default config");
  } else if (cfgStatus != bartender::LoadStatus::ok) {
    config = bartender::defaultConfig(apSuffix());
    Serial.println("config unreadable - using defaults, card left untouched");
  }

  if (libStatus == bartender::LoadStatus::missing) {
    library = bartender::defaultLibrary();
    Serial.println("no recipes on card - writing default library");
    if (!bartender::saveLibrary(sdFs, portal::kRecipesPath, library))
      Serial.println("failed to write default library");
  } else if (libStatus != bartender::LoadStatus::ok) {
    Serial.println("recipes unreadable - starting empty, card left untouched");
  }

  Serial.printf("drinks: %u, pumps: %d, ap: %s\n", (unsigned)library.recipes.size(),
                config.pumpCount, config.wifi.ssid.c_str());
}

// The scheduler closes pump channels at their deadline and runs the independent
// watchdog, so it must not share a task with the UI: a stalled render would hold
// pumps open. It ticks above LVGL on its own task, and the shared I2C bus is
// serialised by I2cGuard rather than by everything living on one thread.
void pourTask(void *) {
  for (;;) {
    // Copy the config out under the lock rather than holding it across I2C.
    bartender::CupSensorConfig cupSensor;
    {
      StateGuard guard;
      cupSensor = config.cupSensor;
    }
    tof.poll(millis(), cupSensor);
    scheduler.tick();
    vTaskDelay(pdMS_TO_TICKS(POUR_TICK_MS));
  }
}

}  // namespace

/* Change to your screen resolution */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;


//  Display refresh
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  if (gfx.getStartCount() > 0) {
    gfx.endWrite();
  }
  gfx.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (lgfx::rgb565_t *)&color_p->full);

  lv_disp_flush_ready(disp);  //	Tell lvgl that the refresh is complete
}

//  Read touch
void my_touchpad_read( lv_indev_drv_t * indev_driver, lv_indev_data_t * data )
{
  int32_t x = 0, y = 0;
  data->state = gt911::read(x, y) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (data->state == LV_INDEV_STATE_PR) {
    data->point.x = x;
    data->point.y = y;
  }
}

bool i2cScanForAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

// Wrapper function for sending I2C commands
void sendI2CCommand(uint8_t command) {
  uint8_t error;
  // Start sending commands to the specified address
  Wire.beginTransmission(0x30);
  // Send command
  Wire.write(command);
  //  End transmission and return status
  error = Wire.endTransmission();

  if (error == 0) {
    Serial.print("command 0x");
    Serial.print(command, HEX);
    Serial.println(" Sent successfully");
  } else {
    Serial.print("Command sent error, error code:");
    Serial.println(error);
  }
}

void scanI2c() {
  Serial.print("i2c:");
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    I2cGuard guard;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", addr);
  }
  Serial.println();
}

void setup()
{
  Serial.begin(115200); 

  pinMode(19, OUTPUT);

  Wire.begin(15, 16);
  delay(50);

  // An MCU reset does not reset the PCA9685, so a channel left full-on keeps a
  // pump running across a reboot. Killing the outputs is the first thing that
  // happens on the bus, ahead of the panel handshake and everything else.
  const bool pumpsReady = pumps.begin();
  Serial.printf("pumps(0x40): %s\n", pumpsReady ? "ok" : "NO");

  while (1) {
    if (i2cScanForAddress(0x30) && i2cScanForAddress(0x5D)) {
      Serial.print("The microcontroller is detected: address 0x");
      Serial.println(0x30, HEX);
      Serial.print("The microcontroller is detected: address 0x");
      Serial.println(0x5D, HEX);
      break;
    } else {
      Serial.print("No microcontroller was detected: address 0x");
      Serial.println(0x30, HEX);
      Serial.print("No microcontroller was detected: address 0x");
      Serial.println(0x5D, HEX);
      //Prevent the microcontroller did not start to adjust the bright screen
      sendI2CCommand(0x19);    // 0x19 : Activate touch screen
      pinMode(1, OUTPUT);
      digitalWrite(1, LOW);
      
      delay(120);
      pinMode(1, INPUT);

      delay(100);
    }
  }
  // Only now: the GT911 does not answer until the STC8's 0x19 has activated it and
  // the reset pulse above has run, so probing it any earlier always fails.
  const bool touchReady = gt911::begin();
  Serial.printf("touch(0x5D): %s\n", touchReady ? "ok" : "NO");

  // Same reason as the touch probe: the VL53L0X init writes registers and runs two
  // reference calibrations, and doing that on the bus before the STC8 handshake has
  // settled leaves a chip that answers its ID but never finishes a ranging cycle.
  const bool cupSensorReady = tof.begin();
  Serial.printf("cup sensor(0x29): %s\n", cupSensorReady ? "ok" : "NO");

  // Start sending command 0x10 to address 0x30
  sendI2CCommand(0x10);  // 0x10 is the brightest backlight.   

  // Init Display
  gfx.init();
  gfx.initDMA();
  gfx.startWrite();
  gfx.fillScreen(TFT_BLACK);

  lv_init();
  size_t buffer_size = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
  buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  buf1 = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);

  lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * LCD_V_RES);

  // Initialize display
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  // Change the following lines to your display resolution
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Initialize input device driver program
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  delay(100);
  gfx.fillScreen(TFT_BLACK);

  loadState();
  if (!pumpsReady) Serial.println("pump driver not responding");
  if (!cupSensorReady)
    Serial.println("cup sensor not responding - pours refuse until it answers or is "
                   "disabled in settings");

  // setup() sent 0x10 to bring the panel up at full brightness before anything
  // could be read off the card; this is where the owner's own level lands.
  {
    I2cGuard guard;
  }

  static PortalState portalState;
  portalState.fs = &sdFs;
  portalState.library = &library;
  portalState.config = &config;
  portalState.pouring = [] { return scheduler.state() == pour::State::pouring; };
  if (!portalStart(portalState)) Serial.println("portal failed to start");

  UiState uiState;
  uiState.library = &library;
  uiState.config = &config;
  uiState.scheduler = &scheduler;
  uiState.fs = &sdFs;
  uiState.cupPresent = [] { return tof.cupPresent(); };
  uiState.cupDistanceMm = [] { return tof.lastMm(); };
  uiState.cupSensorWired = [] { return tof.sensorPresent(); };
  uiState.setPump = [](int channel, bool on) { pumps.set(channel, on); };
  uiState.persistConfig = [] { bartender::saveConfig(sdFs, portal::kConfigPath, config); };
  uiState.persistLibrary = [] { bartender::saveLibrary(sdFs, portal::kRecipesPath, library); };
  uiStart(uiState);

  xTaskCreatePinnedToCore(pourTask, "pour", 4096, nullptr, POUR_TASK_PRIORITY, nullptr,
                          POUR_TASK_CORE);

  scanI2c();
  if (!led::begin(config.ring.pixels)) Serial.println("led ring failed to start");
  Serial.println( "Setup done" );
}

void loop()
{
  lv_timer_handler(); /* let the GUI do its work */
  delay(1);
}
