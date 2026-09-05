# Fabric Colour Sensor

An ESP32-S3 colour sensor wand for reading fabric colours and helping match materials.

## The build

I'm building a handheld scanner around a GY-33 TCS34725 colour sensor. The planned interface uses a button to take a reading and a small OLED to show the result.

| Component | Purpose |
| --- | --- |
| ESP32-S3 | Controller |
| GY-33 TCS34725 | Colour sensor with a white LED |
| 0.96-inch I2C OLED | Display readings |
| Momentary button | Trigger a scan |
| Printed body and sensor shroud | Hold the sensor at a consistent distance and limit ambient light |

## Build progress

This is a work in progress. The build starts with a USB-powered breadboard prototype, followed by the display, scan button, sensor shroud, calibration, and enclosure.

The planned tests compare close fabric colours such as black and navy, as well as matte and shiny materials. Firmware, wiring instructions, and print files are not published in this repository yet.

## Follow the project

- [Hardware projects on my portfolio](https://tikitatech.xyz/hardware-projects/)
- [Build videos on YouTube](https://www.youtube.com/@tikitatech)

## Licence

See [LICENSE](LICENSE).
