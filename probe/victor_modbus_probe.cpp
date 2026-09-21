#include <Arduino.h>

namespace {
HardwareSerial inverterSerial(1);

constexpr int RX_PIN = 19;
constexpr int TX_PIN = 22;

// Read-only Modbus RTU probe for Victor NM-ECO-6.2KW PLUS.
// It never sends function 0x05/0x06/0x0F/0x10 (write commands).
const uint32_t BAUD_RATES[] = {2400, 9600};
const uint8_t SLAVE_IDS[] = {5, 1, 2, 3, 4, 6, 7, 8, 9, 10};

struct Probe {
  uint16_t reg;
  uint16_t count;
  const char *name;
};

const Probe PROBES[] = {
    {4501, 14, "PowMr HVM main block"},
    {4530, 1, "PowMr HVM fault"},
    {4535, 11, "PowMr HVM settings"},
    {4556, 6, "PowMr HVM status"},
    {0, 1, "holding register 0"},
    {100, 1, "holding register 100"},
    {200, 1, "holding register 200"},
};

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; ++pos) {
    crc ^= data[pos];
    for (uint8_t i = 0; i < 8; ++i) {
      const bool lsb = crc & 1;
      crc >>= 1;
      if (lsb) crc ^= 0xA001;
    }
  }
  return crc;
}

void drainInput() {
  while (inverterSerial.available()) inverterSerial.read();
}

void printHex(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i + 1 < len) Serial.print(' ');
  }
}

size_t readFrame(uint8_t *buf, size_t capacity, uint32_t timeoutMs) {
  size_t len = 0;
  uint32_t start = millis();
  uint32_t lastByte = start;
  bool gotAny = false;

  while (millis() - start < timeoutMs) {
    while (inverterSerial.available()) {
      const int value = inverterSerial.read();
      if (value >= 0 && len < capacity) buf[len++] = static_cast<uint8_t>(value);
      gotAny = true;
      lastByte = millis();
    }
    if (gotAny && millis() - lastByte > 30) break;
    delay(1);
  }
  return len;
}

bool readHolding(uint8_t slave, uint16_t reg, uint16_t count) {
  uint8_t request[8] = {
      slave, 0x03,
      static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF),
      static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count & 0xFF),
      0, 0};
  const uint16_t crc = crc16(request, 6);
  request[6] = static_cast<uint8_t>(crc & 0xFF);
  request[7] = static_cast<uint8_t>(crc >> 8);

  drainInput();
  inverterSerial.write(request, sizeof(request));
  inverterSerial.flush();

  uint8_t response[96] = {};
  const size_t len = readFrame(response, sizeof(response), 700);
  if (len == 0) return false;

  Serial.printf("  RX (%u): ", static_cast<unsigned>(len));
  printHex(response, len);

  if (len < 5) {
    Serial.println("  [short/non-Modbus]");
    return false;
  }

  const uint16_t receivedCrc =
      static_cast<uint16_t>(response[len - 2]) |
      (static_cast<uint16_t>(response[len - 1]) << 8);
  const uint16_t calculatedCrc = crc16(response, len - 2);
  const bool crcOk = receivedCrc == calculatedCrc;
  Serial.printf("  [%s]", crcOk ? "CRC OK" : "CRC BAD");

  if (crcOk && response[0] == slave && response[1] == 0x03) {
    Serial.println("  <-- VALID MODBUS");
    return true;
  }
  if (crcOk && response[0] == slave && response[1] == 0x83) {
    Serial.printf("  exception=0x%02X\n", response[2]);
    return true; // Valid Modbus device, register may simply be unsupported.
  }

  Serial.println();
  return false;
}

void runProbe() {
  Serial.println();
  Serial.println("=== Victor read-only Modbus RTU probe ===");
  Serial.println("Only function 0x03 (Read Holding Registers) is transmitted.");
  Serial.println("No inverter settings are written.");
  Serial.printf("UART RX=%d TX=%d\n", RX_PIN, TX_PIN);

  bool anyReply = false;
  for (uint32_t baud : BAUD_RATES) {
    Serial.printf("\n--- baud %lu ---\n", static_cast<unsigned long>(baud));
    inverterSerial.end();
    delay(100);
    inverterSerial.begin(baud, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(250);

    for (uint8_t slave : SLAVE_IDS) {
      bool slaveResponded = false;
      for (const Probe &probe : PROBES) {
        Serial.printf("TX slave=%u fn=03 reg=%u count=%u (%s)\n",
                      slave, probe.reg, probe.count, probe.name);
        if (readHolding(slave, probe.reg, probe.count)) {
          anyReply = true;
          slaveResponded = true;
        }
        delay(120);
      }
      if (slaveResponded) {
        Serial.printf("*** Modbus response detected at baud=%lu slave=%u ***\n",
                      static_cast<unsigned long>(baud), slave);
      }
    }
  }

  Serial.println();
  Serial.println(anyReply ? "=== DONE: at least one valid Modbus response found ==="
                          : "=== DONE: no valid Modbus response on tested combinations ===");
  Serial.println("Reset the ATOM to run the probe again.");
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  runProbe();
}

void loop() {
  delay(1000);
}
