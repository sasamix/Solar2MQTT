#include "core/WiFiManager.h"

#include "pins.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <ctype.h>

#if HAS_LAN
#include <ETH.h>
#endif

#include "core/LogSerial.h"
#include "core/SettingsPrefs.h"

extern Settings _settings;

namespace
{
DNSServer dnsServer;
IPAddress apIp(192, 168, 4, 1);

constexpr unsigned long kNetworkCheckIntervalMs = 2000UL;
constexpr unsigned long kStaReconnectGraceMs = 30000UL;
constexpr unsigned long kReconnectKickIntervalMs = 30000UL;

#if HAS_LAN
bool s_ethConnected = false;

const char *currentNetworkHostName()
{
    static char host[64];
    const char *source = _settings.get.deviceName();
    if (!source || !*source)
    {
        source = SOURCE_NAME;
    }

    size_t index = 0;
    bool lastDash = false;
    while (*source && index < sizeof(host) - 1)
    {
        const unsigned char c = static_cast<unsigned char>(*source++);
        if (isalnum(c))
        {
            host[index++] = static_cast<char>(tolower(c));
            lastDash = false;
        }
        else if (!lastDash && index > 0)
        {
            host[index++] = '-';
            lastDash = true;
        }
    }

    while (index > 0 && host[index - 1] == '-')
    {
        index--;
    }

    if (index == 0)
    {
        strcpy(host, "solar2mqtt");
        return host;
    }

    host[index] = '\0';
    return host;
}

void onNetworkEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_ETH_START:
        LogSerial.println(F("[Network] ETH started"));
        ETH.setHostname(currentNetworkHostName());
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        LogSerial.println(F("[Network] ETH connected (PHY link)"));
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        s_ethConnected = true;
        LogSerial.printf("[Network] ETH got IP: %s\n", ETH.localIP().toString().c_str());
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        s_ethConnected = false;
        LogSerial.println(F("[Network] ETH disconnected"));
        break;
    case ARDUINO_EVENT_ETH_STOP:
        s_ethConnected = false;
        LogSerial.println(F("[Network] ETH stopped"));
        break;
    default:
        break;
    }
}

String resolveEthIpString()
{
    const IPAddress ethIp = ETH.localIP();
    if (ETH.linkUp() && ethIp != IPAddress(0, 0, 0, 0))
    {
        return ethIp.toString();
    }

    return "";
}
#endif

int hexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

bool parseBssid(const char *value, uint8_t out[6])
{
    if (!value || !*value || !out)
    {
        return false;
    }

    size_t pos = 0;
    for (int i = 0; i < 6; ++i)
    {
        const int hi = hexNibble(value[pos]);
        const int lo = hexNibble(value[pos + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }

        out[i] = static_cast<uint8_t>((hi << 4) | lo);
        pos += 2;

        if (i < 5)
        {
            if (value[pos] != ':')
            {
                return false;
            }
            pos++;
        }
    }

    return value[pos] == '\0';
}
} // namespace

WiFiManager::WiFiManager(AsyncWebServer &server)
    : _server(server),
      _isApMode(false),
      _ethActive(false)
{
}

void WiFiManager::begin()
{
#if HAS_LAN
    WiFi.onEvent(onNetworkEvent);
#endif

    WiFi.setAutoReconnect(true);
    applySavedNetworkConfig();
    refreshMdns();
}

void WiFiManager::loop()
{
    if (_isApMode)
    {
        dnsServer.processNextRequest();
    }

    static unsigned long lastCheck = 0;
    const unsigned long now = millis();
    if (static_cast<unsigned long>(now - lastCheck) < kNetworkCheckIntervalMs)
    {
        return;
    }
    lastCheck = now;

    if (isEthActive())
    {
        if (_isApMode)
        {
            LogSerial.println(F("[Network] Ethernet active, stopping recovery AP"));
            dnsServer.stop();
            WiFi.softAPdisconnect(false);
            WiFi.mode(WIFI_STA);
            _isApMode = false;
        }
        _disconnectedSinceMs = 0;
        _lastReconnectKickMs = 0;
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        if (_isApMode)
        {
            LogSerial.printf("[Network] STA restored, IP: %s; stopping recovery AP\n",
                             WiFi.localIP().toString().c_str());
            dnsServer.stop();
            WiFi.softAPdisconnect(false);
            WiFi.mode(WIFI_STA);
            _isApMode = false;
        }

        _disconnectedSinceMs = 0;
        _lastReconnectKickMs = 0;
        return;
    }

    // Do not touch a transiently disconnected mesh client. The ESP32 station
    // auto-reconnect and the mesh controller get a full 30 s to recover.
    if (_disconnectedSinceMs == 0)
    {
        _disconnectedSinceMs = now;
        LogSerial.println(F("[Network] STA link lost; waiting up to 30 s for native auto-reconnect"));
        WiFi.setAutoReconnect(true);
        return;
    }

    if (!_isApMode &&
        static_cast<unsigned long>(now - _disconnectedSinceMs) >= kStaReconnectGraceMs)
    {
        LogSerial.println(F("[Network] STA still offline; starting recovery AP without disconnecting STA"));
        startApMode();
        _isApMode = true;
        _lastReconnectKickMs = now;
        return;
    }

    // While recovery AP is active, leave STA credentials/state intact. A
    // periodic reconnect() is enough; do not scan, roam, disconnect or call
    // WiFi.begin repeatedly.
    if (_isApMode &&
        static_cast<unsigned long>(now - _lastReconnectKickMs) >= kReconnectKickIntervalMs)
    {
        _lastReconnectKickMs = now;
        LogSerial.println(F("[Network] Recovery AP active; nudging STA reconnect"));
        WiFi.setAutoReconnect(true);
        WiFi.reconnect();
    }
}

bool WiFiManager::getConnectionState() const
{
    return isEthActive() || WiFi.status() == WL_CONNECTED;
}

void WiFiManager::reconfigure()
{
    LogSerial.println(F("[Network] Reconfiguring network"));
    dnsServer.stop();
    WiFi.scanDelete();

#if HAS_LAN
    if (_ethActive)
    {
        ETH.end();
        s_ethConnected = false;
        _ethActive = false;
    }
#endif

    // User-requested reconfiguration may replace credentials, but it must not
    // erase the radio/NVS WiFi state or power the radio off.
    WiFi.softAPdisconnect(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_STA);
    _isApMode = false;
    _disconnectedSinceMs = 0;
    _lastReconnectKickMs = 0;
    delay(100);

    applySavedNetworkConfig();
    refreshMdns();
}

void WiFiManager::refreshMdns()
{
    MDNS.end();
    if (MDNS.begin(networkHostName()))
    {
        MDNS.addService("http", "tcp", 80);
        LogSerial.printf("[Network] mDNS started: http://%s.local (IP: %s)\n",
                         networkHostName(),
                         ipAddress().c_str());
    }
}

bool WiFiManager::isInApMode() const
{
    return _isApMode;
}

bool WiFiManager::isEthActive() const
{
#if HAS_LAN
    return _ethActive && s_ethConnected && ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0);
#else
    return false;
#endif
}

bool WiFiManager::hasLanSupport() const
{
#if HAS_LAN
    return true;
#else
    return false;
#endif
}

bool WiFiManager::ethernetEnabled() const
{
#if HAS_LAN
    return _settings.get.ethEnabled();
#else
    return false;
#endif
}

int WiFiManager::rssi() const
{
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

String WiFiManager::ipAddress() const
{
#if HAS_LAN
    const String ethIp = resolveEthIpString();
    if (ethIp.length() > 0)
    {
        return ethIp;
    }
#endif

    if (WiFi.status() == WL_CONNECTED)
    {
        return WiFi.localIP().toString();
    }

    return WiFi.softAPIP().toString();
}

bool WiFiManager::initEthernet()
{
#if !HAS_LAN
    return false;
#else
    if (!_settings.get.ethEnabled())
    {
        LogSerial.println(F("[Network] Ethernet disabled in settings"));
        _ethActive = false;
        return false;
    }

    IPAddress ip;
    IPAddress gw;
    IPAddress sn;
    IPAddress dns;
    const bool useStatic =
        ip.fromString(_settings.get.staticIP()) &&
        gw.fromString(_settings.get.staticGW()) &&
        sn.fromString(_settings.get.staticSN()) &&
        dns.fromString(_settings.get.staticDNS()) &&
        ip != IPAddress(0, 0, 0, 0);

    if (useStatic)
    {
        ETH.config(ip, gw, sn, dns);
        LogSerial.printf("[Network] ETH static IP %s / GW %s / SN %s / DNS %s\n",
                         ip.toString().c_str(),
                         gw.toString().c_str(),
                         sn.toString().c_str(),
                         dns.toString().c_str());
    }
    else
    {
        LogSerial.println(F("[Network] ETH using DHCP"));
    }

    ETH.setHostname(networkHostName());
    s_ethConnected = false;

#if defined(ETH_SPI_W5500) && ETH_SPI_W5500
    LogSerial.println(F("[Network] Initializing Ethernet (W5500 SPI)..."));
    const bool ok = ETH.begin(ETH_PHY_W5500,
                              ETH_PHY_ADDR_AUTO,
                              PIN_ETH_SPI_CS,
                              PIN_ETH_SPI_INT,
                              PIN_ETH_SPI_RST,
                              SPI2_HOST,
                              PIN_ETH_SPI_SCLK,
                              PIN_ETH_SPI_MISO,
                              PIN_ETH_SPI_MOSI);
#else
    LogSerial.println(F("[Network] Initializing Ethernet (LAN8720)..."));
    const bool ok = ETH.begin(ETH_PHY_LAN8720,
                              PIN_ETH_PHY_ADDR,
                              PIN_ETH_MDC,
                              PIN_ETH_MDIO,
                              PIN_ETH_POWER,
                              ETH_CLOCK_GPIO0_IN);
#endif

    if (!ok)
    {
        LogSerial.println(F("[Network] Ethernet init failed, falling back to WiFi"));
        _ethActive = false;
        return false;
    }

    _ethActive = true;

    const uint32_t start = millis();
    while ((millis() - start) < 10000UL)
    {
        if (isEthActive())
        {
            LogSerial.printf("[Network] Ethernet ready, IP: %s\n", ipAddress().c_str());
            return true;
        }
        delay(100);
    }

    LogSerial.println(F("[Network] Ethernet IP timeout, falling back to WiFi"));
    return false;
#endif
}

bool WiFiManager::connectToWifi()
{
    const char *primarySsid = _settings.get.wifiSsid0();
    const char *primaryPass = _settings.get.wifiPassword0();
    const char *fallbackSsid = _settings.get.wifiSsid1();
    const char *fallbackPass = _settings.get.wifiPassword1();

    if ((!primarySsid || !primarySsid[0]) && (!fallbackSsid || !fallbackSsid[0]))
    {
        LogSerial.println(F("[Network] No SSIDs configured"));
        return false;
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.mode(_isApMode ? WIFI_AP_STA : WIFI_STA);
    WiFi.setHostname(networkHostName());
    WiFi.setSleep(false);

    IPAddress ip;
    IPAddress gw;
    IPAddress sn;
    IPAddress dns;
    const bool useStatic =
        ip.fromString(_settings.get.staticIP()) &&
        gw.fromString(_settings.get.staticGW()) &&
        sn.fromString(_settings.get.staticSN()) &&
        dns.fromString(_settings.get.staticDNS()) &&
        ip != IPAddress(0, 0, 0, 0);

    if (useStatic)
    {
        WiFi.config(ip, gw, sn, dns);
    }

    struct Candidate
    {
        const char *ssid;
        const char *password;
        bool primary;
    };

    const Candidate candidates[] = {
        {primarySsid, primaryPass, true},
        {fallbackSsid, fallbackPass, false},
    };

    bool triedFallback = false;
    for (const Candidate &candidate : candidates)
    {
        if (!candidate.ssid || !candidate.ssid[0])
        {
            continue;
        }

        if (!candidate.primary &&
            primarySsid && primarySsid[0] &&
            strcmp(candidate.ssid, primarySsid) == 0 &&
            strcmp(candidate.password ? candidate.password : "",
                   primaryPass ? primaryPass : "") == 0)
        {
            LogSerial.printf("[Network] Skipping duplicate fallback SSID: %s\n", candidate.ssid);
            continue;
        }

        if (!candidate.primary)
        {
            triedFallback = true;
            WiFi.disconnect(false, false);
            delay(250);
        }

        LogSerial.printf("[Network] Trying %s SSID: %s (mesh-managed AP selection)\n",
                         candidate.primary ? "primary" : "fallback",
                         candidate.ssid);

        // No explicit scan and no BSSID pinning here. This is deliberate:
        // mesh systems such as Deco should choose/steer the AP themselves.
        WiFi.begin(candidate.ssid, candidate.password);

        for (int attempt = 0; attempt < 30; ++attempt)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                LogSerial.printf("[Network] WiFi connected via %s, BSSID=%s RSSI=%d dBm IP=%s\n",
                                 candidate.primary ? "primary" : "fallback",
                                 WiFi.BSSIDstr().c_str(),
                                 WiFi.RSSI(),
                                 WiFi.localIP().toString().c_str());
                WiFi.setAutoReconnect(true);
                return true;
            }
            delay(500);
        }

        LogSerial.printf("[Network] %s SSID connection timeout: %s\n",
                         candidate.primary ? "Primary" : "Fallback",
                         candidate.ssid);
    }

    // If a distinct fallback was tried and also failed, return the station to
    // the preferred SSID before enabling recovery AP. Auto-reconnect can then
    // keep trying the preferred mesh without further scans.
    if (triedFallback && primarySsid && primarySsid[0])
    {
        WiFi.disconnect(false, false);
        delay(100);
        LogSerial.printf("[Network] Returning STA reconnect target to primary SSID: %s\n", primarySsid);
        WiFi.begin(primarySsid, primaryPass);
    }

    WiFi.setAutoReconnect(true);
    return false;
}

bool WiFiManager::applySavedNetworkConfig()
{
    bool haveEthernet = false;

#if HAS_LAN
    haveEthernet = initEthernet();
#endif

    if (!haveEthernet && !connectToWifi())
    {
        startApMode();
        _isApMode = true;
        return false;
    }

    _isApMode = false;
    return true;
}

void WiFiManager::startApMode()
{
    // Recovery AP must coexist with STA. Never disconnect or erase the STA
    // here: doing so turns a short mesh interruption into a forced outage.
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
    WiFi.softAP(String(SOURCE_NAME) + "-AP");
    dnsServer.start(53, "*", apIp);
    LogSerial.printf("[Network] Recovery AP started on %s; STA reconnect remains active\n",
                     WiFi.softAPIP().toString().c_str());
}

const char *WiFiManager::networkHostName() const
{
#if HAS_LAN
    return currentNetworkHostName();
#else
    static char host[64];
    const char *source = _settings.get.deviceName();
    if (!source || !*source)
    {
        source = SOURCE_NAME;
    }

    size_t index = 0;
    bool lastDash = false;
    while (*source && index < sizeof(host) - 1)
    {
        const unsigned char c = static_cast<unsigned char>(*source++);
        if (isalnum(c))
        {
            host[index++] = static_cast<char>(tolower(c));
            lastDash = false;
        }
        else if (!lastDash && index > 0)
        {
            host[index++] = '-';
            lastDash = true;
        }
    }

    while (index > 0 && host[index - 1] == '-')
    {
        index--;
    }

    if (index == 0)
    {
        strcpy(host, "solar2mqtt");
        return host;
    }

    host[index] = '\0';
    return host;
#endif
}
