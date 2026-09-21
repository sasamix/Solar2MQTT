#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

namespace {
HardwareSerial inverterSerial(1);
WebServer server(80);
uint32_t lastWifiAttempt = 0;
bool otaServerStarted = false;
bool probeRunning = false;
bool probeCompleted = false;
constexpr size_t LOG_CAPACITY = 16384;
char probeLog[LOG_CAPACITY];
size_t probeLogLen = 0;

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

const char UPDATE_PAGE[] PROGMEM = R"HTML(\n<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">\n<title>Victor Modbus Probe OTA</title></head><body>\n<h2>Victor Modbus Probe</h2>\n<p>Recovery OTA. Select the normal Solar2MQTT firmware.bin.</p>\n<form method="POST" action="/update" enctype="multipart/form-data">\n<input type="file" name="firmware" accept=".bin,application/octet-stream" required>\n<input type="submit" value="Upload firmware"></form></body></html>\n)HTML";

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
void appendLog(const char *s){
  if(!s) return;
  size_t n=strlen(s);
  size_t room=(LOG_CAPACITY-1>probeLogLen)?(LOG_CAPACITY-1-probeLogLen):0;
  if(n>room)n=room;
  if(n){ memcpy(probeLog+probeLogLen,s,n); probeLogLen+=n; probeLog[probeLogLen]='\0'; }
}
void logLine(const char *s){
  Serial.println(s);
  appendLog(s); appendLog("\\n");\n}\nvoid logf(const char *fmt,...){\n  char line[256];\n  va_list ap; va_start(ap,fmt); vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);\n  logLine(line);\n}\nvoid logHex(const uint8_t *data,size_t len){\n  char line[3*96+32]; size_t p=0;\n  p+=snprintf(line+p,sizeof(line)-p,"  RX (%u): ",(unsigned)len);\n  for(size_t i=0;i<len && p+4<sizeof(line);++i) p+=snprintf(line+p,sizeof(line)-p,"%02X%s",data[i],i+1<len?" ":"");\n  logLine(line);\n}\nvoid serviceRecovery(){\n  if(otaServerStarted && WiFi.status()==WL_CONNECTED) server.handleClient();\n}\nvoid drainInput(){ while(inverterSerial.available()) inverterSerial.read(); serviceRecovery(); }\nvoid printHex(const uint8_t *data,size_t len){ for(size_t i=0;i<len;++i){ if(data[i]<0x10)Serial.print('0'); Serial.print(data[i],HEX); if(i+1<len)Serial.print(' '); } }\nsize_t readFrame(uint8_t *buf,size_t capacity,uint32_t timeoutMs){\n  size_t len=0; uint32_t start=millis(),lastByte=start; bool gotAny=false;\n  while(millis()-start<timeoutMs){\n    while(inverterSerial.available()){ int value=inverterSerial.read(); if(value>=0&&len<capacity)buf[len++]=(uint8_t)value; gotAny=true; lastByte=millis(); }\n    if(gotAny&&millis()-lastByte>30)break;\n    serviceRecovery();\n    delay(1);\n  } return len;\n}\nbool readHolding(uint8_t slave,uint16_t reg,uint16_t count){\n  uint8_t request[8]={slave,0x03,(uint8_t)(reg>>8),(uint8_t)reg,(uint8_t)(count>>8),(uint8_t)count,0,0};\n  uint16_t crc=crc16(request,6); request[6]=(uint8_t)crc; request[7]=(uint8_t)(crc>>8);\n  drainInput(); inverterSerial.write(request,sizeof(request)); inverterSerial.flush();\n  uint8_t response[96]={}; size_t len=readFrame(response,sizeof(response),700); if(!len)return false;\n  logHex(response,len);\n  if(len<5){Serial.println("  [short/non-Modbus]");return false;}\n  uint16_t received=(uint16_t)response[len-2]|((uint16_t)response[len-1]<<8);\n  bool ok=received==crc16(response,len-2); Serial.printf("  [%s]",ok?"CRC OK":"CRC BAD");\n  if(ok&&response[0]==slave&&response[1]==0x03){Serial.println("  <-- VALID MODBUS");return true;}\n  if(ok&&response[0]==slave&&response[1]==0x83){Serial.printf("  exception=0x%02X
",response[2]);return true;}\n  Serial.println(); return false;\n}\nvoid runProbe(){\n  probeRunning=true; probeLogLen=0; probeLog[0]='\0';\n  logLine("=== Victor read-only Modbus RTU probe ===");\n  logLine("Only function 0x03 is transmitted. No inverter settings are written.");\n  logf("UART RX=%d TX=%d",RX_PIN,TX_PIN);\n  bool any=false;\n  for(uint32_t baud:BAUD_RATES){\n    Serial.printf("
--- baud %lu ---
",(unsigned long)baud); inverterSerial.end(); delay(100); inverterSerial.begin(baud,SERIAL_8N1,RX_PIN,TX_PIN); delay(250);\n    for(uint8_t slave:SLAVE_IDS){ bool replied=false; for(const Probe &p:PROBES){ Serial.printf("TX slave=%u fn=03 reg=%u count=%u (%s)
",slave,p.reg,p.count,p.name); if(readHolding(slave,p.reg,p.count)){any=true;replied=true;} for(uint32_t waitStart=millis(); millis()-waitStart<120; ){ serviceRecovery(); delay(2); } } if(replied)Serial.printf("*** Modbus response detected at baud=%lu slave=%u ***
",(unsigned long)baud,slave); }\n  }\n  logLine(any?"=== DONE: at least one valid Modbus response found ===":"=== DONE: no valid Modbus response on tested combinations ===");\n  probeRunning=false;\n}\n\nbool tryNetwork(const char *ssidKey, const char *passwordKey){\n  String ssid=readSolarString(ssidKey);\n  String password=readSolarString(passwordKey);\n  if(ssid.isEmpty()) return false;\n  Serial.printf("Connecting to saved Solar2MQTT Wi-Fi: %s
",ssid.c_str());\n  WiFi.disconnect(true, false); delay(200); WiFi.mode(WIFI_STA);\n  WiFi.begin(ssid.c_str(),password.c_str());\n  const uint32_t start=millis();\n  while(WiFi.status()!=WL_CONNECTED && millis()-start<15000){ delay(500); Serial.print('.'); }\n  Serial.println();\n  return WiFi.status()==WL_CONNECTED;\n}\n\nbool connectSavedWiFi(){\n  if(WiFi.status()==WL_CONNECTED) return true;\n  if(!tryNetwork("wifiSsid0","wifiPassword0") && !tryNetwork("wifiSsid1","wifiPassword1")){\n    Serial.println("Wi-Fi unavailable; recovery will retry automatically.");\n    return false;\n  }\n  Serial.printf("Wi-Fi connected. IP: %s
",WiFi.localIP().toString().c_str());\n  if(MDNS.begin("victor-probe")) Serial.println("OTA page: http://victor-probe.local/");\n  Serial.printf("OTA page: http://%s/
",WiFi.localIP().toString().c_str());\n  return true;\n}\n\nvoid startRecoveryOta(){\n  if(otaServerStarted || !connectSavedWiFi()) return;\n  server.on("/",HTTP_GET,[](){\n    String page=F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Victor Modbus Probe</title></head><body><h2>Victor Modbus Probe</h2>");\n    page += "<p>Status: <b>" + String(probeRunning?"RUNNING":"IDLE / COMPLETE") + "</b></p>";\n    page += F("<form method='POST' action='/rerun'><button type='submit'>Run Modbus probe again</button></form><h3>Last probe log</h3><pre style='white-space:pre-wrap;word-break:break-word;border:1px solid #aaa;padding:10px;max-height:60vh;overflow:auto'>");\n    page += probeLog;\n    page += F("</pre><p><a href='/log'>Open raw log</a></p><hr><h3>Recovery OTA</h3><p>Select Solar2MQTT firmware.bin to return to normal firmware.</p><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin,.ota,application/octet-stream' required><input type='submit' value='Upload firmware'></form></body></html>");\n    server.send(200,"text/html",page);\n  });\n  server.on("/log",HTTP_GET,[](){server.send(200,"text/plain",probeLog);});\n  server.on("/rerun",HTTP_POST,[](){\n    if(probeRunning){ server.send(409,"text/plain","Probe already running"); return; }\n    server.sendHeader("Location","/"); server.send(303,"text/plain","");\n    delay(100); runProbe();\n  });\n  server.on("/update",HTTP_POST,\n    [](){bool ok=!Update.hasError();server.send(200,"text/plain",ok?"Update successful. Rebooting...":"Update FAILED. Check serial log.");delay(500);if(ok)ESP.restart();},\n    [](){HTTPUpload &u=server.upload(); if(u.status==UPLOAD_FILE_START){Serial.printf("OTA start: %s
",u.filename.c_str());if(!Update.begin(UPDATE_SIZE_UNKNOWN))Update.printError(Serial);}\n      else if(u.status==UPLOAD_FILE_WRITE){if(Update.write(u.buf,u.currentSize)!=u.currentSize)Update.printError(Serial);}\n      else if(u.status==UPLOAD_FILE_END){if(Update.end(true))Serial.printf("OTA success: %u bytes
",u.totalSize);else Update.printError(Serial);}\n      else if(u.status==UPLOAD_FILE_ABORTED){Update.abort();Serial.println("OTA aborted");}});\n  server.begin(); otaServerStarted=true; Serial.println("Recovery OTA web server started.");\n}\n}\n\nvoid setup(){\n  Serial.begin(115200);\n  delay(1500);\n\n  // Recovery first: get the device back on the saved Solar2MQTT Wi-Fi\n  // and expose OTA before touching the inverter.\n  startRecoveryOta();\n\n  // Then run the read-only Modbus sweep. The web server is already bound;\n  // loop() will service it as soon as the sweep completes.\n  runProbe();\n}\nvoid loop(){\n  if(WiFi.status()==WL_CONNECTED){\n    if(!otaServerStarted) startRecoveryOta();\n    if(otaServerStarted) server.handleClient();\n  } else if(millis()-lastWifiAttempt>=30000){\n    lastWifiAttempt=millis();\n    startRecoveryOta();\n  }\n  delay(2);\n}\n