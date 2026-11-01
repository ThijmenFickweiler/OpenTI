/*
 * OpenTI hardware template -- ESP32-S3-WROOM-1-N16
 * Sources: OpenTI-84-EasyEDA-SCH.json and OpenTI-84-EasyEDA-PCB.json.
 * See PINOUT.md for traceability, address-annotation conflicts, restrictions,
 * the disconnected keypad contacts, and display daughterboard uncertainties.
 *
 * Arduino IDE: use the ESP32-S3 board package. Arduino normally requires the
 * primary sketch and its enclosing folder to have the same name: if prompted,
 * allow the IDE to move/copy this file into an OpenTI sketch folder.
 *
 * This is a foundation for YOUR firmware, not a finished calculator OS.
 * Change the examples below to make your own menus, applications, games, etc.
 * The examples are disabled by default and have not been tested on hardware.
 *
 * Getting started:
 * 1. Select an ESP32-S3 board and the correct Serial/USB setting in Arduino IDE.
 * 2. Read ../PINOUT.md and resolve the hardware warnings before powering a board.
 * 3. After verifying a connection, adapt the relevant example below and enable
 *    only what you need. Lines marked TODO need hardware information first.
 *
 * The schematic shows one-pin keypad pads, not switches to ground: until the
 * physical keypad return path is verified, raw levels are NOT key events.
 * Do not drive display/LED pins until the daughterboard and circuit are verified.
 */
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// ---- ESP32-S3 direct connections (numbers are GPIO, not module pin numbers)
constexpr int PIN_BATTERY_ADC = 4;       // ADC1_CH3, R12/R11 = 10k/10k
constexpr int PIN_CHARGE_STATUS = 5;     // BQ24075 CHG#, active low
constexpr int PIN_LED_RED = 6;           // via R22 to RGB LED U10
constexpr int PIN_LED_GREEN = 7;         // via R23
constexpr int PIN_LED_BLUE = 15;         // via R24
constexpr int PIN_I2C_SDA = 16;
constexpr int PIN_I2C_SCL = 17;
constexpr int PIN_USB_D_MINUS = 19;
constexpr int PIN_USB_D_PLUS = 20;
constexpr int PIN_LED_BRIGHTNESS = 3;    // Q10 gate, strapping pin; avoid driving
constexpr int PIN_BOOT = 0;              // U14, strapping pin

// Display: daughterboard-facing 1PAD contacts, not a confirmed FPC pinout.
constexpr int PIN_DISPLAY_DC = 9;        // Display_RS; D/C role is inferred
constexpr int PIN_DISPLAY_CS = 10;
constexpr int PIN_DISPLAY_MOSI = 11;     // Display_SDA; SPI role is inferred
constexpr int PIN_DISPLAY_SCK = 12;
constexpr int PIN_DISPLAY_BACKLIGHT = 13; // daughterboard pad only
constexpr int PIN_DISPLAY_RESET = 14;
constexpr int PIN_DISPLAY_MISO = -1;    // TODO: no connection documented

// ---- I2C: actual seven-bit addresses derived from A2/A1/A0 straps.
// The drawing's 0x42/0x40 write annotations disagree with those straps.
constexpr uint8_t I2C_ADDRESS_U11 = 0x22; // 0/1/0; write byte 0x44
constexpr uint8_t I2C_ADDRESS_U12 = 0x21; // 0/0/1; write byte 0x42
constexpr uint32_t I2C_FREQUENCY_HZ = 100000;
// TODO: enable only after checking the exact FCA9535T datasheet and board.
constexpr bool ENABLE_EXPANDER_CONFIGURATION = false;
// TODO: enable only when Serial is confirmed to use native USB, not UART0.
constexpr bool ENABLE_UART0_KEY_INPUTS = false;

// FCA9535/PCA9535-family register layout. Nyfea's exact PDF remains to be
// checked; this template uses single-register transactions (no auto-increment).
constexpr uint8_t EXPANDER_INPUT_0 = 0x00;
constexpr uint8_t EXPANDER_INPUT_1 = 0x01;
constexpr uint8_t EXPANDER_OUTPUT_0 = 0x02;
constexpr uint8_t EXPANDER_OUTPUT_1 = 0x03;
constexpr uint8_t EXPANDER_POLARITY_0 = 0x04;
constexpr uint8_t EXPANDER_POLARITY_1 = 0x05;
constexpr uint8_t EXPANDER_CONFIG_0 = 0x06;
constexpr uint8_t EXPANDER_CONFIG_1 = 0x07; // 1=input, 0=output

// ---- Complete named keypad connections; low raw level is only a candidate
// press after the physical switch return path has been validated.
enum ButtonId {
  BUTTON_PROGRAM, BUTTON_MODE, BUTTON_LEFT, BUTTON_DOWN, BUTTON_RIGHT,
  BUTTON_UP, BUTTON_DELETE, BUTTON_ZOOM, BUTTON_WINDOW, BUTTON_STORE,
  BUTTON_ALPHA, BUTTON_2ND, BUTTON_ON, BUTTON_ENTER, BUTTON_RESET,
  BUTTON_CLEAR, BUTTON_TRACE, BUTTON_GRAPH, BUTTON_Y_EQUALS,
  BUTTON_LN, BUTTON_LOG, BUTTON_SQUARE, BUTTON_RECIPROCAL, BUTTON_MATH,
  BUTTON_DIVIDE, BUTTON_POWER, BUTTON_VARS, BUTTON_X_T_THETA_N, BUTTON_APPS,
  BUTTON_SIN, BUTTON_COMMA, BUTTON_7, BUTTON_4, BUTTON_1, BUTTON_0,
  BUTTON_DECIMAL, BUTTON_2, BUTTON_5, BUTTON_8, BUTTON_LEFT_PAREN,
  BUTTON_COS, BUTTON_NEGATE, BUTTON_3, BUTTON_9, BUTTON_STAT,
  BUTTON_RIGHT_PAREN, BUTTON_TAN, BUTTON_MULTIPLY, BUTTON_SUBTRACT,
  BUTTON_ADD, BUTTON_6, BUTTON_COUNT
};

enum ButtonSource { DIRECT_GPIO, EXPANDER_U11, EXPANDER_U12 };
struct ButtonMapping {
  ButtonId id;
  const char *name;
  ButtonSource source;
  int8_t gpio; // -1 for expander buttons
  uint8_t port;
  uint8_t bit;
};
#define DIRECT(id, pin) {id, #id, DIRECT_GPIO, pin, 0, 0}
#define U11_KEY(id, port, bit) {id, #id, EXPANDER_U11, -1, port, bit}
#define U12_KEY(id, port, bit) {id, #id, EXPANDER_U12, -1, port, bit}
const ButtonMapping BUTTONS[] = {
  DIRECT(BUTTON_PROGRAM, 18), DIRECT(BUTTON_MODE, 8),
  DIRECT(BUTTON_LEFT, 46), DIRECT(BUTTON_DOWN, 21),
  DIRECT(BUTTON_RIGHT, 47), DIRECT(BUTTON_UP, 48),
  DIRECT(BUTTON_DELETE, 45), DIRECT(BUTTON_ZOOM, 35),
  DIRECT(BUTTON_WINDOW, 36), DIRECT(BUTTON_STORE, 37),
  DIRECT(BUTTON_ALPHA, 38), DIRECT(BUTTON_2ND, 39),
  DIRECT(BUTTON_ON, 40), DIRECT(BUTTON_ENTER, 41),
  DIRECT(BUTTON_RESET, 42), DIRECT(BUTTON_CLEAR, 44),
  DIRECT(BUTTON_TRACE, 43), DIRECT(BUTTON_GRAPH, 2),
  DIRECT(BUTTON_Y_EQUALS, 1),
  U11_KEY(BUTTON_LN, 0, 0), U11_KEY(BUTTON_LOG, 0, 1),
  U11_KEY(BUTTON_SQUARE, 0, 2), U11_KEY(BUTTON_RECIPROCAL, 0, 3),
  U11_KEY(BUTTON_MATH, 0, 4), U11_KEY(BUTTON_DIVIDE, 0, 5),
  U11_KEY(BUTTON_POWER, 0, 6), U11_KEY(BUTTON_VARS, 0, 7),
  U11_KEY(BUTTON_X_T_THETA_N, 1, 0), U11_KEY(BUTTON_APPS, 1, 1),
  U11_KEY(BUTTON_SIN, 1, 2), U11_KEY(BUTTON_COMMA, 1, 3),
  U11_KEY(BUTTON_7, 1, 4), U11_KEY(BUTTON_4, 1, 5),
  U11_KEY(BUTTON_1, 1, 6), U11_KEY(BUTTON_0, 1, 7),
  U12_KEY(BUTTON_DECIMAL, 0, 0), U12_KEY(BUTTON_2, 0, 1),
  U12_KEY(BUTTON_5, 0, 2), U12_KEY(BUTTON_8, 0, 3),
  U12_KEY(BUTTON_LEFT_PAREN, 0, 4), U12_KEY(BUTTON_COS, 0, 5),
  U12_KEY(BUTTON_NEGATE, 0, 6), U12_KEY(BUTTON_3, 0, 7),
  U12_KEY(BUTTON_9, 1, 0), U12_KEY(BUTTON_STAT, 1, 1),
  U12_KEY(BUTTON_RIGHT_PAREN, 1, 2), U12_KEY(BUTTON_TAN, 1, 3),
  U12_KEY(BUTTON_MULTIPLY, 1, 4), U12_KEY(BUTTON_SUBTRACT, 1, 5),
  U12_KEY(BUTTON_ADD, 1, 6), U12_KEY(BUTTON_6, 1, 7)
};
#undef DIRECT
#undef U11_KEY
#undef U12_KEY
static_assert(sizeof(BUTTONS) / sizeof(BUTTONS[0]) == BUTTON_COUNT,
              "Every named key must have exactly one mapping");

// Explicit declarations keep Arduino's sketch preprocessor from generating
// prototypes that refer to custom types before those types are declared.
struct KeypadSnapshot;
bool readKeypadRaw(const ButtonMapping &button, const KeypadSnapshot &snapshot,
                   bool &low);
void pollKeypad(bool u11Ready, bool u12Ready);

// ---- Basic I2C / expander access; propagate errors, never return stale data.
bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(1)) != 1) return false;
  value = static_cast<uint8_t>(Wire.read());
  return true;
}

bool initExpander(uint8_t address) {
  // Explicitly return both ports to input even after an MCU-only reset.
  // If either operation fails, do not read that expander as a keypad.
  const bool p0 = i2cWriteRegister(address, EXPANDER_CONFIG_0, 0xff);
  const bool p1 = i2cWriteRegister(address, EXPANDER_CONFIG_1, 0xff);
  const bool inv0 = i2cWriteRegister(address, EXPANDER_POLARITY_0, 0x00);
  const bool inv1 = i2cWriteRegister(address, EXPANDER_POLARITY_1, 0x00);
  return p0 && p1 && inv0 && inv1;
}

bool readExpanderPorts(uint8_t address, uint8_t &port0, uint8_t &port1) {
  uint8_t p0, p1;
  if (!i2cReadRegister(address, EXPANDER_INPUT_0, p0) ||
      !i2cReadRegister(address, EXPANDER_INPUT_1, p1)) return false;
  port0 = p0;
  port1 = p1;
  return true;
}

// ---- Keypad framework. The two devices are independent inputs, not a matrix.
struct KeypadSnapshot {
  uint8_t u11[2];
  uint8_t u12[2];
  bool u11Valid;
  bool u12Valid;
};

bool readKeypadRaw(const ButtonMapping &button, const KeypadSnapshot &snapshot,
                   bool &low) {
  if (button.source == DIRECT_GPIO) {
    // GPIO43/44 may be occupied by UART0; never claim an unavailable reading.
    if (!ENABLE_UART0_KEY_INPUTS &&
        (button.id == BUTTON_TRACE || button.id == BUTTON_CLEAR)) return false;
    low = digitalRead(button.gpio) == LOW;
    return true;
  }
  const bool u11 = button.source == EXPANDER_U11;
  if (!(u11 ? snapshot.u11Valid : snapshot.u12Valid)) return false;
  const uint8_t *ports = u11 ? snapshot.u11 : snapshot.u12;
  low = (ports[button.port] & (1U << button.bit)) == 0;
  return true;
}

void initDirectKeyInputs() {
  for (const auto &button : BUTTONS) {
    if (button.source != DIRECT_GPIO) continue;
    if (!ENABLE_UART0_KEY_INPUTS &&
        (button.id == BUTTON_TRACE || button.id == BUTTON_CLEAR)) continue;
    pinMode(button.gpio, INPUT);
  }
  // External 10k pull-ups are shown; avoid adding pulls on strapping pins.
}

// TODO: verify physical contacts and return to ground before turning raw lows
// into events; add debounce and state-change handling once confirmed.
void pollKeypad(bool u11Ready, bool u12Ready) {
  KeypadSnapshot snapshot = {};
  if (u11Ready) snapshot.u11Valid = readExpanderPorts(I2C_ADDRESS_U11, snapshot.u11[0], snapshot.u11[1]);
  if (u12Ready) snapshot.u12Valid = readExpanderPorts(I2C_ADDRESS_U12, snapshot.u12[0], snapshot.u12[1]);
  if ((u11Ready && !snapshot.u11Valid) || (u12Ready && !snapshot.u12Valid)) {
    Serial.println("Keypad I2C read failed; affected port data invalid");
  }
  // EXAMPLE: after verifying the keypad contacts, you can uncomment this
  // diagnostic to print each raw low level. It repeats while a key is held:
  // for (const auto &button : BUTTONS) {
  //   bool low = false;
  //   if (readKeypadRaw(button, snapshot, low) && low) {
  //     Serial.println(button.name);
  //   }
  // }
  // For applications, add debounce and detect a transition from released to
  // pressed before acting once. An unavailable expander returns false; never
  // interpret it as a released key. TRACE/CLEAR also require the USB setting.
  // Example action after debouncing: if (button.id == BUTTON_ENTER) { ... }
  // No key events are emitted until physical contact behavior is verified.
}

void initDisplay() {
  // TODO: obtain the daughterboard schematic, controller and power sequence.
  // Do not call SPI.begin() or configure outputs until the interface is verified.
  // FOUNDATION IDEA: once you know the controller, use its datasheet/driver
  // here to initialize the screen, then add your own drawText() or menu code.
  // PIN_DISPLAY_* names above describe the board-side nets, not a proven driver.
  Serial.println("Display disabled: controller and daughterboard pinout unknown");
}

void initPowerMonitoring() {
  pinMode(PIN_CHARGE_STATUS, INPUT); // CHG# has external pull-up
  // TODO: validate ADC attenuation/calibration and divider before battery reads.
  // Battery voltage is theoretically twice the ADC voltage (R12/R11 = 10k).
  // EXAMPLE after verifying the divider, input range, attenuation and calibration:
  // uint32_t adcMv = analogReadMilliVolts(PIN_BATTERY_ADC);
  // float batteryVolts = (adcMv * 2.0f) / 1000.0f;
  // Serial.println(batteryVolts); // approximate, not a battery percentage
}

void initLeds() {
  // TODO: verify RGB LED polarity and Q10 gate/strap behavior before driving.
  // Q10 gate is also GPIO3: an externally pulled-up strapping pin.
  // FOUNDATION IDEA: once electrically verified, add functions such as
  // setChargingColor() here and choose colors for your own applications.
}

bool u11Ready = false;
bool u12Ready = false;
bool i2cReady = false;

void setup() {
  Serial.begin(115200);
  Serial.println("OpenTI hardware template: no hardware tests performed");
  initDirectKeyInputs();
  initPowerMonitoring();
  initLeds();
  initDisplay();

  i2cReady = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQUENCY_HZ);
  if (!i2cReady) {
    Serial.println("I2C initialization failed");
    return;
  }
  if (ENABLE_EXPANDER_CONFIGURATION) {
    u11Ready = initExpander(I2C_ADDRESS_U11);
    u12Ready = initExpander(I2C_ADDRESS_U12);
    if (!u11Ready) Serial.println("U11 not initialized at 7-bit address 0x22");
    if (!u12Ready) Serial.println("U12 not initialized at 7-bit address 0x21");
  } else {
    Serial.println("Expander configuration disabled pending datasheet verification");
  }
  Serial.println("TODO: verify expander address straps and physical keypad");
}

void loop() {
  if (i2cReady) pollKeypad(u11Ready, u12Ready);
  // EXAMPLE: CHG# is active low. After checking the actual charger circuit,
  // uncomment this to see the signal in Serial Monitor. LOW indicates the
  // charger asserts CHG#; HIGH alone does not prove the battery is full.
  // bool chargeAsserted = digitalRead(PIN_CHARGE_STATUS) == LOW;
  // Serial.println(chargeAsserted ? "CHG# active" : "CHG# inactive");
  //
  // Add your application logic here: update a menu, run a game, etc.
  // The current delay also sets the example keypad polling interval. Use a
  // shorter non-blocking millis()-based schedule when adding responsive input.
  delay(250);
}
