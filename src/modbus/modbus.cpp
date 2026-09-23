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
        requestStaticData = false;
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

String MODBUS::requestData(String command)
{
    requestStaticData = true;
    command.trim();

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

    // Victor/PowMr lithium SBU controls.
    // In lithium/BMS mode menu 12/13 are SOC thresholds. On this family
    // the writable mirrors are 5025/5026 and values are whole percentages.
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        command.startsWith("powmr output "))
    {
        String mode = command.substring(13);
        mode.toLowerCase();
        int value = -1;
        if (mode == "utility") value = 0;
        else if (mode == "solar") value = 1;
        else if (mode == "sbu") value = 2;
        if (value < 0)
            return "ERROR: allowed output mode is utility/solar/sbu";

        if (!_mCom.writeHoldingRegister(5018, static_cast<uint16_t>(value)))
        {
            const uint8_t result = _mCom.getLastWriteResult();
            return String("ERROR: Modbus write failed result=") +
                   static_cast<unsigned int>(result) + " (" +
                   _mCom.getLastWriteResultText() + ")";
        }
        _mCom.clearReadCache();
        static_info.curr_register = 0;
        return String("OK: Output source priority = ") + mode;
    }

    const bool isBackUtilitySoc = command.startsWith("powmr backutility ");
    const bool isBackBatterySoc = command.startsWith("powmr backbattery ");
    if (device != nullptr && device->getProtocol() == MODBUS_POWMR &&
        (isBackUtilitySoc || isBackBatterySoc))
    {
        const int soc = command.substring(18).toInt();
        if (isBackUtilitySoc && (soc < 5 || soc > 50 || (soc % 5) != 0))
            return "ERROR: back-to-utility SOC must be 5..50% in 5% steps";
        if (isBackBatterySoc && (soc < 60 || soc > 100 || (soc % 5) != 0))
            return "ERROR: back-to-battery SOC must be 60..100% in 5% steps";

        const uint16_t reg = isBackUtilitySoc ? 5025 : 5026;
        if (!_mCom.writeHoldingRegister(reg, static_cast<uint16_t>(soc)))
        {
            const uint8_t result = _mCom.getLastWriteResult();
            return String("ERROR: Modbus write failed result=") +
                   static_cast<unsigned int>(result) + " (" +
                   _mCom.getLastWriteResultText() + ")";
        }
        _mCom.clearReadCache();
        static_info.curr_register = 0;
        requestStaticData = true;
        return String("OK: ") + (isBackUtilitySoc ? "Back to utility SOC = " : "Back to battery SOC = ") +
               String(soc) + "%";
    }

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
