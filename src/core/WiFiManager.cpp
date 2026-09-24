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
constexpr unsigned long kStaReconnectGraceMs = 12000UL;
constexpr unsigned long kReconnectFastIntervalMs = 15000UL;
constexpr unsigned long kReconnectSlowIntervalMs = 60000UL;
constexpr uint8_t kReconnectFastAttempts = 20;
constexpr unsigned long kRoamCheckIntervalMs = 300000UL;
constexpr unsigned long kPrimaryRecoveryCheckIntervalMs = 60000UL;
constexpr int kRoamPoorRssiDbm = -72;
constexpr int kRoamImprovementDb = 10;

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
    if ((now - lastCheck) < kNetworkCheckIntervalMs)
    {
        return;
    }
    lastCheck = now;

    if (isEthActive())
    {
        if (_isApMode)
        {
            LogSerial.println(F("[Network] Ethernet active, stopping AP mode"));
            dnsServer.stop();
            WiFi.softAPdisconnect(false);
            WiFi.mode(WIFI_STA);
            _isApMode = false;
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        if (_isApMode)
        {
            LogSerial.printf("[Network] STA restored, IP: %s; stopping AP mode\n",
                             WiFi.localIP().toString().c_str());
            dnsServer.stop();
            WiFi.softAPdisconnect(false);
            WiFi.mode(WIFI_STA);
            _isApMode = false;
        }
        _lastReconnectAttemptMs = 0;
        _disconnectedSinceMs = 0;
        _reconnectFailures = 0;
        // Do not actively roam a healthy mesh connection. Deco and the ESP32
        // station stack handle association; forced scans/BSSID switches make
        // brief RF dips look like full disconnects.
        return;
    }

    if (!_isApMode)
    {
        if (_disconnectedSinceMs == 0)
        {
            _disconnectedSinceMs = now;
            LogSerial.println(F("[Network] STA link lost; waiting for native mesh auto-reconnect"));
            WiFi.setAutoReconnect(true);
            return;
        }

        if (static_cast<unsigned long>(now - _disconnectedSinceMs) < kStaReconnectGraceMs)
        {
            return;
        }

        LogSerial.println(F("[Network] STA did not recover within 12 s; starting recovery AP"));
        startApMode();
        _isApMode = true;
        _lastReconnectAttemptMs = 0;
    }

    const unsigned long reconnectInterval =
        (_reconnectFailures < kReconnectFastAttempts)
            ? kReconnectFastIntervalMs
            : kReconnectSlowIntervalMs;

    if (_lastReconnectAttemptMs != 0 &&
        static_cast<unsigned long>(now - _lastReconnectAttemptMs) < reconnectInterval)
    {
        return;
    }

    _lastReconnectAttemptMs = now;
    LogSerial.println(F("[Network] Reconnect attempt: trying saved WiFi networks"));

    if (connectToWifi())
    {
        LogSerial.printf("[Network] Reconnected to STA, IP: %s\n",
                         WiFi.localIP().toString().c_str());
        dnsServer.stop();
        WiFi.softAPdisconnect(false);
        WiFi.mode(WIFI_STA);
        _isApMode = false;
        _lastReconnectAttemptMs = 0;
        _disconnectedSinceMs = 0;
        _reconnectFailures = 0;
        refreshMdns();
    }
    else
    {
        if (_reconnectFailures < 255)
            _reconnectFailures++;
        LogSerial.printf("[Network] Reconnect failed; AP stays active, retry in %lu s\n",
                         static_cast<unsigned long>(
                             (_reconnectFailures < kReconnectFastAttempts)
                                 ? kReconnectFastIntervalMs / 1000UL
                                 : kReconnectSlowIntervalMs / 1000UL));
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

    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    _isApMode = false;
    _lastReconnectAttemptMs = 0;
    _lastRoamCheckMs = 0;
    _disconnectedSinceMs = 0;
    _reconnectFailures = 0;
    delay(50);

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
    if (strlen(_settings.get.wifiSsid0()) == 0 && strlen(_settings.get.wifiSsid1()) == 0)
    {
        LogSerial.println(F("[Network] No SSIDs configured"));
        return false;
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(_isApMode ? WIFI_AP_STA : WIFI_STA);
    WiFi.setHostname(networkHostName());
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
    delay(150);

    struct Candidate
    {
        const char *ssid;
        const char *password;
        const char *bssid;
        bool primary;
    };

    const Candidate candidates[] = {
        {_settings.get.wifiSsid0(), _settings.get.wifiPassword0(), _settings.get.wifiBssid0(), true},
        {_settings.get.wifiSsid1(), _settings.get.wifiPassword1(), _settings.get.wifiBssid1(), false},
    };

    for (const Candidate &candidate : candidates)
    {
        if (!candidate.ssid || !candidate.ssid[0])
        {
            continue;
        }

        if (!candidate.primary &&
            strcmp(candidate.ssid, _settings.get.wifiSsid0()) == 0 &&
            strcmp(candidate.password ? candidate.password : "",
                   _settings.get.wifiPassword0() ? _settings.get.wifiPassword0() : "") == 0)
        {
            LogSerial.printf("[Network] Skipping duplicate fallback SSID: %s\n", candidate.ssid);
            continue;
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
            WiFi.config(ip, gw, sn, dns);
        }

        const bool lockRequested = candidate.primary && _settings.get.wifiBssidLock();

        if (lockRequested)
        {
            uint8_t lockedBssid[6] = {};
            if (!parseBssid(candidate.bssid, lockedBssid))
            {
                LogSerial.println(F("[Network] Primary BSSID lock is enabled but stored BSSID is invalid"));
                continue;
            }

            // Only a manual BSSID lock needs a scan. Normal mesh operation
            // must connect directly by SSID so Deco/Orbi/etc. can select the
            // appropriate AP without a blocking full-network scan.
            const int networkCount = WiFi.scanNetworks(false, false);
            int lockedIndex = -1;
            if (networkCount > 0)
            {
                for (int i = 0; i < networkCount; ++i)
                {
                    const uint8_t *scanBssid = WiFi.BSSID(i);
                    if (WiFi.SSID(i) == candidate.ssid &&
                        scanBssid != nullptr &&
                        memcmp(scanBssid, lockedBssid, 6) == 0)
                    {
                        lockedIndex = i;
                        break;
                    }
                }
            }

            if (lockedIndex < 0)
            {
                LogSerial.printf("[Network] Locked BSSID not found for SSID: %s\n", candidate.ssid);
                WiFi.scanDelete();
                continue;
            }

            const int32_t channel = WiFi.channel(lockedIndex);
            const uint8_t *bestBssid = WiFi.BSSID(lockedIndex);
            LogSerial.printf("[Network] Trying primary SSID with BSSID lock: %s BSSID=%s channel=%ld\n",
                             candidate.ssid,
                             WiFi.BSSIDstr(lockedIndex).c_str(),
                             static_cast<long>(channel));

            WiFi.begin(candidate.ssid,
                       candidate.password,
                       channel > 0 ? channel : 0,
                       bestBssid,
                       true);
            WiFi.scanDelete();
        }
        else
        {
            LogSerial.printf("[Network] Trying %s SSID: %s (automatic mesh AP selection)\n",
                             candidate.primary ? "primary" : "fallback",
                             candidate.ssid);
            WiFi.begin(candidate.ssid, candidate.password);
        }

        // Direct SSID association normally completes quickly. Keep enough
        // room for DHCP without making fallback/recovery feel stalled.
        for (int attempt = 0; attempt < 20; ++attempt)
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

        LogSerial.printf("[Network] %s SSID connection failed: %s\n",
                         candidate.primary ? "Primary" : "Fallback",
                         candidate.ssid);

        WiFi.disconnect(false, false);
        delay(250);
    }

    return false;
}

bool WiFiManager::maybeRoamToBetterAp()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    const unsigned long now = millis();
    const String currentSsid = WiFi.SSID();
    const String primarySsid = _settings.get.wifiSsid0();
    const bool onPrimary = primarySsid.length() > 0 && currentSsid == primarySsid;

    const unsigned long checkInterval =
        onPrimary ? kRoamCheckIntervalMs : kPrimaryRecoveryCheckIntervalMs;

    if (_lastRoamCheckMs != 0 &&
        static_cast<unsigned long>(now - _lastRoamCheckMs) < checkInterval)
    {
        return false;
    }
    _lastRoamCheckMs = now;

    // A manual BSSID lock applies only to the primary network. While on the
    // fallback network we still periodically look for the primary SSID.
    if (onPrimary && _settings.get.wifiBssidLock())
    {
        return false;
    }

    const int currentRssi = WiFi.RSSI();

    // While connected to the preferred network, avoid scans unless signal is
    // actually poor. This keeps mesh roaming lightweight.
    if (onPrimary && currentRssi > kRoamPoorRssiDbm)
    {
        return false;
    }

    const int networkCount = WiFi.scanNetworks(false, true);
    if (networkCount <= 0)
    {
        WiFi.scanDelete();
        return false;
    }

    String targetSsid = onPrimary ? currentSsid : primarySsid;
    if (targetSsid.length() == 0)
    {
        WiFi.scanDelete();
        return false;
    }

    int bestIndex = -1;
    int bestRssi = -127;

    uint8_t lockedBssid[6] = {};
    const bool lockRequested = !onPrimary && _settings.get.wifiBssidLock();
    const bool lockValid = lockRequested && parseBssid(_settings.get.wifiBssid0(), lockedBssid);

    for (int i = 0; i < networkCount; ++i)
    {
        if (WiFi.SSID(i) != targetSsid)
            continue;

        const uint8_t *scanBssid = WiFi.BSSID(i);
        if (lockRequested)
        {
            if (!lockValid || scanBssid == nullptr || memcmp(scanBssid, lockedBssid, 6) != 0)
                continue;
        }

        const int candidateRssi = WiFi.RSSI(i);
        if (candidateRssi > bestRssi)
        {
            bestRssi = candidateRssi;
            bestIndex = i;
        }
    }

    if (bestIndex < 0)
    {
        WiFi.scanDelete();
        return false;
    }

    if (onPrimary && (bestRssi - currentRssi) < kRoamImprovementDb)
    {
        WiFi.scanDelete();
        return false;
    }

    const uint8_t *bestBssid = WiFi.BSSID(bestIndex);
    const int32_t channel = WiFi.channel(bestIndex);
    const String bestBssidText = WiFi.BSSIDstr(bestIndex);
    const String currentBssid = WiFi.BSSIDstr();

    const char *password = nullptr;
    if (targetSsid == _settings.get.wifiSsid0())
        password = _settings.get.wifiPassword0();
    else if (targetSsid == _settings.get.wifiSsid1())
        password = _settings.get.wifiPassword1();

    if (password == nullptr)
    {
        WiFi.scanDelete();
        return false;
    }

    if (!onPrimary)
    {
        LogSerial.printf("[Network] Preferred SSID restored: switching %s -> %s, BSSID=%s RSSI=%d dBm\n",
                         currentSsid.c_str(),
                         targetSsid.c_str(),
                         bestBssidText.c_str(),
                         bestRssi);
    }
    else
    {
        LogSerial.printf("[Network] Roaming %s -> %s, RSSI %d -> %d dBm\n",
                         currentBssid.c_str(),
                         bestBssidText.c_str(),
                         currentRssi,
                         bestRssi);
    }

    WiFi.begin(targetSsid.c_str(),
               password,
               channel > 0 ? channel : 0,
               bestBssid,
               true);
    WiFi.scanDelete();
    return true;
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
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
    WiFi.softAP(String(SOURCE_NAME) + "-AP");
    dnsServer.start(53, "*", apIp);
    LogSerial.printf("[Network] AP started on %s\n", WiFi.softAPIP().toString().c_str());
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
