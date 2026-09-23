// #define isDEBUG
#include "modbus.h"
 
//----------------------------------------------------------------------
//  Public Functions
//----------------------------------------------------------------------

MODBUS::MODBUS(HardwareSerial *port, int rxPin, int txPin)
{
    my_serialIntf = port;
    _rxPin = rxPin;
    _txPin = txPin;
}

MODBUS::~MODBUS()
{
    delete device;
    device = nullptr;
}
 
bool MODBUS::Init()
{
    // Null check the serial interface
    if (this->my_serialIntf == NULL)
    {
        writeLog("No serial specificed!");
        return false;
    }
    //this->my_serialIntf->setTimeout(2000);
 
    return true;
}

void MODBUS::prepareRegisters()
{
    const modbus_register_t *registers_live = device->getLiveRegisters();
    const modbus_register_t *registers_static = device->getStaticRegisters();

    live_info = {
        .variant = &liveData,
        .registers = registers_live,
        .array_size = device->getLiveRegistersCount(),
        .curr_register = 0};
    static_info = {
        .variant = &staticData,
        .registers = registers_static,
        .array_size = device->getStaticRegistersCount(),
        .curr_register = 0};
    previousTime = millis();
}

void MODBUS::loop()
{
    if (device == nullptr)
    {
        return;
    }

    if (_powmrDumpRunning)
    {
        previousTime = millis();
        return;
    }

    if (millis() - previousTime < kCommandDelayMs)
    {
        return;
    }

    modbus_register_info_t *cur_info_registers = &live_info;
    if (requestStaticData)
    {
        cur_info_registers = &static_info;
    }
    switch (_mCom.parseModbusToJson(*cur_info_registers))
    {
    case READ_OK:
        connectionCounter = 0;
        break;
    case READ_FAIL:
        connectionCounter++;
        break;
    default:
        break;
    }

    connection = connectionCounter < MAX_CONNECTION_ATTEMPTS;
    if (_mCom.isAllRegistersRead(*cur_info_registers))
    {
        const bool completedLivePass = (cur_info_registers == &live_info);
        requestStaticData = false;

        if (completedLivePass && device != nullptr && device->getProtocol() == MODBUS_POWMR)
        {
            capturePowmrSocSample();
        }

        if (requestCallback)
        {
            requestCallback();
        }
    }

    previousTime = millis();
}

void MODBUS::callback(std::function<void()> func)
{
    requestCallback = func;
}

void MODBUS::capturePowmrSocSample()
{
    const unsigned long now = millis();
    if (_powmrSocHistoryCount > 0 &&
        static_cast<unsigned long>(now - _powmrSocLastSampleMs) < kPowMrSocSampleIntervalMs)
    {
        return;
    }

    const JsonVariant socVar = liveData[DESCR_Battery_Percent];
    const JsonVariant voltageVar = liveData[DESCR_Battery_Voltage];
    const JsonVariant chargeVar = liveData[DESCR_Battery_Charge_Current];
    const JsonVariant dischargeVar = liveData[DESCR_Battery_Discharge_Current];

    if (socVar.isNull() || voltageVar.isNull() || chargeVar.isNull() || dischargeVar.isNull())
    {
        return;
    }

    const int soc = socVar.as<int>();
    const float voltage = voltageVar.as<float>();
    const int charge = chargeVar.as<int>();
    const int discharge = dischargeVar.as<int>();

    if (soc < 0 || soc > 100 || voltage < 35.0f || voltage > 70.0f ||
        charge < 0 || charge > 300 || discharge < 0 || discharge > 300)
    {
        return;
    }

    PowMrSocSample &sample = _powmrSocHistory[_powmrSocHistoryHead];
    sample.uptimeSeconds = now / 1000UL;
    sample.batteryDeciVolts = static_cast<uint16_t>(voltage * 10.0f + 0.5f);
    sample.socPercent = static_cast<uint8_t>(soc);
    sample.chargeAmps = static_cast<uint16_t>(charge);
    sample.dischargeAmps = static_cast<uint16_t>(discharge);
    sample.valid = true;

    _powmrSocHistoryHead = static_cast<uint8_t>((_powmrSocHistoryHead + 1) % kPowMrSocHistorySize);
    if (_powmrSocHistoryCount < kPowMrSocHistorySize)
    {
        _powmrSocHistoryCount++;
    }
    _powmrSocLastSampleMs = now;
}

String MODBUS::buildPowmrSocDiag() const
{
    String answer;
    answer.reserve(7000);
    answer = "POWMR_SOC_DIAG interval=5min samples=";
    answer += static_cast<unsigned int>(_powmrSocHistoryCount);
    answer += "/";
    answer += static_cast<unsigned int>(kPowMrSocHistorySize);
    answer += "\n";

    if (_powmrSocHistoryCount == 0)
    {
        answer += "NO HISTORY YET: wait for a complete PowMr live-data pass";
        return answer;
    }

    const uint32_t nowSec = millis() / 1000UL;
    const uint8_t oldestIndex = static_cast<uint8_t>(
        (_powmrSocHistoryHead + kPowMrSocHistorySize - _powmrSocHistoryCount) %
        kPowMrSocHistorySize);

    uint8_t firstSoc = 0;
    uint8_t lastSoc = 0;
    uint32_t firstTime = 0;
    uint32_t lastTime = 0;
    float estimatedDischargeAh = 0.0f;
    bool first = true;
    bool havePrevious = false;
    PowMrSocSample previous;

    for (uint8_t i = 0; i < _powmrSocHistoryCount; ++i)
    {
        const uint8_t idx = static_cast<uint8_t>((oldestIndex + i) % kPowMrSocHistorySize);
        const PowMrSocSample &sample = _powmrSocHistory[idx];
        if (!sample.valid)
            continue;

        const uint32_t ageSec = nowSec >= sample.uptimeSeconds ? nowSec - sample.uptimeSeconds : 0;
        const uint32_t ageMin = ageSec / 60UL;

        char line[96];
        snprintf(line, sizeof(line),
                 "age=%lum SOC=%u V=%u.%u charge=%uA discharge=%uA\n",
                 static_cast<unsigned long>(ageMin),
                 static_cast<unsigned int>(sample.socPercent),
                 static_cast<unsigned int>(sample.batteryDeciVolts / 10),
                 static_cast<unsigned int>(sample.batteryDeciVolts % 10),
                 static_cast<unsigned int>(sample.chargeAmps),
                 static_cast<unsigned int>(sample.dischargeAmps));
        answer += line;

        if (first)
        {
            firstSoc = sample.socPercent;
            firstTime = sample.uptimeSeconds;
            first = false;
        }
        lastSoc = sample.socPercent;
        lastTime = sample.uptimeSeconds;

        if (havePrevious && sample.uptimeSeconds >= previous.uptimeSeconds)
        {
            const float hours = static_cast<float>(sample.uptimeSeconds - previous.uptimeSeconds) / 3600.0f;
            const float netDischarge = static_cast<float>(
                previous.dischargeAmps > previous.chargeAmps
                    ? previous.dischargeAmps - previous.chargeAmps
                    : 0);
            estimatedDischargeAh += netDischarge * hours;
        }
        previous = sample;
        havePrevious = true;
    }

    const uint32_t spanMin = (lastTime >= firstTime) ? (lastTime - firstTime) / 60UL : 0;
    answer += "SUMMARY span=";
    answer += static_cast<unsigned long>(spanMin);
    answer += "m SOC=";
    answer += static_cast<unsigned int>(firstSoc);
    answer += "->";
    answer += static_cast<unsigned int>(lastSoc);
    answer += " estimated_net_discharge=";
    answer += String(estimatedDischargeAh, 1);
    answer += "Ah";

    if (spanMin >= 30 && firstSoc == lastSoc && estimatedDischargeAh >= 5.0f)
    {
        answer += " WARNING=SOC_STUCK_SUSPECTED";
    }

    return answer;
}

void MODBUS::powmrDumpTask(void *param)
{
    MODBUS *self = static_cast<MODBUS *>(param);
    self->runPowmrDump();
    self->_powmrDumpTask = nullptr;
    vTaskDelete(nullptr);
}

void MODBUS::runPowmrDump()
{
    String answer;
    answer.reserve(14000);
    answer = "POWMR_DIAG BEGIN holding=4500-4564,5000-5038 input=4500-4564,5000-5038 readable-only\n";

    uint16_t readable = 0;
    uint16_t failed = 0;
    const uint16_t oldTimeout = _mCom.getResponseTimeout();
    _mCom.setResponseTimeout(250);
    _mCom.clearReadCache();

    _powmrDumpCurrentRegister = 4500;
    _powmrDumpReadable = 0;
    _powmrDumpFailed = 0;

    ModbusMaster *mb = _mCom.getModbusMaster();

    auto appendReadable = [&](const char *space, uint16_t reg, uint16_t raw) {
        const uint16_t swapped = static_cast<uint16_t>((raw >> 8) | (raw << 8));
        const uint16_t candidates[] = {25, 30, 35, 70, 99, 250, 300, 350, 700, 990};
        bool marked = false;
        for (uint16_t v : candidates)
        {
            if (raw == v || swapped == v)
            {
                marked = true;
                break;
            }
        }
        char line[104];
        snprintf(line, sizeof(line), "%s reg=%u raw=%u swap=%u hex=0x%04X%s\n",
                 space, static_cast<unsigned int>(reg), static_cast<unsigned int>(raw),
                 static_cast<unsigned int>(swapped), static_cast<unsigned int>(raw),
                 marked ? " CANDIDATE" : "");
        answer += line;
        readable++;
        _powmrDumpReadable = readable;
    };

    auto scanHolding = [&](uint16_t first, uint16_t last) {
        for (uint16_t reg = first; reg <= last; ++reg)
        {
            _powmrDumpCurrentRegister = reg;
            const uint8_t result = mb->readHoldingRegisters(reg, 1);
            if (result == mb->ku8MBSuccess)
                appendReadable("H", reg, mb->getResponseBuffer(0));
            else
            {
                failed++;
                _powmrDumpFailed = failed;
            }
            vTaskDelay(1);
        }
    };

    auto scanInput = [&](uint16_t first, uint16_t last) {
        for (uint16_t reg = first; reg <= last; ++reg)
        {
            _powmrDumpCurrentRegister = reg;
            const uint8_t result = mb->readInputRegisters(reg, 1);
            if (result == mb->ku8MBSuccess)
                appendReadable("I", reg, mb->getResponseBuffer(0));
            else
            {
                failed++;
                _powmrDumpFailed = failed;
            }
            vTaskDelay(1);
        }
    };

    scanHolding(4500, 4564);
    scanHolding(5000, 5038);
    scanInput(4500, 4564);
    scanInput(5000, 5038);

    answer += "POWMR_DIAG END readable=";
    answer += readable;
    answer += " failed=";
    answer += failed;

    _mCom.setResponseTimeout(oldTimeout);
    _mCom.clearReadCache();

    _powmrDumpResult = answer;
    _powmrDumpReady = true;
    _powmrDumpRunning = false;

    writeLog("POWMR_DIAG complete readable=%u failed=%u",
             static_cast<unsigned int>(readable),
             static_cast<unsigned int>(failed));
}
String MODBUS::requestData(String command)
{
    requestStaticData = true;
    command.trim();

    // In-RAM SOC history sampled every five minutes from PowMr live registers.
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command == "powmr socdiag")
    {
        return buildPowmrSocDiag();
    }

    // Compact read-only snapshot of known battery/BMS-facing registers.
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command == "powmr bmsdiag")
    {
        if (_powmrDumpRunning)
            return "ERROR: wait for PowMr diagnostic scan to finish";

        auto readOne = [&](uint16_t reg, uint16_t &value) -> bool {
            _mCom.clearReadCache();
            return _mCom.readHoldingBlock(reg, 1, &value, 1);
        };
        auto swap16 = [](uint16_t value) -> uint16_t {
            return static_cast<uint16_t>((value >> 8) | (value << 8));
        };

        uint16_t r4506=0, r4507=0, r4508=0, r4509=0;
        uint16_t r4539=0, r4553=0, r4554=0, r4557=0;
        const bool ok =
            readOne(4506, r4506) && readOne(4507, r4507) &&
            readOne(4508, r4508) && readOne(4509, r4509) &&
            readOne(4539, r4539) && readOne(4553, r4553) &&
            readOne(4554, r4554) && readOne(4557, r4557);

        if (!ok)
            return "ERROR: one or more BMS diagnostic registers could not be read";

        String answer = "POWMR_BMS_DIAG";
        answer += " V=";
        answer += String(swap16(r4506) / 10.0f, 1);
        answer += " SOC4507=";
        answer += static_cast<unsigned int>(swap16(r4507));
        answer += " charge=";
        answer += static_cast<unsigned int>(swap16(r4508));
        answer += "A discharge=";
        answer += static_cast<unsigned int>(swap16(r4509));
        answer += "A batteryType=";
        answer += static_cast<unsigned int>(swap16(r4539));
        answer += " flags4553=0x";
        answer += String(swap16(r4553), HEX);
        answer += " flags4554=0x";
        answer += String(swap16(r4554), HEX);
        answer += " invTemp4557=";
        answer += static_cast<unsigned int>(swap16(r4557));
        answer += "C";
        return answer;
    }
    // PowMr/Victor battery type (menu 05).
    // Read:  powmr batterytype
    // Write: powmr batterytype AGM|FLD|USE|LIB|LIC|LIP|LIL
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        (command == "powmr batterytype" || command.startsWith("powmr batterytype ")))
    {
        if (_powmrDumpRunning)
            return "ERROR: wait for PowMr scan to finish before changing battery type";

        auto batteryTypeName = [](uint16_t value) -> const char * {
            switch (value)
            {
            case 0: return "AGM";
            case 1: return "FLD";
            case 2: return "USE";
            case 3: return "LIB";
            case 4: return "LIC";
            case 5: return "LIP";
            case 6: return "LIL";
            default: return "UNKNOWN";
            }
        };

        if (command == "powmr batterytype")
        {
            uint16_t value = 0;
            _mCom.clearReadCache();
            if (!_mCom.readHoldingBlock(5020, 1, &value, 1))
                return "ERROR: unable to read battery type register 5020";

            return String("OK: Battery type = ") + batteryTypeName(value) +
                   " (" + static_cast<unsigned int>(value) + ")";
        }

        String requested = command.substring(18);
        requested.trim();
        requested.toUpperCase();

        int value = -1;
        if (requested == "AGM") value = 0;
        else if (requested == "FLD") value = 1;
        else if (requested == "USE") value = 2;
        else if (requested == "LIB") value = 3;
        else if (requested == "LIC") value = 4;
        else if (requested == "LIP") value = 5;
        else if (requested == "LIL") value = 6;

        if (value < 0)
            return "ERROR: battery type must be AGM/FLD/USE/LIB/LIC/LIP/LIL";

        if (!_mCom.writeHoldingRegister(5020, static_cast<uint16_t>(value)))
        {
            const uint8_t result = _mCom.getLastWriteResult();
            return String("ERROR: battery type write failed result=") +
                   static_cast<unsigned int>(result) + " (" +
                   _mCom.getLastWriteResultText() + ")";
        }

        delay(150);
        uint16_t readback = 0xFFFF;
        _mCom.clearReadCache();
        const bool readOk = _mCom.readHoldingBlock(5020, 1, &readback, 1);

        static_info.curr_register = 0;
        requestStaticData = true;

        if (!readOk)
            return String("OK: Battery type write accepted: ") + requested +
                   "; readback unavailable";

        if (readback != static_cast<uint16_t>(value))
            return String("ERROR: battery type readback mismatch requested=") +
                   requested + " actual=" + batteryTypeName(readback) +
                   " (" + static_cast<unsigned int>(readback) + ")";

        return String("OK: Battery type = ") + batteryTypeName(readback) +
               " (" + static_cast<unsigned int>(readback) + ")";
    }

    // Guarded first write command for PowMr/Victor.
    // Syntax: powmr charge <amps>
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command.startsWith("powmr charge "))
    {
        const String valueText = command.substring(13);
        const int amps = valueText.toInt();
        const bool allowed = amps == 10 || amps == 20 || amps == 30 ||
                             amps == 40 || amps == 50 || amps == 60;
        if (!allowed)
        {
            return "ERROR: allowed charge current is 10/20/30/40/50/60 A";
        }

        // PowMr control register 5022 is written in normal Modbus word order.
        // 50 A is value 0x0032; do not byte-swap control-register writes.
        const uint16_t rawValue = static_cast<uint16_t>(amps);

        if (!_mCom.writeHoldingRegister(5022, rawValue))
        {
            const uint8_t result = _mCom.getLastWriteResult();
            return String("ERROR: Modbus write failed result=") +
                   static_cast<unsigned int>(result) + " (" +
                   _mCom.getLastWriteResultText() + ")";
        }

        _mCom.clearReadCache();
        static_info.curr_register = 0;
        requestStaticData = true;
        return String("OK: Max charging current = ") + amps + " A";
    }

    // Safe read-only Victor/PowMr register diagnostics.
    // Syntax: powmr read <start> <count>. Restrict to the 5000-series
    // control/config area and never perform writes from this command.
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command.startsWith("powmr read "))
    {
        String args = command.substring(11);
        args.trim();
        const int split = args.indexOf(' ');
        if (split <= 0)
            return "ERROR: syntax powmr read <start> <count>";
        const int start = args.substring(0, split).toInt();
        const int count = args.substring(split + 1).toInt();
        if (start < 5000 || start > 5099 || count < 1 || count > 20 ||
            start + count - 1 > 5099)
            return "ERROR: diagnostic range must stay within 5000..5099, max 20 registers";

        uint16_t values[20] = {};
        String answer = "OK:";
        _mCom.clearReadCache();

        if (_mCom.readHoldingBlock(static_cast<uint16_t>(start),
                                   static_cast<uint16_t>(count),
                                   values, 20))
        {
            for (int i = 0; i < count; ++i)
            {
                answer += " ";
                answer += String(start + i);
                answer += "=";
                answer += String(values[i]);
            }
            return answer;
        }

        // Some Victor/PowMr firmware rejects a block when even one address in
        // it is unsupported. Fall back to one-register reads and return all
        // readable values instead of failing the whole command.
        answer = "PARTIAL:";
        uint16_t readable = 0;
        uint16_t failed = 0;
        for (int i = 0; i < count; ++i)
        {
            const uint16_t reg = static_cast<uint16_t>(start + i);
            uint16_t value = 0;
            _mCom.clearReadCache();
            if (_mCom.readHoldingBlock(reg, 1, &value, 1))
            {
                answer += " ";
                answer += String(reg);
                answer += "=";
                answer += String(value);
                readable++;
            }
            else
            {
                answer += " ";
                answer += String(reg);
                answer += "=X";
                failed++;
            }
        }

        answer += " [readable=";
        answer += readable;
        answer += " failed=";
        answer += failed;
        answer += "]";
        return answer;
    }

    // Read-only asynchronous PowMr/Victor diagnostic dump.
    // Start with "powmr dump"; retrieve later with "powmr dump result".
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command == "powmr dump result")
    {
        if (_powmrDumpRunning)
        {
            return String("RUNNING: reg=") +
                   static_cast<unsigned int>(_powmrDumpCurrentRegister) +
                   " readable=" + static_cast<unsigned int>(_powmrDumpReadable) +
                   " failed=" + static_cast<unsigned int>(_powmrDumpFailed);
        }
        if (!_powmrDumpReady)
            return "NO RESULT: start with 'powmr dump'";
        return _powmrDumpResult;
    }

    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command == "powmr dump")
    {
        if (_powmrDumpRunning)
            return "RUNNING: PowMr dump already started";

        _powmrDumpReady = false;
        _powmrDumpResult = "";
        _powmrDumpRunning = true;

        TaskHandle_t handle = nullptr;
        if (xTaskCreate(powmrDumpTask, "powmr_dump", 8192, this, 1, &handle) != pdPASS)
        {
            _powmrDumpRunning = false;
            return "ERROR: failed to start PowMr dump task";
        }

        _powmrDumpTask = handle;
        return "STARTED: PowMr BMS/SOC/temperature diagnostic scan; use 'powmr dump result' for progress/result";
    }

    // SOC threshold writes are intentionally disabled until the exact Victor
    // lithium/BMS registers and encoding are confirmed by read-only diagnostics.
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        (command.startsWith("powmr backutility ") ||
         command.startsWith("powmr backbattery ")))
        return "ERROR: SBU SOC writes disabled until registers are verified";

    writeLog("Custom Modbus command unsupported: %s", command.c_str());
    return "UNSUPPORTED";
}

bool MODBUS::forceProtocol(protocol_type_t protocol)
{
    delete device;
    device = nullptr;

    switch (protocol)
    {
    case MODBUS_POWMR:
        device = new PowMr();
        break;
    case MODBUS_DEYE:
        device = new Deye();
        break;
    case MODBUS_SMG_II_11KW:
        device = new SMGII11KW();
        break;
    case MODBUS_SMG:
        device = new SMG();
        break;
    case MODBUS_ANENJI_SRNE:
        device = new AnenjiSrne();
        break;
    case MODBUS_ANENJI:
        device = new Anenji();
        break;
    case MODBUS_MUST:
        device = new MustPV_PH18();
        break;
    default:
        return false;
    }

    device->init(*my_serialIntf, _rxPin, _txPin, _mCom, true);
    stabilizeSerial();
    _mCom.setResponseTimeout(device->getResponseTimeout());
    staticData[DESCR_Device_Model] = device->getName();
    staticData[DESCR_Protocol_ID] = protocolToString(protocol);
    prepareRegisters();
    writeLog("Modbus protocol forced to %s", protocolToString(protocol));
    return true;
}

//----------------------------------------------------------------------
// Private Functions
//----------------------------------------------------------------------
protocol_type_t MODBUS::autoDetect(bool powMrOnly) // function for autodetect the inverter type
{
    protocol_type_t protocol = NoD;
    char modelName[48] = {};
    long activeBaudRate = 0;
    const uint16_t normalResponseTimeout = _mCom.getResponseTimeout();

    writeLog("Try Autodetect Modbus device");
    _mCom.setResponseTimeout(MODBUS_DETECTION_TIMEOUT_MS);

    ModbusDevice *devices[] = { new PowMr(), new Deye(), new SMGII11KW(), new SMG(), new AnenjiSrne(), new Anenji(), new MustPV_PH18()};
    const size_t deviceCount = sizeof(devices) / sizeof(devices[0]);

    for (size_t i = 0; i < deviceCount; ++i)
    {
        modelName[0] = '\0';
        const bool configureSerial = activeBaudRate != devices[i]->getBaudRate();
        devices[i]->init(*my_serialIntf, _rxPin, _txPin, _mCom, configureSerial);
        if (configureSerial)
        {
            activeBaudRate = devices[i]->getBaudRate();
            stabilizeSerial();
        }

        const bool detected = devices[i]->retrieveModel(_mCom, modelName, sizeof(modelName));

        if (detected && modelName[0] != '\0')
        {
            writeLog("<Autodetect> Found Modbus device: %s", modelName);
            staticData["Device_Model"] = modelName;
            
            device = devices[i];
            prepareRegisters();
            protocol = device->getProtocol();
            staticData[DESCR_Protocol_ID] = protocolToString(protocol);

            // Clean up candidates that were not selected. Earlier failures are
            // already deleted and nulled below, so guard against double-free.
            for (size_t j = 0; j < deviceCount; ++j)
            {
                if (j != i && devices[j] != nullptr)
                {
                    delete devices[j];
                    devices[j] = nullptr;
                }
            }
            _mCom.setResponseTimeout(device->getResponseTimeout());
            return protocol;
        }
        delete devices[i];
        devices[i] = nullptr;

        if (powMrOnly)
        {
            break;
        }
    }

    for (size_t i = 0; i < deviceCount; ++i)
    {
        if (devices[i] != nullptr)
        {
            delete devices[i];
            devices[i] = nullptr;
        }
    }

    _mCom.setResponseTimeout(normalResponseTimeout);
    return protocol;
}

void MODBUS::stabilizeSerial()
{
    while (my_serialIntf->available() > 0)
    {
        my_serialIntf->read();
    }

    delay(kSerialStabilizationDelayMs);

    while (my_serialIntf->available() > 0)
    {
        my_serialIntf->read();
    }
}
