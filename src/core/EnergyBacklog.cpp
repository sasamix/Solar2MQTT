#include "core/EnergyBacklog.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <stddef.h>

#include "core/SolarState.h"
#include "descriptors.h"

extern void writeLog(const char *format, ...);

namespace
{
constexpr const char *BACKLOG_PARTITION = "backlog";
constexpr const char *BACKLOG_FILE = "/energy.bin";
constexpr const char *BACKLOG_TEMP_FILE = "/energy.tmp";
constexpr uint32_t RECORD_MAGIC = 0x454E5247UL; // "ENRG"
constexpr uint16_t RECORD_VERSION = 1;
constexpr unsigned long CAPTURE_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr size_t MAX_BACKLOG_BYTES = 192UL * 1024UL;
constexpr size_t KEEP_BACKLOG_BYTES = 96UL * 1024UL;

const char *const ENERGY_KEYS[] = {
    DESCR_PV_Generation_Sum,
    DESCR_PV_Generation_Year,
    DESCR_PV_Generation_Month,
    DESCR_PV_Generation_Day,
    DESCR_AC_In_Generation_Sum,
    DESCR_AC_In_Generation_Year,
    DESCR_AC_In_Generation_Month,
    DESCR_AC_In_Generation_Day,
};

constexpr size_t ENERGY_KEY_COUNT = sizeof(ENERGY_KEYS) / sizeof(ENERGY_KEYS[0]);

struct EnergyRecord
{
    // Order fields so the 64-bit counters stay naturally aligned on every
    // ESP32 family while the on-flash record remains exactly 80 bytes.
    uint32_t magic;
    uint32_t sequence;
    int64_t values[ENERGY_KEY_COUNT];
    uint16_t version;
    uint16_t mask;
    uint32_t checksum;
};

static_assert(ENERGY_KEY_COUNT == 8, "Energy backlog format expects 8 counters");
static_assert(sizeof(EnergyRecord) == 80, "Unexpected EnergyRecord size");

uint32_t recordChecksum(const EnergyRecord &record)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
    const size_t length = offsetof(EnergyRecord, checksum);

    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

bool recordValid(const EnergyRecord &record)
{
    return record.magic == RECORD_MAGIC &&
           record.version == RECORD_VERSION &&
           record.mask != 0 &&
           record.checksum == recordChecksum(record);
}

bool readEnergyValues(SolarState &state, EnergyRecord &record)
{
    JsonDocument snapshot;
    state.snapshotTo(snapshot);
    JsonObjectConst live = snapshot["LiveData"].as<JsonObjectConst>();

    record.mask = 0;
    for (size_t i = 0; i < ENERGY_KEY_COUNT; ++i)
    {
        JsonVariantConst value = live[ENERGY_KEYS[i]];
        if (value.isNull())
        {
            continue;
        }

        record.values[i] = value.as<int64_t>();
        record.mask |= static_cast<uint16_t>(1U << i);
    }

    return record.mask != 0;
}

bool publishEnergyRecord(PubSubClient &client, const String &baseTopic, const EnergyRecord &record)
{
    for (size_t i = 0; i < ENERGY_KEY_COUNT; ++i)
    {
        const uint16_t bit = static_cast<uint16_t>(1U << i);
        if ((record.mask & bit) == 0)
        {
            continue;
        }

        const String topic = baseTopic + "/LiveData/" + ENERGY_KEYS[i];
        char payload[32];
        snprintf(payload, sizeof(payload), "%lld", static_cast<long long>(record.values[i]));

        if (!client.publish(topic.c_str(), payload, true))
        {
            return false;
        }

        client.loop();
    }

    return true;
}
} // namespace

EnergyBacklog::EnergyBacklog()
    : _ready(false),
      _offlineActive(false),
      _haveLastValues(false),
      _replayActive(false),
      _lastCaptureMs(0),
      _nextSequence(1),
      _replayOffset(0),
      _lastMask(0)
{
    memset(_lastValues, 0, sizeof(_lastValues));
}

void EnergyBacklog::begin()
{
    _ready = LittleFS.begin(true, "/littlefs", 5, BACKLOG_PARTITION);
    if (!_ready)
    {
        writeLog("[EnergyBacklog] LittleFS mount failed; offline counters disabled");
        return;
    }

    repairTrailingPartialRecord();

    File file = LittleFS.open(BACKLOG_FILE, "r");
    if (file)
    {
        EnergyRecord record{};
        while (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record))
        {
            if (recordValid(record) && record.sequence >= _nextSequence)
            {
                _nextSequence = record.sequence + 1;
            }
        }

        writeLog("[EnergyBacklog] Ready, pending=%u bytes, next_seq=%u",
                 static_cast<unsigned>(file.size()),
                 static_cast<unsigned>(_nextSequence));
        file.close();
    }
    else
    {
        writeLog("[EnergyBacklog] Ready, backlog empty");
    }
}

void EnergyBacklog::captureIfNeeded(SolarState &state, bool mqttOffline, unsigned long nowMs)
{
    if (!_ready)
    {
        return;
    }

    if (!mqttOffline)
    {
        _offlineActive = false;
        return;
    }

    appendCurrentSnapshot(state, nowMs, !_offlineActive);
    _offlineActive = true;
}

bool EnergyBacklog::appendCurrentSnapshot(SolarState &state, unsigned long nowMs, bool force)
{
    EnergyRecord record{};
    record.magic = RECORD_MAGIC;
    record.version = RECORD_VERSION;
    record.sequence = _nextSequence;

    if (!readEnergyValues(state, record))
    {
        return false;
    }

    bool changed = !_haveLastValues || record.mask != _lastMask;
    bool decreased = false;

    for (size_t i = 0; i < ENERGY_KEY_COUNT; ++i)
    {
        const uint16_t bit = static_cast<uint16_t>(1U << i);
        if ((record.mask & bit) == 0)
        {
            continue;
        }

        if (!_haveLastValues || (_lastMask & bit) == 0 || record.values[i] != _lastValues[i])
        {
            changed = true;
        }

        if (_haveLastValues && (_lastMask & bit) != 0 && record.values[i] < _lastValues[i])
        {
            decreased = true;
        }
    }

    if (!changed)
    {
        return false;
    }

    if (!force && !decreased && (nowMs - _lastCaptureMs) < CAPTURE_INTERVAL_MS)
    {
        return false;
    }

    record.checksum = recordChecksum(record);

    if (!compactIfNeeded(sizeof(record)))
    {
        return false;
    }

    File file = LittleFS.open(BACKLOG_FILE, "a");
    if (!file)
    {
        writeLog("[EnergyBacklog] Failed to open backlog for append");
        return false;
    }

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    file.flush();
    file.close();

    if (written != sizeof(record))
    {
        writeLog("[EnergyBacklog] Incomplete backlog write: %u/%u",
                 static_cast<unsigned>(written),
                 static_cast<unsigned>(sizeof(record)));
        repairTrailingPartialRecord();
        return false;
    }

    ++_nextSequence;
    _lastCaptureMs = nowMs;
    _lastMask = record.mask;
    memcpy(_lastValues, record.values, sizeof(_lastValues));
    _haveLastValues = true;
    return true;
}

bool EnergyBacklog::compactIfNeeded(size_t incomingBytes)
{
    File source = LittleFS.open(BACKLOG_FILE, "r");
    if (!source)
    {
        return true;
    }

    const size_t sourceSize = source.size();
    if ((sourceSize + incomingBytes) <= MAX_BACKLOG_BYTES)
    {
        source.close();
        return true;
    }

    const size_t recordSize = sizeof(EnergyRecord);
    size_t keepBytes = KEEP_BACKLOG_BYTES - (KEEP_BACKLOG_BYTES % recordSize);
    if (keepBytes > sourceSize)
    {
        keepBytes = sourceSize - (sourceSize % recordSize);
    }

    const size_t startOffset = sourceSize - keepBytes;
    if (!source.seek(startOffset, SeekSet))
    {
        source.close();
        writeLog("[EnergyBacklog] Compaction seek failed");
        return false;
    }

    LittleFS.remove(BACKLOG_TEMP_FILE);
    File target = LittleFS.open(BACKLOG_TEMP_FILE, "w");
    if (!target)
    {
        source.close();
        writeLog("[EnergyBacklog] Compaction temp open failed");
        return false;
    }

    uint8_t buffer[512];
    size_t remaining = keepBytes;
    bool ok = true;

    while (remaining > 0)
    {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t read = source.read(buffer, chunk);
        if (read != chunk || target.write(buffer, read) != read)
        {
            ok = false;
            break;
        }
        remaining -= read;
    }

    target.flush();
    target.close();
    source.close();

    if (!ok)
    {
        LittleFS.remove(BACKLOG_TEMP_FILE);
        writeLog("[EnergyBacklog] Compaction copy failed");
        return false;
    }

    LittleFS.remove(BACKLOG_FILE);
    if (!LittleFS.rename(BACKLOG_TEMP_FILE, BACKLOG_FILE))
    {
        writeLog("[EnergyBacklog] Compaction rename failed");
        return false;
    }

    writeLog("[EnergyBacklog] Backlog compacted to %u bytes", static_cast<unsigned>(keepBytes));
    return true;
}

bool EnergyBacklog::repairTrailingPartialRecord()
{
    File source = LittleFS.open(BACKLOG_FILE, "r");
    if (!source)
    {
        return true;
    }

    const size_t recordSize = sizeof(EnergyRecord);
    const size_t currentSize = source.size();
    const size_t alignedSize = currentSize - (currentSize % recordSize);

    if (alignedSize == currentSize)
    {
        source.close();
        return true;
    }

    // This Arduino-ESP32 FS implementation does not expose File::truncate().
    // Recover from a torn final write by rebuilding only the newest aligned
    // records into the temporary file. Keeping at most KEEP_BACKLOG_BYTES also
    // guarantees enough free space for the recovery copy.
    size_t keepBytes = KEEP_BACKLOG_BYTES - (KEEP_BACKLOG_BYTES % recordSize);
    if (keepBytes > alignedSize)
    {
        keepBytes = alignedSize;
    }

    const size_t startOffset = alignedSize - keepBytes;
    if (!source.seek(startOffset, SeekSet))
    {
        source.close();
        writeLog("[EnergyBacklog] Partial-tail recovery seek failed");
        return false;
    }

    LittleFS.remove(BACKLOG_TEMP_FILE);
    File target = LittleFS.open(BACKLOG_TEMP_FILE, "w");
    if (!target)
    {
        source.close();
        writeLog("[EnergyBacklog] Partial-tail recovery temp open failed");
        return false;
    }

    uint8_t buffer[512];
    size_t remaining = keepBytes;
    bool ok = true;

    while (remaining > 0)
    {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t read = source.read(buffer, chunk);
        if (read != chunk || target.write(buffer, read) != read)
        {
            ok = false;
            break;
        }
        remaining -= read;
    }

    target.flush();
    target.close();
    source.close();

    if (!ok)
    {
        LittleFS.remove(BACKLOG_TEMP_FILE);
        writeLog("[EnergyBacklog] Partial-tail recovery copy failed");
        return false;
    }

    LittleFS.remove(BACKLOG_FILE);
    if (!LittleFS.rename(BACKLOG_TEMP_FILE, BACKLOG_FILE))
    {
        writeLog("[EnergyBacklog] Partial-tail recovery rename failed");
        return false;
    }

    writeLog("[EnergyBacklog] Recovered torn write; dropped %u trailing bytes",
             static_cast<unsigned>(currentSize - alignedSize));
    return true;
}

bool EnergyBacklog::startReplay()
{
    if (!_ready)
    {
        return false;
    }

    repairTrailingPartialRecord();

    File file = LittleFS.open(BACKLOG_FILE, "r");
    if (!file)
    {
        return false;
    }

    const size_t size = file.size();
    file.close();

    if (size < sizeof(EnergyRecord))
    {
        return false;
    }

    _replayOffset = 0;
    _replayActive = true;
    writeLog("[EnergyBacklog] Replay started: %u bytes", static_cast<unsigned>(size));
    return true;
}

EnergyBacklog::ReplayResult EnergyBacklog::replayBatch(PubSubClient &client,
                                                       const String &baseTopic,
                                                       size_t maxRecords)
{
    if (!_replayActive)
    {
        return ReplayResult::Nothing;
    }

    if (!client.connected())
    {
        cancelReplay();
        return ReplayResult::Failed;
    }

    File file = LittleFS.open(BACKLOG_FILE, "r");
    if (!file)
    {
        cancelReplay();
        return ReplayResult::Failed;
    }

    const size_t fileSize = file.size();
    if (_replayOffset >= fileSize)
    {
        file.close();
        LittleFS.remove(BACKLOG_FILE);
        cancelReplay();
        return ReplayResult::Complete;
    }

    if (!file.seek(_replayOffset, SeekSet))
    {
        file.close();
        cancelReplay();
        return ReplayResult::Failed;
    }

    size_t processed = 0;
    EnergyRecord record{};

    while (processed < maxRecords &&
           file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record))
    {
        _replayOffset += sizeof(record);

        if (!recordValid(record))
        {
            ++processed;
            continue;
        }

        if (!publishEnergyRecord(client, baseTopic, record))
        {
            file.close();
            cancelReplay();
            return ReplayResult::Failed;
        }

        ++processed;
    }

    const bool complete = _replayOffset >= fileSize;
    file.close();

    if (!complete)
    {
        return ReplayResult::InProgress;
    }

    if (!LittleFS.remove(BACKLOG_FILE))
    {
        writeLog("[EnergyBacklog] Replay complete but backlog delete failed");
        cancelReplay();
        return ReplayResult::Failed;
    }

    writeLog("[EnergyBacklog] Replay complete; backlog cleared");
    cancelReplay();
    return ReplayResult::Complete;
}

void EnergyBacklog::cancelReplay()
{
    _replayActive = false;
    _replayOffset = 0;
}
