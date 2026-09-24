#include "core/TelemetryBacklog.h"

#include <LittleFS.h>

extern void writeLog(const char *format, ...);

namespace
{
constexpr const char *kPartitionLabel = "backlog";
constexpr const char *kBasePath = "/littlefs";
constexpr const char *kQueueDir = "/mqttq";
constexpr size_t kReserveBytes = 48 * 1024;

bool parseQueueSeq(const String &name, uint32_t &seq)
{
    int slash = name.lastIndexOf('/');
    String base = slash >= 0 ? name.substring(slash + 1) : name;
    if (!base.startsWith("q") || !base.endsWith(".json"))
        return false;

    String number = base.substring(1, base.length() - 5);
    if (number.length() == 0)
        return false;

    for (size_t i = 0; i < number.length(); ++i)
    {
        if (!isDigit(static_cast<unsigned char>(number.charAt(i))))
            return false;
    }

    seq = static_cast<uint32_t>(strtoul(number.c_str(), nullptr, 10));
    return seq != 0;
}
} // namespace

bool TelemetryBacklog::begin()
{
    _ready = LittleFS.begin(true, kBasePath, 10, kPartitionLabel);
    if (!_ready)
    {
        writeLog("[MQTT][BACKLOG] LittleFS mount failed");
        return false;
    }

    if (!LittleFS.exists(kQueueDir) && !LittleFS.mkdir(kQueueDir))
    {
        writeLog("[MQTT][BACKLOG] unable to create %s", kQueueDir);
        _ready = false;
        return false;
    }

    if (!scanQueue())
    {
        writeLog("[MQTT][BACKLOG] queue scan failed");
        return false;
    }

    writeLog("[MQTT][BACKLOG] ready records=%u used=%u/%u bytes next=%lu",
             static_cast<unsigned>(_count),
             static_cast<unsigned>(usedBytes()),
             static_cast<unsigned>(totalBytes()),
             static_cast<unsigned long>(_nextSeq));
    return true;
}

String TelemetryBacklog::pathFor(uint32_t seq) const
{
    char path[32];
    snprintf(path, sizeof(path), "%s/q%010lu.json",
             kQueueDir,
             static_cast<unsigned long>(seq));
    return String(path);
}

bool TelemetryBacklog::scanQueue()
{
    _count = 0;
    _oldestSeq = 0;
    uint32_t highest = 0;

    File dir = LittleFS.open(kQueueDir);
    if (!dir || !dir.isDirectory())
        return false;

    File file = dir.openNextFile();
    while (file)
    {
        uint32_t seq = 0;
        if (parseQueueSeq(file.name(), seq))
        {
            ++_count;
            if (_oldestSeq == 0 || seq < _oldestSeq)
                _oldestSeq = seq;
            if (seq > highest)
                highest = seq;
        }
        file = dir.openNextFile();
    }

    _nextSeq = highest == UINT32_MAX ? 1 : highest + 1;
    if (_nextSeq == 0)
        _nextSeq = 1;

    while (_count > kMaxRecords)
    {
        if (!removeOldestForSpace())
            break;
    }

    return true;
}

bool TelemetryBacklog::advanceOldest()
{
    if (_count == 0)
    {
        _oldestSeq = 0;
        return true;
    }

    uint32_t candidate = _oldestSeq + 1;
    for (uint32_t checked = 0; checked < static_cast<uint32_t>(kMaxRecords) + 8U; ++checked, ++candidate)
    {
        if (candidate == 0)
            candidate = 1;
        if (LittleFS.exists(pathFor(candidate)))
        {
            _oldestSeq = candidate;
            return true;
        }
    }

    return scanQueue();
}

bool TelemetryBacklog::removeOldestForSpace()
{
    if (_count == 0 || _oldestSeq == 0)
        return true;

    const String path = pathFor(_oldestSeq);
    if (!LittleFS.remove(path))
        return false;

    writeLog("[MQTT][BACKLOG] dropped oldest seq=%lu to make space",
             static_cast<unsigned long>(_oldestSeq));

    --_count;
    return advanceOldest();
}

bool TelemetryBacklog::ensureCapacity(size_t incomingBytes)
{
    if (!_ready)
        return false;

    if (incomingBytes > kMaxRecordBytes)
        return false;

    const size_t total = totalBytes();
    while (_count >= kMaxRecords ||
           (total > 0 && usedBytes() + incomingBytes + kReserveBytes > total))
    {
        if (_count == 0)
            break;
        if (!removeOldestForSpace())
            return false;
    }

    return total == 0 || usedBytes() + incomingBytes + kReserveBytes <= total;
}

bool TelemetryBacklog::capture(JsonObjectConst liveData, uint32_t epochSeconds, uint32_t uptimeSeconds)
{
    if (!_ready || liveData.isNull())
        return false;

    JsonDocument doc;
    doc["seq"] = _nextSeq;
    doc["epoch"] = epochSeconds;
    doc["uptime"] = uptimeSeconds;

    JsonObject target = doc["live"].to<JsonObject>();
    for (JsonPairConst entry : liveData)
    {
        target[entry.key()] = entry.value();
    }

    String payload;
    serializeJson(doc, payload);
    if (payload.length() == 0 || payload.length() > kMaxRecordBytes)
    {
        writeLog("[MQTT][BACKLOG] record too large: %u bytes",
                 static_cast<unsigned>(payload.length()));
        return false;
    }

    if (!ensureCapacity(payload.length()))
    {
        writeLog("[MQTT][BACKLOG] no filesystem capacity for record");
        return false;
    }

    const uint32_t seq = _nextSeq;
    const String finalPath = pathFor(seq);
    const String tempPath = String(kQueueDir) + "/pending.tmp";

    LittleFS.remove(tempPath);
    File file = LittleFS.open(tempPath, FILE_WRITE);
    if (!file)
        return false;

    const size_t written = file.print(payload);
    file.flush();
    file.close();

    if (written != payload.length())
    {
        LittleFS.remove(tempPath);
        return false;
    }

    LittleFS.remove(finalPath);
    if (!LittleFS.rename(tempPath, finalPath))
    {
        LittleFS.remove(tempPath);
        return false;
    }

    if (_count == 0)
        _oldestSeq = seq;
    ++_count;

    ++_nextSeq;
    if (_nextSeq == 0)
        _nextSeq = 1;

    writeLog("[MQTT][BACKLOG] stored seq=%lu records=%u bytes=%u",
             static_cast<unsigned long>(seq),
             static_cast<unsigned>(_count),
             static_cast<unsigned>(payload.length()));
    return true;
}

bool TelemetryBacklog::peek(String &payload, uint32_t &seq)
{
    payload = "";
    seq = 0;

    if (!_ready || _count == 0)
        return false;

    if (_oldestSeq == 0 && !scanQueue())
        return false;

    const String path = pathFor(_oldestSeq);
    File file = LittleFS.open(path, FILE_READ);
    if (!file)
    {
        if (!scanQueue() || _count == 0)
            return false;
        file = LittleFS.open(pathFor(_oldestSeq), FILE_READ);
        if (!file)
            return false;
    }

    payload = file.readString();
    file.close();

    if (payload.length() == 0 || payload.length() > kMaxRecordBytes)
        return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload))
        return false;

    seq = doc["seq"] | 0U;
    return seq == _oldestSeq;
}

bool TelemetryBacklog::confirm(uint32_t seq)
{
    if (!_ready || _count == 0 || seq == 0 || seq != _oldestSeq)
        return false;

    const String path = pathFor(seq);
    if (!LittleFS.remove(path))
        return false;

    --_count;
    advanceOldest();

    writeLog("[MQTT][BACKLOG] confirmed seq=%lu remaining=%u",
             static_cast<unsigned long>(seq),
             static_cast<unsigned>(_count));
    return true;
}

size_t TelemetryBacklog::totalBytes() const
{
    return _ready ? LittleFS.totalBytes() : 0;
}

size_t TelemetryBacklog::usedBytes() const
{
    return _ready ? LittleFS.usedBytes() : 0;
}
