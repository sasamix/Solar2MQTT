#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class TelemetryBacklog
{
public:
    bool begin();
    bool ready() const { return _ready; }

    bool capture(JsonObjectConst liveData, uint32_t epochSeconds, uint32_t uptimeSeconds);
    bool peek(String &payload, uint32_t &seq);
    bool confirm(uint32_t seq);

    uint16_t count() const { return _count; }
    size_t totalBytes() const;
    size_t usedBytes() const;

private:
    static constexpr uint16_t kMaxRecords = 240;
    static constexpr size_t kMaxRecordBytes = 8192;

    bool _ready = false;
    uint32_t _oldestSeq = 0;
    uint32_t _nextSeq = 1;
    uint16_t _count = 0;

    String pathFor(uint32_t seq) const;
    bool scanQueue();
    bool advanceOldest();
    bool removeOldestForSpace();
    bool ensureCapacity(size_t incomingBytes);
};
