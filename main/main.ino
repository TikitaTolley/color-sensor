#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

constexpr int SENSOR_RX = 9;  // Connected to GY-33 CT
constexpr int SENSOR_TX = 8;  // Connected to GY-33 DR
constexpr int OLED_SDA = 4;
constexpr int OLED_SCL = 5;
constexpr int SCAN_BUTTON_PIN = 7;
constexpr int SAVE_BUTTON_PIN = 10;
constexpr int CLEAR_BUTTON_PIN = 12;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_RESET = -1;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t SCAN_SAMPLE_COUNT = 10;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 30;
constexpr float COLOUR_DISTANCE_WEIGHT = 400.0f;
constexpr float BRIGHTNESS_DIFFERENCE_WEIGHT = 60.0f;
constexpr int MATCH_SCORE_THRESHOLD = 85;
constexpr int SIMILAR_SCORE_THRESHOLD = 45;

// Calibration measured with the final sensor enclosure fitted.
// Mean of three scans each of black and white felt; comparison validation pending.
constexpr float BLACK_RED = 38.0f;
constexpr float BLACK_GREEN = 64.0f;
constexpr float BLACK_BLUE = 58.67f;
constexpr float BLACK_CLEAR = 175.0f;
constexpr float WHITE_RED = 130.0f;
constexpr float WHITE_GREEN = 176.33f;
constexpr float WHITE_BLUE = 163.33f;
constexpr float WHITE_CLEAR = 496.0f;

struct ColourReading {
  uint16_t red;
  uint16_t green;
  uint16_t blue;
  uint16_t clear;
};

struct CalibratedReading {
  float red;
  float green;
  float blue;
  float clear;
};

struct MatchResult {
  float colourDistance;
  float brightnessDifference;
  int score;
  const char* label;
};

struct Button {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeAt;
};

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

uint8_t packet[13];
size_t packetPosition = 0;
unsigned long lastByteAt = 0;
unsigned long lastReadingAt = 0;

Button scanButton = {SCAN_BUTTON_PIN, HIGH, HIGH, 0};
Button saveButton = {SAVE_BUTTON_PIN, HIGH, HIGH, 0};
Button clearButton = {CLEAR_BUTTON_PIN, HIGH, HIGH, 0};

bool scanInProgress = false;
uint8_t samplesCollected = 0;
uint32_t redTotal = 0;
uint32_t greenTotal = 0;
uint32_t blueTotal = 0;
uint32_t clearTotal = 0;

ColourReading currentReading = {0, 0, 0, 0};
ColourReading savedColourA = {0, 0, 0, 0};
ColourReading savedColourB = {0, 0, 0, 0};
bool currentReadingAvailable = false;
bool colourASaved = false;
bool colourBSaved = false;

void sendCommand(uint8_t command) {
  const uint8_t message[] = {
    0xA5,
    command,
    static_cast<uint8_t>(0xA5 + command)
  };
  Serial1.write(message, sizeof(message));
}

uint16_t readChannel(size_t offset) {
  return (static_cast<uint16_t>(packet[offset]) << 8)
    | packet[offset + 1];
}

float calibrateChannel(uint16_t raw, float black, float white) {
  const float corrected = 255.0f * (static_cast<float>(raw) - black) / (white - black);
  return constrain(corrected, 0.0f, 255.0f);
}

CalibratedReading calibrateReading(const ColourReading& reading) {
  return {
    calibrateChannel(reading.red, BLACK_RED, WHITE_RED),
    calibrateChannel(reading.green, BLACK_GREEN, WHITE_GREEN),
    calibrateChannel(reading.blue, BLACK_BLUE, WHITE_BLUE),
    calibrateChannel(reading.clear, BLACK_CLEAR, WHITE_CLEAR)
  };
}

int roundedChannel(float value) {
  return static_cast<int>(value + 0.5f);
}

MatchResult compareReadings(const ColourReading& a, const ColourReading& b) {
  const float clearA = max(static_cast<float>(a.clear), 1.0f);
  const float clearB = max(static_cast<float>(b.clear), 1.0f);

  const float redDifference = (a.red / clearA) - (b.red / clearB);
  const float greenDifference = (a.green / clearA) - (b.green / clearB);
  const float blueDifference = (a.blue / clearA) - (b.blue / clearB);
  const float colourDistance = sqrtf(
    redDifference * redDifference
    + greenDifference * greenDifference
    + blueDifference * blueDifference
  );
  const float largerClear = max(clearA, clearB);
  const float brightnessDifference = abs(clearA - clearB) / largerClear;
  const float rawScore = 100.0f
    - colourDistance * COLOUR_DISTANCE_WEIGHT
    - brightnessDifference * BRIGHTNESS_DIFFERENCE_WEIGHT;
  const int score = roundedChannel(constrain(rawScore, 0.0f, 100.0f));

  const char* label = "Different";
  if (score >= MATCH_SCORE_THRESHOLD) {
    label = "Match";
  } else if (score >= SIMILAR_SCORE_THRESHOLD) {
    label = "Similar";
  }

  return {colourDistance, brightnessDifference, score, label};
}

void startScreen(const char* heading) {
  if (!oledReady) return;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(heading);
  display.println();
}

void finishScreen() {
  if (!oledReady) return;
  display.display();
}

void displayReady() {
  startScreen("Colour matcher");
  display.println("Ready");
  display.println();
  display.println("SCAN: capture");
  display.println("SAVE: keep colour");
  display.println("CLEAR: undo");
  finishScreen();
}

void displayScanning() {
  startScreen("Scanning...");
  display.printf("Samples: %u/%u\n",
    static_cast<unsigned int>(samplesCollected),
    static_cast<unsigned int>(SCAN_SAMPLE_COUNT));
  finishScreen();
}

void displayCurrentReading() {
  const CalibratedReading calibrated = calibrateReading(currentReading);

  startScreen("Averaged scan");
  display.printf("RGB: %d %d %d\n",
    roundedChannel(calibrated.red),
    roundedChannel(calibrated.green),
    roundedChannel(calibrated.blue));
  display.printf("Light: %d\n", roundedChannel(calibrated.clear));
  display.println();
  display.println("SAVE: keep");
  display.println("SCAN: redo");
  finishScreen();
}

void displayColourASaved() {
  const CalibratedReading calibrated = calibrateReading(savedColourA);

  startScreen("Colour A saved");
  display.printf("RGB: %d %d %d\n",
    roundedChannel(calibrated.red),
    roundedChannel(calibrated.green),
    roundedChannel(calibrated.blue));
  display.printf("Light: %d\n", roundedChannel(calibrated.clear));
  display.println();
  display.println("Scan colour B");
  finishScreen();
}

void displayPairSaved() {
  const CalibratedReading calibratedA = calibrateReading(savedColourA);
  const CalibratedReading calibratedB = calibrateReading(savedColourB);
  const MatchResult result = compareReadings(savedColourA, savedColourB);

  startScreen("Comparison");
  display.printf("A: %d %d %d\n",
    roundedChannel(calibratedA.red),
    roundedChannel(calibratedA.green),
    roundedChannel(calibratedA.blue));
  display.printf("B: %d %d %d\n",
    roundedChannel(calibratedB.red),
    roundedChannel(calibratedB.green),
    roundedChannel(calibratedB.blue));
  display.println();
  display.println(result.label);
  display.printf("Score: %d%%\n", result.score);
  finishScreen();
}

bool buttonPressed(Button& button) {
  const bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.lastChangeAt = millis();
  }

  if (millis() - button.lastChangeAt < BUTTON_DEBOUNCE_MS) return false;
  if (reading == button.stableState) return false;

  button.stableState = reading;
  return button.stableState == LOW;
}

void beginScan() {
  if (scanInProgress) return;

  scanInProgress = true;
  samplesCollected = 0;
  redTotal = 0;
  greenTotal = 0;
  blueTotal = 0;
  clearTotal = 0;
  currentReadingAvailable = false;

  // A new scan replaces colour B while keeping colour A as the reference.
  if (colourBSaved) colourBSaved = false;

  displayScanning();
  Serial.println("Scan started. Averaging 10 readings...");
}

void saveCurrentReading() {
  if (!currentReadingAvailable) {
    Serial.println("Nothing to save. Press Scan first.");
    return;
  }

  if (!colourASaved) {
    savedColourA = currentReading;
    colourASaved = true;
    currentReadingAvailable = false;
    displayColourASaved();
    Serial.println("Colour A saved.");
    return;
  }

  savedColourB = currentReading;
  colourBSaved = true;
  currentReadingAvailable = false;
  displayPairSaved();

  const MatchResult result = compareReadings(savedColourA, savedColourB);
  Serial.printf(
    "Colour B saved. Result: %s (%d%%). ",
    result.label,
    result.score
  );
  Serial.printf(
    "Colour distance: %.4f, brightness difference: %.1f%%\n",
    result.colourDistance,
    result.brightnessDifference * 100.0f
  );
}

void clearNewestReading() {
  if (scanInProgress) {
    scanInProgress = false;
    samplesCollected = 0;
    Serial.println("Scan cancelled.");
  } else if (currentReadingAvailable) {
    currentReadingAvailable = false;
    Serial.println("Unsaved scan cleared.");
  } else if (colourBSaved) {
    colourBSaved = false;
    Serial.println("Colour B cleared; colour A kept.");
  } else if (colourASaved) {
    colourASaved = false;
    Serial.println("Colour A cleared.");
  } else {
    Serial.println("Nothing to clear.");
  }

  if (colourASaved) {
    displayColourASaved();
  } else {
    displayReady();
  }
}

void updateButtons() {
  if (buttonPressed(scanButton)) beginScan();
  if (buttonPressed(saveButton)) saveCurrentReading();
  if (buttonPressed(clearButton)) clearNewestReading();
}

void scanForI2CDevices() {
  Serial.println("Scanning I2C bus...");

  int devicesFound = 0;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      ++devicesFound;
    }
  }

  if (devicesFound == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.printf("Scan complete: %d device(s) found.\n", devicesFound);
  }
}

void collectScanSample(const ColourReading& reading) {
  if (!scanInProgress) return;

  redTotal += reading.red;
  greenTotal += reading.green;
  blueTotal += reading.blue;
  clearTotal += reading.clear;
  ++samplesCollected;

  displayScanning();

  if (samplesCollected < SCAN_SAMPLE_COUNT) return;

  currentReading = {
    static_cast<uint16_t>(redTotal / SCAN_SAMPLE_COUNT),
    static_cast<uint16_t>(greenTotal / SCAN_SAMPLE_COUNT),
    static_cast<uint16_t>(blueTotal / SCAN_SAMPLE_COUNT),
    static_cast<uint16_t>(clearTotal / SCAN_SAMPLE_COUNT)
  };

  scanInProgress = false;
  currentReadingAvailable = true;
  displayCurrentReading();

  Serial.printf(
    "Averaged R: %u  G: %u  B: %u  Clear: %u\n",
    static_cast<unsigned int>(currentReading.red),
    static_cast<unsigned int>(currentReading.green),
    static_cast<unsigned int>(currentReading.blue),
    static_cast<unsigned int>(currentReading.clear)
  );

  const CalibratedReading calibrated = calibrateReading(currentReading);
  Serial.printf(
    "Provisional RGB: %d  %d  %d  Light: %d\n",
    roundedChannel(calibrated.red),
    roundedChannel(calibrated.green),
    roundedChannel(calibrated.blue),
    roundedChannel(calibrated.clear)
  );
}

void handleByte(uint8_t value) {
  // Discard an incomplete packet after a gap.
  if (packetPosition > 0 && millis() - lastByteAt > 100) {
    packetPosition = 0;
  }
  lastByteAt = millis();

  // A raw packet begins with: 5A 5A 15 08.
  if (packetPosition < 2) {
    if (value == 0x5A) {
      packet[packetPosition++] = value;
    } else {
      packetPosition = 0;
    }
    return;
  }

  if (packetPosition == 2 && value != 0x15) {
    // Preserve repeated header bytes while searching.
    packetPosition = (value == 0x5A) ? 2 : 0;
    return;
  }

  if (packetPosition == 3 && value != 0x08) {
    packetPosition = (value == 0x5A) ? 1 : 0;
    return;
  }

  packet[packetPosition++] = value;
  if (packetPosition < sizeof(packet)) return;
  packetPosition = 0;

  uint8_t checksum = 0;
  for (size_t i = 0; i < sizeof(packet) - 1; ++i) {
    checksum += packet[i];
  }

  if (checksum != packet[12]) {
    Serial.println("Packet checksum failed; skipping.");
    return;
  }

  const ColourReading reading = {
    readChannel(4),
    readChannel(6),
    readChannel(8),
    readChannel(10)
  };

  collectScanSample(reading);
  lastReadingAt = millis();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(SCAN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SAVE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CLEAR_BUTTON_PIN, INPUT_PULLUP);
  delay(10);

  Serial.printf(
    "Initial buttons - Scan GPIO %d: %s, Save GPIO %d: %s, Clear GPIO %d: %s\n",
    SCAN_BUTTON_PIN,
    digitalRead(SCAN_BUTTON_PIN) == LOW ? "pressed" : "released",
    SAVE_BUTTON_PIN,
    digitalRead(SAVE_BUTTON_PIN) == LOW ? "pressed" : "released",
    CLEAR_BUTTON_PIN,
    digitalRead(CLEAR_BUTTON_PIN) == LOW ? "pressed" : "released"
  );

  Wire.begin(OLED_SDA, OLED_SCL);
  scanForI2CDevices();

  // Wire is already configured on GPIO 4 and 5, so periphBegin is false.
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);

  if (oledReady) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    displayReady();
    Serial.println("OLED ready.");
  } else {
    Serial.println("OLED initialization failed.");
  }

  Serial1.begin(9600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
  delay(500);

  sendCommand(0x65);  // White illumination at middle setting
  delay(100);
  sendCommand(0x84);  // Continuously send raw RGBC readings

  Serial.println("GY-33 ready. Use Scan, Save, and Clear.");
  lastReadingAt = millis();
}

void loop() {
  updateButtons();

  while (Serial1.available() > 0) {
    handleByte(static_cast<uint8_t>(Serial1.read()));
  }

  if (millis() - lastReadingAt > 5000) {
    Serial.println("No valid readings in the last 5 seconds.");
    lastReadingAt = millis();
  }
}
