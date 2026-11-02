#include <Arduino.h>
#include <Wire.h>

#include <cstring>

#include "ac_panel.h"
#include "attitude.h"
#include "bridge.h"
#include "config.h"
#include "fw_version.h"
#include "hal/ble_bthome.h"
#include "hal/ble_ota.h"
#include "hal/ble_server.h"
#include "hal/ble_unit_link.h"
#include "hal/can_twai.h"
#include "hal/mpu6050.h"
#include "hal/nvs_store.h"
#include "hal/ota_flash.h"
#include "hal/pair_window.h"
#include "hal/status_led.h"
#include "node.h"
#include "ota_trial.h"
#include "schedule.h"
#include "sensor_health.h"

namespace {

CanTwai can;
Mpu6050 mpu;
NvsStore store;
BleBthome ble;
StatusLed led(cfg::PIN_LED_R, cfg::PIN_LED_G, cfg::PIN_LED_B, cfg::LED_BRIGHTNESS_PCT);
level::AttitudeFilter filter(cfg::FILTER_TAU_S, cfg::MOTION_GYRO_DPS);
level::GyroBias gyroBias;
level::SensorHealth health;
char productId[64];
rvc::Node* node = nullptr;
uint32_t lastSampleMs = 0;
uint32_t lastPrintMs = 0;

OtaFlash otaFlash;
BleServer bleServer;
BleOta bleOta(otaFlash, bleServer, kFwVersion);
// Without a GATT service there is no server: the sensor only broadcasts, as before the AC bridge.
constexpr bool kBleServerWanted = cfg::AC_BRIDGE_ENABLED || !cfg::BLE_OTA_DISABLED;
bool bleServerUp = false;
bool bleOtaUp = false;  // BleOta::begin succeeded; its mutex exists only then
bool fastCpu = false;   // at CPU_MHZ_UPDATE while the update window is open
// AC bridge (cfg::AC_BRIDGE_ENABLED): the unit link, the bridge and the panel's instance-7 AC.
BleUnitLink unitLink(bleServer);
ac::Bridge bridge(unitLink);
rvc::AcPanel acPanel(bridge, can);
PairWindow pairWindow;
bool unitLinkUp = false;  // BleUnitLink::begin succeeded
bool pairWasOpen = false;
bool onTrial = false;       // booted a new image that hasn't confirmed itself yet
bool imuValidSeen = !cfg::LEVELING_ENABLED;  // part of the trial's health check; no IMU to wait for without leveling

#ifdef OTA_TEST_NEVER_CONFIRM
constexpr bool kNeverConfirm = true;  // test build: proves rollback on the coach
#else
constexpr bool kNeverConfirm = false;
#endif

void settleTrial(uint32_t now) {
  if (!onTrial) return;
  switch (ota::decideTrial(true, now, node->claimed() && !kNeverConfirm, imuValidSeen)) {
    case ota::TrialAction::Confirm:
      OtaFlash::confirm();
      onTrial = false;
      Serial.println("new firmware confirmed");
      break;
    case ota::TrialAction::RollBack:
      Serial.println("new firmware not healthy after 60 s: rolling back");
      Serial.flush();
      OtaFlash::rollBack();
    default:
      break;
  }
}

void logLine(const char* line) { Serial.println(line); }

void sampleSensor(uint32_t now) {
  if (health.faulted()) {
    if (!health.shouldRetryInit(now) || !mpu.begin(Wire)) return;
    filter.reset();
  }
  level::Vec3 accel, gyro;
  if (!mpu.read(accel, gyro)) {
    health.onReadFail(now);
    if (health.faulted()) filter.reset();
    return;
  }
  health.onReadOk();
  level::AxisMap map{cfg::AXIS_FORWARD, cfg::AXIS_DRIVER};
  filter.update(level::mapToCoach(accel, map), gyroBias.correct(gyro), now);
}

rvc::SensorSnapshot snapshot(uint32_t now) {
  rvc::SensorSnapshot s;
  s.faulted = health.faulted();
  s.valid = !s.faulted && filter.seeded();
  s.moving = !gyroBias.ready() || filter.moving(now);
  s.rawPitchDeg = filter.pitchDeg();
  s.rawRollDeg = filter.rollDeg();
  return s;
}

bthome::Reading bleReading(const rvc::SensorSnapshot& s) {
  bthome::Reading r{};
  r.valid = s.valid;
  r.level = s.valid && !node->bits().any();
  r.moving = s.moving;
  r.problem = s.faulted;
  r.pitchDeg = node->pitchDeg();
  r.rollDeg = node->rollDeg();
  return r;
}

void printSummary() {
  level::LevelBits b = node->bits();
  const level::BlockPlan& p = node->blockPlan();
  Serial.printf("SA=%u pitch=%+.2f roll=%+.2f low[D F P R]=%d%d%d%d layers[FL FR RL RR]=%lu %lu %lu %lu/%u "
                "fault=%d moving=%d busoff=%d drops=%lu\n",
                node->address(), node->pitchDeg(), node->rollDeg(), b.driverLow, b.frontLow, b.passengerLow,
                b.rearLow, static_cast<unsigned long>(p.layers[level::kFrontLeft]),
                static_cast<unsigned long>(p.layers[level::kFrontRight]),
                static_cast<unsigned long>(p.layers[level::kRearLeft]),
                static_cast<unsigned long>(p.layers[level::kRearRight]), node->settings().blocks.blocksCarried,
                health.faulted(), filter.moving(millis()), can.busOff(), static_cast<unsigned long>(can.dropped()));
}

const char* modeName(cm::Mode m) {
  switch (m) {
    case cm::Mode::Cool: return "cool";
    case cm::Mode::Eco: return "eco";
    case cm::Mode::Sleep: return "sleep";
    case cm::Mode::Turbo: return "turbo";
    case cm::Mode::FanOnly: return "fan";
    case cm::Mode::Dehumid: return "dehumid";
    default: return "?";
  }
}

void printAcSummary(uint32_t now) {
  const cm::UnitStatus& u = bridge.status();
  Serial.printf("AC link=%d responding=%d power=%d mode=%s fan=%u setpoint=%u%s air=%uF battery=%uV pair=%d\n",
                unitLink.connected(), bridge.responding(now), u.power, modeName(u.mode), u.fan, u.setpointRaw,
                u.celsius ? "C" : "F", u.airTempF, u.batteryV, pairWindow.isOpen(now));
}

void openPairWindow(uint32_t now) {
  pairWindow.open(now);
  Serial.printf("pairing window open (%lu s)\n", static_cast<unsigned long>(cfg::PAIR_WINDOW_MS / 1000));
  if (unitLinkUp && !unitLink.bound()) unitLink.startBindScan(now);
}

void forgetHa() {
  store.clearHaKey();
  // The HA service (Task 9) must also drop its current session here.
  Serial.println("Home Assistant pairing cleared");
}

void reboot(const char* why) {
  Serial.printf("rebooting (%s)\n", why);
  Serial.flush();
  ESP.restart();
}

// AC console commands: "pair" opens the pairing window (binds the AC unit if none is bound, lets
// HA pair), "forget-ac" drops the bound unit, "unpair" clears the HA pairing.
bool handleAcCommand(const char* cmd) {
  uint32_t now = millis();
  if (std::strcmp(cmd, "pair") == 0) {
    openPairWindow(now);
  } else if (std::strcmp(cmd, "forget-ac") == 0) {
    unitLink.forget();
    store.clearUnitAddress();
    Serial.println("AC unit forgotten; 'pair' binds one again");
  } else if (std::strcmp(cmd, "unpair") == 0) {
    forgetHa();
  } else {
    return false;
  }
  return true;
}

// Serial console: one command per line. "zero" sets pitch and roll zero where the sensor sits;
// "reboot" restarts the board. AC builds add handleAcCommand's.
void handleCommand(const char* cmd) {
  if (cfg::AC_BRIDGE_ENABLED && handleAcCommand(cmd)) return;
  if (std::strcmp(cmd, "zero") == 0) {
    Serial.println(node->zeroHere() ? "zero set (pitch and roll)"
                                    : "zero rejected: sensor moving or not ready, hold it still and retry");
  } else if (std::strcmp(cmd, "reboot") == 0) {
    reboot("serial");
  } else {
    Serial.printf("unknown command '%s' (commands: zero, reboot%s)\n", cmd,
                  cfg::AC_BRIDGE_ENABLED ? ", pair, forget-ac, unpair" : "");
  }
}

void pollSerial() {
  static char line[16];
  static size_t len = 0;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      line[len] = '\0';
      if (len) handleCommand(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

}  // namespace

// The Arduino core confirms a new OTA image at boot unless this returns true; settleTrial() decides
// instead.
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  setCpuFrequencyMhz(cfg::CPU_MHZ);
  Serial.begin(115200);
  led.begin();
  Wire.begin(cfg::PIN_SDA, cfg::PIN_SCL, 400000);

  uint64_t mac = ESP.getEfuseMac();  // byte 0 = first MAC octet
  uint8_t m[6];
  for (int i = 0; i < 6; ++i) m[i] = static_cast<uint8_t>(mac >> (8 * i));
  snprintf(productId, sizeof(productId), "%s*%s*%02X%02X%02X%02X%02X%02X*", cfg::MAKE, cfg::MODEL, m[0], m[1],
           m[2], m[3], m[4], m[5]);
  uint32_t serial = (static_cast<uint32_t>(m[3]) << 16) | (m[4] << 8) | m[5];

  rvc::NodeConfig nc;
  nc.name = rvc::makeName(serial, cfg::MANUFACTURER_CODE);
  nc.saHigh = cfg::SA_RANGE_HIGH;
  nc.saLow = cfg::SA_RANGE_LOW;
  nc.instance = cfg::SENSOR_INSTANCE;
  nc.dsa = cfg::DSA;
  nc.leveling = cfg::LEVELING_ENABLED;
  nc.productId = productId;
  nc.statusMinGapMs = cfg::STATUS_MIN_GAP_MS;
  nc.statusHeartbeatMs = cfg::STATUS_HEARTBEAT_MS;
  nc.statusDeadbandDeg = cfg::STATUS_DEADBAND_DEG;
  nc.wheelHeartbeatMs = cfg::WHEEL_HEARTBEAT_MS;
  nc.wheelMinGapMs = cfg::WHEEL_MIN_GAP_MS;
  nc.wheelDeadbandDeg = cfg::WHEEL_DEADBAND_DEG;
  nc.log = logLine;

  store.begin();
  static rvc::Node n(nc, store.loadSettings(), can, store);
  node = &n;

  if (!can.begin(cfg::PIN_CAN_TX, cfg::PIN_CAN_RX)) Serial.println("TWAI init failed");
  if constexpr (cfg::LEVELING_ENABLED) {
    if (!mpu.begin(Wire)) {
      Serial.printf("MPU-6050 init failed (WHO_AM_I=0x%02X)\n", mpu.whoAmI());
      health.markFaulted(millis());
    }
  }
  if constexpr (cfg::BLE_ENABLED) {
    if (!ble.begin(cfg::BLE_NAME, cfg::BLE_ADV_INTERVAL_MS, cfg::BLE_REFRESH_MS, cfg::BLE_REAPPLY_MS, nullptr)) {
      Serial.println("BLE init failed");
    } else {
      if (kBleServerWanted) {
        bleServerUp = bleServer.begin(ble);
        if (!bleServerUp) Serial.println("BLE server init failed");
      }
      if (cfg::BLE_OTA_DISABLED) {
        Serial.println("Bluetooth updates disabled in this build");
      } else if (!bleServerUp || !bleOta.begin()) {
        Serial.println("BLE update service init failed");
      } else {
        bleOtaUp = true;
        node->setUpdateWindowSupported(true);
      }
    }
  }
  if constexpr (cfg::AC_BRIDGE_ENABLED) {
    uint8_t addr[6], type, key[32];
    bool haveUnit = store.loadUnitAddress(addr, type);
    bool haveHa = store.loadHaKey(key);
    std::memset(key, 0, sizeof key);
    if (!bleServerUp || !unitLink.begin(bridge)) {
      Serial.println("AC unit link init failed");
    } else {
      unitLinkUp = true;
      if (haveUnit) unitLink.bindTo(NimBLEAddress(addr, type));
    }
    if (!haveUnit && !haveHa) openPairWindow(millis());  // first boot: nothing to pair with yet
  }
  Serial.printf("RV-C level sensor %s firmware %s (slot %u)\n", productId, kFwVersion, OtaFlash::runningSlot());
  onTrial = OtaFlash::runningOnTrial();
  if (onTrial) Serial.println("new firmware on trial: confirms once claimed and reading, else rolls back at 60 s");
  node->begin(store.loadAddress(), millis());
}

void loop() {
  // Sleep in the CAN receive until a frame arrives or the next sample is due, rather than
  // spinning: the idle CPU waits for an interrupt, which is most of the board's power saving.
  rvc::CanFrame f;
  uint32_t wait = level::msUntilDue(millis(), lastSampleMs, cfg::SAMPLE_PERIOD_MS);
  if (can.receive(f, wait)) {
    do {
      node->onFrame(f, millis());
      if constexpr (cfg::AC_BRIDGE_ENABLED) acPanel.onFrame(f, millis());
    } while (can.receive(f));
  }
  uint32_t now = millis();

  if (can.pollRecovered()) {
    Serial.println("CAN bus recovered, reclaiming address");
    node->restartClaim(now);
  }

  if (now - lastSampleMs >= cfg::SAMPLE_PERIOD_MS) {
    lastSampleMs = now;
    rvc::SensorSnapshot s;
    if constexpr (cfg::LEVELING_ENABLED) {
      sampleSensor(now);
      s = snapshot(now);
      if (s.valid) imuValidSeen = true;
    }
    node->update(s, now);
    if constexpr (cfg::BLE_ENABLED) {
      if constexpr (cfg::LEVELING_ENABLED) {
        ble.update(bleReading(s), now);
      } else {
        ble.update(now);  // name only, so the AC service can be found
      }
    }
  }
  if (node->rebootDue(now)) reboot("GENERAL_RESET");
  settleTrial(now);
  if (bleOtaUp) {
    if (node->takeUpdateWindowRequest()) {
      bleOta.openWindow(now);
      Serial.println("update window open (5 min)");
    }
    bleOta.poll(now);
    if (bleOta.rebootDue(now)) reboot("firmware update");
    bool open = bleOta.windowOpen();
    if (open != fastCpu) {
      setCpuFrequencyMhz(open ? cfg::CPU_MHZ_UPDATE : cfg::CPU_MHZ);
      fastCpu = open;
    }
  }
  if constexpr (cfg::AC_BRIDGE_ENABLED) {
    if (unitLinkUp) unitLink.poll(now);  // before bleServer.poll, which acts on its slot reservation
    bridge.tick(now);
    acPanel.update(node->claimed(), node->address(), now);
    uint8_t addr[6], type;
    if (unitLink.takeNewBinding(addr, type)) store.saveUnitAddress(addr, type);
    bool pairOpen = pairWindow.isOpen(now);
    if (pairWasOpen && !pairOpen) Serial.println("pairing window closed");
    pairWasOpen = pairOpen;
  }
  if (bleServerUp) bleServer.poll();  // after bleOta.poll and unitLink.poll, which set what they want

  LedMode mode = LedMode::Level;
  if ((cfg::LEVELING_ENABLED && health.faulted()) || can.busOff()) {
    mode = LedMode::Fault;
  } else if (!node->claimed()) {
    mode = LedMode::Claiming;
  } else if (cfg::LEVELING_ENABLED && node->bits().any()) {
    mode = LedMode::OutOfLevel;
  }
  led.update(mode, now);

  pollSerial();

  if (now - lastPrintMs >= 1000) {
    lastPrintMs = now;
    printSummary();
    if constexpr (cfg::AC_BRIDGE_ENABLED) printAcSummary(now);
  }
}
