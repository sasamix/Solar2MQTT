#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>

namespace {
HardwareSerial inverterSerial(1);
WebServer server(80);\nuint32_t lastWifiAttempt = 0;\nbool otaServerStarted = false;

constexpr int RX_PIN = 19;
constexpr int TX_PIN = 22;

const uint32_t BAUD_RATES[] = {2400, 9600};
const uint8_t SLAVE_IDS[] = {5, 1, 2, 3, 4, 6, 7, 8, 9, 10};

struct Probe { uint16_t reg; uint16_t count; const char *name; };
const Probe PROBES[] = {
    {4501, 14, "PowMr HVM main block"}, {4530, 1, "PowMr HVM fault"},
    {4535, 11, "PowMr HVM settings"}, {4556, 6, "PowMr HVM status"},
    {0, 1, "holding register 0"}, {100, 1, "holding register 100"},
    {200, 1, "holding register 200"},
};

const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Victor Modbus Probe OTA</title></head><body>
<h2>Victor Modbus Probe</h2>
<p>Recovery OTA. Select the normal Solar2MQTT firmware.bin.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin,application/octet-stream" required>
<input type="submit" value="Upload firmware"></form></body></html>
)HTML";

uint32_t fnv1a(const char *group, const char *name) {
  uint32_t hash = 2166136261u;
  if (group) for (const uint8_t *p=(const uint8_t*)group; *p; ++p) { hash ^= *p; hash *= 16777619u; }
  hash ^= (uint8_t)'/'; hash *= 16777619u;
  if (name) for (const uint8_t *p=(const uint8_t*)name; *p; ++p) { hash ^= *p; hash *= 16777619u; }
  return hash;
}

void makeNvsKey(const char *group, const char *name, char out[16]) {
  const size_t n = strlen(name);
  if (n <= 15) { strncpy(out, name, 15); out[15]='\0'; return; }
  memset(out, 0, 16);
  for (size_t i=0; i<6 && name[i]; ++i) out[i]=name[i];
  out[6]='_';
  const uint32_t hash=fnv1a(group,name);
  const char hex[]="0123456789ABCDEF";
  for (int i=0;i<8;++i) out[7+i]=hex[(hash>>(28-4*i))&0x0F];
  out[15]='\0';
}

String readSolarString(const char *name) {
  char key[16]; makeNvsKey("network", name, key);
  Preferences prefs;
  if (!prefs.begin("network", true)) return String();
  String value=prefs.getString(key, "");
  prefs.end();
  return value;
}

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc=0xFFFF;
  for(size_t pos=0;pos<len;++pos){ crc^=data[pos]; for(uint8_t i=0;i<8;++i){ bool lsb=crc&1; crc>>=1; if(lsb) crc^=0xA001; } }
  return crc;
}
void drainInput(){ while(inverterSerial.available()) inverterSerial.read(); }
void printHex(const uint8_t *data,size_t len){ for(size_t i=0;i<len;++i){ if(data[i]<0x10)Serial.print('0'); Serial.print(data[i],HEX); if(i+1<len)Serial.print(' '); } }
size_t readFrame(uint8_t *buf,size_t capacity,uint32_t timeoutMs){
  size_t len=0; uint32_t start=millis(),lastByte=start; bool gotAny=false;
  while(millis()-start<timeoutMs){ while(inverterSerial.available()){ int value=inverterSerial.read(); if(value>=0&&len<capacity)buf[len++]=(uint8_t)value; gotAny=true; lastByte=millis(); } if(gotAny&&millis()-lastByte>30)break; delay(1); } return len;
}
bool readHolding(uint8_t slave,uint16_t reg,uint16_t count){
  uint8_t request[8]={slave,0x03,(uint8_t)(reg>>8),(uint8_t)reg,(uint8_t)(count>>8),(uint8_t)count,0,0};
  uint16_t crc=crc16(request,6); request[6]=(uint8_t)crc; request[7]=(uint8_t)(crc>>8);
  drainInput(); inverterSerial.write(request,sizeof(request)); inverterSerial.flush();
  uint8_t response[96]={}; size_t len=readFrame(response,sizeof(response),700); if(!len)return false;
  Serial.printf("  RX (%u): ",(unsigned)len); printHex(response,len);
  if(len<5){Serial.println("  [short/non-Modbus]");return false;}
  uint16_t received=(uint16_t)response[len-2]|((uint16_t)response[len-1]<<8);
  bool ok=received==crc16(response,len-2); Serial.printf("  [%s]",ok?"CRC OK":"CRC BAD");
  if(ok&&response[0]==slave&&response[1]==0x03){Serial.println("  <-- VALID MODBUS");return true;}
  if(ok&&response[0]==slave&&response[1]==0x83){Serial.printf("  exception=0x%02X\n",response[2]);return true;}
  Serial.println(); return false;
}
void runProbe(){
  Serial.println("\n=== Victor read-only Modbus RTU probe ===");
  Serial.println("Only function 0x03 is transmitted. No inverter settings are written.");
  Serial.printf("UART RX=%d TX=%d\n",RX_PIN,TX_PIN);
  bool any=false;
  for(uint32_t baud:BAUD_RATES){
    Serial.printf("\n--- baud %lu ---\n",(unsigned long)baud); inverterSerial.end(); delay(100); inverterSerial.begin(baud,SERIAL_8N1,RX_PIN,TX_PIN); delay(250);
    for(uint8_t slave:SLAVE_IDS){ bool replied=false; for(const Probe &p:PROBES){ Serial.printf("TX slave=%u fn=03 reg=%u count=%u (%s)\n",slave,p.reg,p.count,p.name); if(readHolding(slave,p.reg,p.count)){any=true;replied=true;} delay(120); } if(replied)Serial.printf("*** Modbus response detected at baud=%lu slave=%u ***\n",(unsigned long)baud,slave); }
  }
  Serial.println(any?"\n=== DONE: at least one valid Modbus response found ===":"\n=== DONE: no valid Modbus response on tested combinations ===");
}

bool tryNetwork(const char *ssidKey, const char *passwordKey){
  String ssid=readSolarString(ssidKey);
  String password=readSolarString(passwordKey);
  if(ssid.isEmpty()) return false;
  Serial.printf("Connecting to saved Solar2MQTT Wi-Fi: %s\n",ssid.c_str());
  WiFi.disconnect(true, false); delay(200); WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(),password.c_str());
  const uint32_t start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<15000){ delay(500); Serial.print('.'); }
  Serial.println();
  return WiFi.status()==WL_CONNECTED;
}

bool connectSavedWiFi(){
  if(WiFi.status()==WL_CONNECTED) return true;
  if(!tryNetwork("wifiSsid0","wifiPassword0") && !tryNetwork("wifiSsid1","wifiPassword1")){
    Serial.println("Wi-Fi unavailable; recovery will retry automatically.");
    return false;
  }
  Serial.printf("Wi-Fi connected. IP: %s\n",WiFi.localIP().toString().c_str());
  if(MDNS.begin("victor-probe")) Serial.println("OTA page: http://victor-probe.local/");
  Serial.printf("OTA page: http://%s/\n",WiFi.localIP().toString().c_str());
  return true;
}

void startRecoveryOta(){
  if(otaServerStarted || !connectSavedWiFi()) return;
  server.on("/",HTTP_GET,[](){server.send_P(200,"text/html",UPDATE_PAGE);});
  server.on("/update",HTTP_POST,
    [](){bool ok=!Update.hasError();server.send(200,"text/plain",ok?"Update successful. Rebooting...":"Update FAILED. Check serial log.");delay(500);if(ok)ESP.restart();},
    [](){HTTPUpload &u=server.upload(); if(u.status==UPLOAD_FILE_START){Serial.printf("OTA start: %s\n",u.filename.c_str());if(!Update.begin(UPDATE_SIZE_UNKNOWN))Update.printError(Serial);}
      else if(u.status==UPLOAD_FILE_WRITE){if(Update.write(u.buf,u.currentSize)!=u.currentSize)Update.printError(Serial);}
      else if(u.status==UPLOAD_FILE_END){if(Update.end(true))Serial.printf("OTA success: %u bytes\n",u.totalSize);else Update.printError(Serial);}
      else if(u.status==UPLOAD_FILE_ABORTED){Update.abort();Serial.println("OTA aborted");}});
  server.begin(); otaServerStarted=true; Serial.println("Recovery OTA web server started.");
}
}

void setup(){ Serial.begin(115200); delay(1500); runProbe(); startRecoveryOta(); }
void loop(){
  if(WiFi.status()==WL_CONNECTED){
    if(!otaServerStarted) startRecoveryOta();
    if(otaServerStarted) server.handleClient();
  } else if(millis()-lastWifiAttempt>=30000){
    lastWifiAttempt=millis();
    startRecoveryOta();
  }
  delay(2);
}
