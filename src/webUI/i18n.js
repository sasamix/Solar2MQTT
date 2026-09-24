(function () {
  "use strict";

  if (window.I18n) return;

  const resources = {
    en: {},
    ru: {
      "Solar2MQTT Status": "Solar2MQTT — Состояние",
      "Solar2MQTT Menu": "Solar2MQTT — Меню",
      "Solar2MQTT - Network Setup": "Solar2MQTT — Настройка сети",
      "Solar2MQTT MQTT": "Solar2MQTT — MQTT",
      "Solar2MQTT Device": "Solar2MQTT — Устройство",
      "Solar2MQTT Firmware": "Solar2MQTT — Прошивка",
      "Solar2MQTT Debug": "Solar2MQTT — Диагностика",
      "Solar2MQTT Web Serial": "Solar2MQTT — Веб-консоль",
      "Solar2MQTT - Inverter Settings": "Solar2MQTT — Настройки инвертора",

      "Menu": "Меню",
      "Settings": "Настройки",
      "Back": "Назад",
      "Save": "Сохранить",
      "Apply": "Применить",
      "Refresh": "Обновить",
      "Close": "Закрыть",
      "Connect": "Подключить",
      "Loading...": "Загрузка...",
      "None": "Нет",
      "Current": "Текущее значение",
      "Result": "Результат",

      "Energy Overview": "Энергия",
      "Inverter Overview": "Инвертор",
      "Battery": "Батарея",
      "Load": "Нагрузка",
      "Solar": "Солнце",
      "Charge": "Заряд",
      "Discharge": "Разряд",
      "Waiting for data": "Ожидание данных",
      "Waiting for inverter data.": "Ожидание данных инвертора.",
      "No live data yet": "Пока нет текущих данных",
      "Battery data unavailable": "Данные батареи недоступны",
      "Load data unavailable": "Данные нагрузки недоступны",
      "Solar data unavailable": "Данные солнечных панелей недоступны",
      "Charge data unavailable": "Данные заряда недоступны",
      "Temperature": "Температура",
      "Inverter": "Инвертор",
      "Mode": "Режим",
      "Grid": "Сеть",
      "Grid L1": "Сеть L1",
      "Grid L2": "Сеть L2",
      "Grid L3": "Сеть L3",
      "AC Out L1": "Выход AC L1",
      "AC Out L2": "Выход AC L2",
      "AC Out L3": "Выход AC L3",

      "Network Settings": "Настройки сети",
      "MQTT Settings": "Настройки MQTT",
      "Device Settings": "Настройки устройства",
      "Inverter Settings": "Настройки инвертора",
      "Firmware Update": "Обновление прошивки",
      "Debug Tools": "Диагностика",
      "Web Serial": "Веб-консоль",

      "Network Setup": "Настройка сети",
      "Available Networks": "Доступные сети",
      "Refresh networks": "Обновить список сетей",
      "Network Configuration": "Настройка сети",
      "Advanced Network Settings": "Расширенные настройки сети",
      "Hide Advanced Network Settings": "Скрыть расширенные настройки сети",
      "Prefer Ethernet when available": "Предпочитать Ethernet, если он доступен",
      "If an Ethernet link is available, wired networking is used first. Wi-Fi remains available as fallback.": "Если доступен Ethernet, сначала используется проводная сеть. Wi‑Fi остаётся резервным подключением.",
      "Device Name": "Имя устройства",
      "Lock to selected BSSID (Mesh/Steering off)": "Привязать к выбранному BSSID (отключить Mesh/Steering)",
      "Password": "Пароль",
      "Fallback": "Резервная сеть",
      "Fallback SSID (optional)": "Резервный SSID (необязательно)",
      "Password (optional)": "Пароль (необязательно)",
      "Static IP (optional)": "Статический IP (необязательно)",
      "Static IP": "Статический IP",
      "Subnet Mask": "Маска подсети",
      "Gateway": "Шлюз",
      "DNS Server": "DNS-сервер",
      "WebUI Security (optional)": "Защита WebUI (необязательно)",
      "Username": "Имя пользователя",
      "Selected network: none": "Выбранная сеть: нет",
      "Enter password": "Введите пароль",
      "Enter password before connecting.": "Введите пароль перед подключением.",
      "(hidden)": "(скрыта)",

      "MQTT Configuration": "Настройка MQTT",
      "Connection": "Подключение",
      "MQTT Host": "MQTT-сервер",
      "Port": "Порт",
      "User (optional)": "Пользователь (необязательно)",
      "Base topic": "Базовый топик",
      "Trigger topic": "Топик триггера",
      "Send interval [s]": "Интервал отправки [с]",
      "0 means immediate publish mode. Values greater than 0 limit state publishes to the configured interval.": "0 означает немедленную публикацию. Значения больше 0 ограничивают публикацию состояния указанным интервалом.",
      "MQTT is active when a broker host is configured.": "MQTT активен, когда указан адрес брокера.",
      "Security": "Безопасность",
      "Use SSL/TLS": "Использовать SSL/TLS",
      "Payload": "Данные",
      "Enable JSON mode": "Включить режим JSON",
      "Publishes grouped JSON payloads instead of only single values. This cannot be enabled together with Home Assistant discovery.": "Публикует сгруппированные JSON-данные вместо отдельных значений. Нельзя использовать одновременно с обнаружением Home Assistant.",
      "Integrations": "Интеграции",
      "Enable Home Assistant discovery": "Включить обнаружение Home Assistant",
      "Discovery topics are only published when this switch is enabled. Enabling this will switch JSON mode off.": "Топики обнаружения публикуются только при включённом переключателе. При включении режим JSON будет отключён.",

      "UART / RS232/ RS485": "UART / RS232 / RS485",
      "Inverter Protocol": "Протокол инвертора",
      "Auto detect": "Автоопределение",
      "PI protocol": "Протокол PI",
      "Modbus protocol": "Протокол Modbus",
      "Manual selection skips protocol detection. Use Auto detect unless the inverter is detected incorrectly.": "Ручной выбор отключает автоопределение протокола. Используйте автоопределение, если инвертор определяется правильно.",
      "UART RX GPIO": "GPIO UART RX",
      "UART TX GPIO": "GPIO UART TX",
      "RS485 DIR GPIO": "GPIO направления RS485",
      "Poll Interval [ms]": "Интервал опроса [мс]",
      "Temperature Sensors": "Датчики температуры",
      "DS18B20 GPIO": "GPIO DS18B20",
      "Use -1 to disable. Multiple DS18B20 sensors can share the same pin.": "Укажите -1 для отключения. Несколько датчиков DS18B20 могут использовать один GPIO.",
      "Status LED": "Индикатор состояния",
      "Status LED GPIO": "GPIO индикатора",
      "Status LED Brightness": "Яркость индикатора",
      "Live Link": "Текущее подключение",
      "Protocol": "Протокол",
      "Detected Model": "Определённая модель",
      "Firmware": "Прошивка",
      "Build": "Сборка",
      "PV Power": "Мощность PV",
      "Battery Voltage": "Напряжение батареи",
      "Report Working Device": "Сообщить о поддерживаемом устройстве",
      "Opening report form": "Открытие формы",
      "Opening report form...": "Открытие формы...",

      "Firmware Management": "Управление прошивкой",
      "Firmware File": "Файл прошивки",
      "Upload Firmware": "Загрузить прошивку",
      "Check for Updates": "Проверить обновления",
      "Install Update": "Установить обновление",
      "Restart Device": "Перезагрузить устройство",
      "Export Settings": "Экспорт настроек",
      "Import Settings": "Импорт настроек",
      "Download": "Скачать",

      "Debug": "Диагностика",
      "Command": "Команда",
      "Send Command": "Отправить команду",
      "Command Answer": "Ответ команды",
      "Data Preview": "Просмотр данных",
      "Refresh Data": "Обновить данные",
      "BMS / SOC Snapshot": "Снимок BMS / SOC",
      "Register Watch": "Отслеживание регистров",
      "PowMr Dump": "Дамп PowMr",
      "Loopback Test": "Тест Loopback",

      "Web Serial": "Веб-консоль",
      "Connect Serial": "Подключить консоль",
      "Disconnect": "Отключить",
      "Clear": "Очистить",

      "Priorities and Modes": "Приоритеты и режимы",
      "Output mode": "Режим выхода",
      "UTI — Utility first": "UTI — сначала сеть",
      "SUB — Solar → Utility → Battery": "SUB — солнце → сеть → батарея",
      "SBU — Solar → Battery → Utility": "SBU — солнце → батарея → сеть",
      "Charging priority": "Приоритет зарядки",
      "Utility first": "Сначала сеть",
      "Solar first": "Сначала солнце",
      "Solar and Utility": "Солнце и сеть",
      "Solar only": "Только солнце",
      "Input voltage range": "Диапазон входного напряжения",
      "Appliances": "Бытовая техника",
      "Battery type / BMS": "Тип батареи / BMS",
      "LIB — lithium, no BMS protocol": "LIB — литий, без протокола BMS",
      "Output frequency": "Частота выхода",
      "Output voltage": "Выходное напряжение",
      "Charging Currents": "Токи зарядки",
      "Maximum charging current, A": "Максимальный ток зарядки, А",
      "Maximum utility charging current, A": "Максимальный ток зарядки от сети, А",
      "Battery Voltages": "Напряжения батареи",
      "Switch to utility / Recharge, V": "Переход на сеть / Recharge, В",
      "Return to battery / Redischarge, V": "Возврат на батарею / Redischarge, В",
      "Low DC cut-off voltage, V": "Нижнее напряжение отключения, В",
      "Equalization": "Выравнивание",
      "For LIL/PYLON these parameters normally should not be used for manual charge control. They are shown because the registers are known.": "Для LIL/PYLON эти параметры обычно не следует использовать для ручного управления зарядом. Они отображаются, потому что регистры известны.",
      "Equalization voltage, V": "Напряжение выравнивания, В",
      "Equalization time, min": "Время выравнивания, мин",
      "Equalization timeout, min": "Тайм-аут выравнивания, мин",
      "Equalization interval, days": "Интервал выравнивания, дней",
      "Reload values": "Обновить значения",
      "Save changes": "Сохранить изменения",
      "Only known parameters are shown. Saving writes only changed fields; every write is verified by reading the register back.": "Показываются только известные параметры. При сохранении записываются только изменённые поля; каждая запись проверяется чтением регистра обратно.",
      "This page is available only when MODBUS_POWMR is the active protocol.": "Эта страница доступна только при активном протоколе MODBUS_POWMR.",
      "Values loaded from inverter.": "Значения загружены из инвертора.",
      "No changes.": "Изменений нет.",
      "Saved:": "Сохранено:",
      "ERROR:": "ОШИБКА:",

      "Network settings saved.": "Настройки сети сохранены.",
      "MQTT settings applied.": "Настройки MQTT применены.",
      "Device settings applied.": "Настройки устройства применены.",
      "Command answer received.": "Ответ команды получен.",
      "Command sent. No answer received yet.": "Команда отправлена. Ответ пока не получен.",
      "Firmware uploaded. Restart is being prepared.": "Прошивка загружена. Подготавливается перезагрузка.",
      "Settings imported. Restart is being prepared.": "Настройки импортированы. Подготавливается перезагрузка.",
      "Loopback test started.": "Тест Loopback запущен.",
      "Ethernet connected": "Ethernet подключён",
      "AP mode active": "Режим точки доступа активен",
      "WiFi offline": "Wi‑Fi отключён",
      "MQTT connected": "MQTT подключён",
      "MQTT offline": "MQTT отключён",
      "Inverter connected": "Инвертор подключён",
      "Inverter offline": "Инвертор отключён",
      "Status": "Состояние",

      "Firmware & Backup": "Прошивка и резервная копия",
      "Online Update": "Онлайн-обновление",
      "Check for updates from GitHub and install the latest OTA package.": "Проверить обновления на GitHub и установить последний OTA-пакет.",
      "Update Now": "Обновить сейчас",
      "Manual Firmware Update": "Ручное обновление прошивки",
      "Select a firmware file (*.ota) and start the update.": "Выберите файл прошивки (*.ota) и запустите обновление.",
      "The device will reboot automatically after a successful upload.": "После успешной загрузки устройство автоматически перезагрузится.",
      "Choose Firmware (.ota)": "Выбрать прошивку (.ota)",
      "Backup & Restore": "Резервная копия и восстановление",
      "Download all settings as JSON or restore from a previously saved file.": "Скачать все настройки в JSON или восстановить их из ранее сохранённого файла.",
      "On restore the current settings are overwritten and the device may reboot if required.": "При восстановлении текущие настройки будут перезаписаны; при необходимости устройство перезагрузится.",
      "Download configuration": "Скачать конфигурацию",
      "Restore configuration": "Восстановить конфигурацию",
      "Back to menu": "Назад в меню",
      "Command Sender": "Отправка команд",
      "Send": "Отправить",
      "Actions": "Действия",
      "HA Discovery": "Обнаружение HA",
      "Download Report": "Скачать отчёт",
      "Refresh Preview": "Обновить просмотр",
      "SOC History": "История SOC",
      "Reboot": "Перезагрузить",
      "Loopback Status": "Состояние Loopback",
      "Loaded automatically ...": "Загружено автоматически ...",
      "Loading ...": "Загрузка ...",
      "Browser Console": "Консоль браузера",
      "Copy Log": "Копировать лог",
      "Live log output via the `/webserialws` WebSocket endpoint.": "Вывод журнала в реальном времени через WebSocket `/webserialws`.",
      "Send Command": "Отправить команду",
      "Apply": "Применить",
"Placeholder": "Сообщение",
      "Use": "Укажите",
      "to disable. Multiple DS18B20 sensors can share the same pin.": "для отключения. Несколько датчиков DS18B20 могут использовать один GPIO.",
      "means immediate publish mode. Values greater than": "означает режим немедленной публикации. Значения больше",
      "limit state publishes to the configured interval.": "ограничивают публикацию состояния указанным интервалом.",
      "Select a firmware file (*.ota) and start the update. The device will reboot automatically after a successful upload.": "Выберите файл прошивки (*.ota) и запустите обновление. После успешной загрузки устройство автоматически перезагрузится.",
      "Download all settings as JSON or restore from a previously saved file. On restore the current settings are overwritten and the device may reboot if required.": "Скачайте все настройки в JSON или восстановите их из ранее сохранённого файла. При восстановлении текущие настройки будут перезаписаны; при необходимости устройство перезагрузится.",
            "Switch language": "Переключить язык"
    }
  };

  const dynamicRules = [
    [/^Selected network: none$/, () => "Выбранная сеть: нет"],
    [/^Selected network: (.+)$/, (m) => "Выбранная сеть: " + m[1]],
    [/^Wi-Fi password for (.+)$/, (m) => "Пароль Wi‑Fi для " + m[1]],
    [/^Last updated: (.+)$/, (m) => "Обновлено: " + m[1]],
    [/^Max (.+)$/, (m) => "Макс. " + m[1]],
    [/^Now (.+)$/, (m) => "Сейчас " + m[1]]
  ];

  const textOriginal = new WeakMap();
  const attrOriginal = new WeakMap();
  let applying = false;

  function detectLanguage() {
    const saved = localStorage.getItem("solar2mqtt.lang");
    if (saved === "ru" || saved === "en") return saved;

    const langs = Array.isArray(navigator.languages) && navigator.languages.length
      ? navigator.languages
      : [navigator.language || "en"];

    return langs.some((lang) => String(lang).toLowerCase().startsWith("ru")) ? "ru" : "en";
  }

  let language = detectLanguage();

  function translateString(source, lang = language) {
    if (source == null) return source;
    const text = String(source);
    if (lang === "en") return text;

    const normalized = text.replace(/\s+/g, " ").trim();
    const direct = resources.ru[text] !== undefined ? resources.ru[text] : resources.ru[normalized];
    if (direct !== undefined) return direct;

    for (const [rule, render] of dynamicRules) {
      const match = normalized.match(rule);
      if (match) return render(match);
    }
    return text;
  }

  function shouldSkipNode(node) {
    const parent = node?.nodeType === Node.ELEMENT_NODE ? node : node?.parentElement;
    if (!parent) return false;
    return Boolean(parent.closest("script, style"));
  }

  function translateTextNode(node) {
    if (!node || node.nodeType !== Node.TEXT_NODE || shouldSkipNode(node)) return;
    const raw = node.nodeValue;
    if (!raw || !raw.trim()) return;

    const leading = raw.match(/^\s*/)?.[0] || "";
    const trailing = raw.match(/\s*$/)?.[0] || "";
    const core = raw.trim();

    let original = textOriginal.get(node);
    if (!original || (!applying && raw !== translateString(original, language) && core !== translateString(original, language))) {
      original = core;
      textOriginal.set(node, original);
    }

    const translated = translateString(original, language);
    const next = leading + translated + trailing;
    if (node.nodeValue !== next) node.nodeValue = next;
  }

  function translateAttributes(element) {
    if (!(element instanceof Element) || shouldSkipNode(element)) return;
    const attrs = ["title", "aria-label", "placeholder"];

    let originals = attrOriginal.get(element);
    if (!originals) {
      originals = {};
      attrOriginal.set(element, originals);
    }

    for (const attr of attrs) {
      if (!element.hasAttribute(attr)) continue;
      const raw = element.getAttribute(attr) || "";
      if (!originals[attr] || (!applying && raw !== translateString(originals[attr], language))) {
        originals[attr] = raw;
      }
      const translated = translateString(originals[attr], language);
      if (raw !== translated) element.setAttribute(attr, translated);
    }
  }

  function translateTree(root) {
    if (!root) return;
    applying = true;
    try {
      if (root.nodeType === Node.TEXT_NODE) {
        translateTextNode(root);
        return;
      }

      if (root.nodeType === Node.ELEMENT_NODE) translateAttributes(root);

      const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
      let node = walker.currentNode;
      while (node) {
        if (node.nodeType === Node.TEXT_NODE) translateTextNode(node);
        else translateAttributes(node);
        node = walker.nextNode();
      }

      if (document.title) {
        const sourceTitle = document.documentElement.dataset.i18nOriginalTitle || document.title;
        document.documentElement.dataset.i18nOriginalTitle = sourceTitle;
        document.title = translateString(sourceTitle, language);
      }
    } finally {
      applying = false;
    }
  }

  function renderLanguageButton() {
    let button = document.getElementById("languageToggleBtn");
    if (!button) {
      const header = document.getElementById("header");
      if (!header) return;
      button = document.createElement("button");
      button.id = "languageToggleBtn";
      button.type = "button";
      button.className = "language-toggle";
      button.addEventListener("click", () => setLanguage(language === "ru" ? "en" : "ru"));
      header.appendChild(button);
    }

    button.textContent = language === "ru" ? "EN" : "RU";
    button.title = translateString("Switch language", language);
    button.setAttribute("aria-label", button.title);
  }

  function setLanguage(lang) {
    if (lang !== "ru" && lang !== "en") return;
    language = lang;
    localStorage.setItem("solar2mqtt.lang", lang);
    document.documentElement.lang = lang;
    translateTree(document.body);
    renderLanguageButton();
    window.dispatchEvent(new CustomEvent("solar2mqtt-language-changed", { detail: { language: lang } }));
  }

  const observer = new MutationObserver((mutations) => {
    if (applying) return;
    for (const mutation of mutations) {
      if (mutation.type === "characterData") translateTextNode(mutation.target);
      if (mutation.type === "attributes") translateAttributes(mutation.target);
      mutation.addedNodes?.forEach((node) => translateTree(node));
    }
  });

  window.I18n = {
    resources,
    t: translateString,
    getLanguage: () => language,
    setLanguage,
    apply: () => translateTree(document.body)
  };

  document.documentElement.lang = language;

  document.addEventListener("DOMContentLoaded", () => {
    translateTree(document.body);
    renderLanguageButton();
    observer.observe(document.body, {
      subtree: true,
      childList: true,
      characterData: true,
      attributes: true,
      attributeFilter: ["title", "aria-label", "placeholder"]
    });
  });
})();
