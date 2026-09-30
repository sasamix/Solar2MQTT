#!/usr/bin/env python3
import os
from pathlib import Path

root = Path(os.environ.get("ESP8266_SRC", "esp8266-src"))
version = os.environ.get("ESP8266_BUILD_VERSION", "1.2.0-sasamix.1")

pio = root / "platformio.ini"
text = pio.read_text(encoding="utf-8-sig")
lines = text.splitlines()
for i, line in enumerate(lines):
    if line.strip().startswith("custom_prog_version ="):
        lines[i] = f"custom_prog_version = {version}"
        break
else:
    raise SystemExit("custom_prog_version not found")
pio.write_text("\n".join(lines) + "\n", encoding="utf-8")

main = root / "src" / "main.cpp"
src = main.read_text(encoding="utf-8-sig")

old_response = '''    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError())?"FAIL":"OK");
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    //restartNow = true; // Tell the main loop to restart the ESP
    //RestartTimer = millis();  // Tell the main loop to restart the ESP
    request->send(response); },'''

new_response = '''    String payload;
    int status = 200;
    if (Update.hasError())
    {
      status = 500;
      payload = String("{\\\"success\\\":false,\\\"error\\\":") + String(Update.getError()) +
                ",\\\"freeSketchSpace\\\":" + String(ESP.getFreeSketchSpace()) + "}";
    }
    else
    {
      payload = "{\\\"success\\\":true}";
    }
    AsyncWebServerResponse *response = request->beginResponse(status, "application/json", payload);
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response); },'''

if old_response not in src:
    raise SystemExit("OTA response block not found")
src = src.replace(old_response, new_response, 1)

marker = '''    server.onNotFound([](AsyncWebServerRequest *request)
                      { request->send(418, "text/plain", "418 I'm a teapot"); });'''

endpoint = '''    server.on("/api/system/ota", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(settings.data.httpUser) > 0 && !request->authenticate(settings.data.httpUser, settings.data.httpPass))
                {
                  return request->requestAuthentication();
                }

                const uint32_t flashReal = ESP.getFlashChipRealSize();
                const uint32_t flashConfigured = ESP.getFlashChipSize();
                const uint32_t sketchSize = ESP.getSketchSize();
                const uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
                const uint32_t freeHeap = ESP.getFreeHeap();
                const bool otaReady = freeSketchSpace > sketchSize;

                String json = "{";
                json += "\\\"platform\\\":\\\"ESP8266\\\",";
                json += "\\\"version\\\":\\\"" + String(SOFTWARE_VERSION) + "\\\",";
                json += "\\\"flashReal\\\":" + String(flashReal) + ",";
                json += "\\\"flashConfigured\\\":" + String(flashConfigured) + ",";
                json += "\\\"sketchSize\\\":" + String(sketchSize) + ",";
                json += "\\\"freeSketchSpace\\\":" + String(freeSketchSpace) + ",";
                json += "\\\"freeHeap\\\":" + String(freeHeap) + ",";
                json += "\\\"otaReady\\\":" + String(otaReady ? "true" : "false");
                json += "}";
                request->send(200, "application/json", json);
              });

''' + marker

if marker not in src:
    raise SystemExit("server.onNotFound marker not found")
src = src.replace(marker, endpoint, 1)

main.write_text(src, encoding="utf-8")
print(f"Applied ESP8266 patches, version={version}")
