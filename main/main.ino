#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

constexpr int SENSOR_RX = 9;  // Connected to GY-33 CT
constexpr int SENSOR_TX = 8;  // Connected to GY-33 DR
constexpr int OLED_SDA = 4;
constexpr int OLED_SCL = 5;
constexpr int BUTTON_PIN = 7;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_RESET = -1;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 30;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

uint8_t packet[13];
size_t packetPosition = 0;
unsigned long lastByteAt = 0;
unsigned long lastReadingAt = 0;
unsigned long lastButtonChangeAt = 0;
bool lastButtonReading = HIGH;
bool buttonState = HIGH;
bool captureRequested = false;

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

void displayReading(uint16_t red, uint16_t green, uint16_t blue, uint16_t clear) {
  if (!oledReady) return;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Captured RGBC");
  display.println();
  display.printf("R: %u\n", static_cast<unsigned int>(red));
  display.printf("G: %u\n", static_cast<unsigned int>(green));
  display.printf("B: %u\n", static_cast<unsigned int>(blue));
  display.printf("C: %u\n", static_cast<unsigned int>(clear));
  display.println();
  display.print("Press to scan again");
  display.display();
}

void displayScanning() {
  if (!oledReady) return;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Colour sensor");
  display.println();
  display.println("Scanning...");
  display.display();
}

void updateButton() {
  const bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    lastButtonChangeAt = millis();
  }

  if (millis() - lastButtonChangeAt < BUTTON_DEBOUNCE_MS) return;
  if (reading == buttonState) return;

  buttonState = reading;

  if (buttonState == LOW) {
    captureRequested = true;
    displayScanning();
    Serial.println("Scan requested.");
  }
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

  const uint16_t red = readChannel(4);
  const uint16_t green = readChannel(6);
  const uint16_t blue = readChannel(8);
  const uint16_t clear = readChannel(10);

  /*Serial.printf(
    "R: %u  G: %u  B: %u  Clear: %u\n",
    static_cast<unsigned int>(red),
    static_cast<unsigned int>(green),
    static_cast<unsigned int>(blue),
    static_cast<unsigned int>(clear)
  );*/

  if (captureRequested) {
    captureRequested = false;
    displayReading(red, green, blue, clear);
    Serial.printf(
      "Captured R: %u  G: %u  B: %u  Clear: %u\n",
      static_cast<unsigned int>(red),
      static_cast<unsigned int>(green),
      static_cast<unsigned int>(blue),
      static_cast<unsigned int>(clear)
    );
  }

  lastReadingAt = millis();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  scanForI2CDevices();

  // Wire is already configured on GPIO 4 and 5, so periphBegin is false.
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);

  if (oledReady) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Colour sensor");
    display.println();
    display.println("Ready to scan");
    display.println("Press button");
    display.display();
    Serial.println("OLED ready.");
  } else {
    Serial.println("OLED initialization failed.");
  }

  Serial1.begin(9600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
  delay(500);

  sendCommand(0x65);  // White illumination at middle setting
  delay(100);
  sendCommand(0x84);  // Continuously send raw RGBC readings

  Serial.println("GY-33 test started. Waiting for readings...");
  lastReadingAt = millis();
}

void loop() {
  updateButton();

  while (Serial1.available() > 0) {
    handleByte(static_cast<uint8_t>(Serial1.read()));
  }

  if (millis() - lastReadingAt > 5000) {
    Serial.println("No valid readings in the last 5 seconds.");
    lastReadingAt = millis();
  }
}
