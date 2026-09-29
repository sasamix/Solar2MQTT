const BOARDS = {
  m5stack_atom_lite:{name:"M5Stack ATOM Lite + ATOMIC RS232",chip:"ESP32",file:"m5stack_atom_lite.bin"},
  wemos_d1_mini32:{name:"Wemos D1 Mini32",chip:"ESP32",file:"wemos_d1_mini32.bin"},
  esp32c3_supermini:{name:"ESP32-C3 SuperMini",chip:"ESP32-C3",file:"esp32c3_supermini.bin"},
  esp32s3_supermini:{name:"ESP32-S3 SuperMini",chip:"ESP32-S3",file:"esp32s3_supermini.bin"},
  waveshare_esp32_s3_eth:{name:"Waveshare ESP32-S3 Ethernet",chip:"ESP32-S3",file:"waveshare_esp32_s3_eth.bin"}
};

const TEXT = {
ru:{
subtitle:"Web‑прошивка ESP32",eyebrow:"Browser Flasher",title:"Прошивка Solar2MQTT через браузер",
intro:"Подключите плату по USB, выберите точную модель и запустите установку. Никаких отдельных программ‑флешеров и ручного ввода адресов памяти.",
versionLabel:"Версия",browserLabel:"Web Serial",browserOk:"поддерживается",browserNo:"недоступен",
step1Title:"Выберите плату",step1Text:"Нужна точная аппаратная версия — семейства ESP недостаточно.",
step2Title:"Подключите USB",step2Text:"При необходимости переведите плату в режим загрузчика.",
step3Title:"Нажмите Connect",step3Text:"Выберите COM‑порт и дождитесь завершения прошивки.",
installerEyebrow:"Installer",installerTitle:"Выберите целевую плату",boardLabel:"Плата",boardPlaceholder:"— Выберите плату —",
selectedBoard:"Плата",chipLabel:"Чип",imageLabel:"Образ",warningTitle:"Полная установка",
warningText:"Записывается полный flash‑образ с таблицей разделов и LittleFS backlog. Wi‑Fi/MQTT и другие сохранённые настройки будут сброшены. Для обычного обновления уже настроенного устройства используйте OTA в Web UI Solar2MQTT.",
confirmText:"Я выбрал точную модель платы и понимаю, что это полная прошивка.",
unsupported:"Web Serial недоступен. Откройте страницу в актуальном Chrome или Edge на компьютере.",
selectHint:"Сначала выберите плату.",confirmHint:"Подтвердите точную модель платы, чтобы открыть кнопку прошивки.",
readyHint:"Готово. Нажмите Connect и выберите последовательный порт платы.",
notesTitle:"Что прошивается",notesText:"Флешер использует тот же код и те же PlatformIO‑окружения, что и основной CI Solar2MQTT. Для каждой платы собирается отдельный полный образ. ESP Web Tools дополнительно проверяет семейство подключённого чипа.",
repoLink:"GitHub репозиторий",releaseLink:"Последний Release",versionError:"не определена"
},
en:{
subtitle:"ESP32 Web Flasher",eyebrow:"Browser Flasher",title:"Flash Solar2MQTT from your browser",
intro:"Connect the board over USB, choose the exact hardware model and start installation. No separate flashing utility or manual flash offsets required.",
versionLabel:"Version",browserLabel:"Web Serial",browserOk:"supported",browserNo:"unavailable",
step1Title:"Choose the board",step1Text:"Select the exact hardware variant; the ESP family alone is not enough.",
step2Title:"Connect USB",step2Text:"Put the board into bootloader mode if your hardware requires it.",
step3Title:"Press Connect",step3Text:"Choose the serial port and wait for flashing to finish.",
installerEyebrow:"Installer",installerTitle:"Select your target board",boardLabel:"Board",boardPlaceholder:"— Select board —",
selectedBoard:"Board",chipLabel:"Chip",imageLabel:"Image",warningTitle:"Full installation",
warningText:"This writes the complete flash image including the partition table and LittleFS backlog. Saved Wi‑Fi/MQTT and other settings will be reset. For normal updates of an already configured device, use OTA in the Solar2MQTT Web UI.",
confirmText:"I selected the exact board model and understand this is a full flash.",
unsupported:"Web Serial is unavailable. Open this page in a current desktop Chrome or Edge browser.",
selectHint:"Choose a board first.",confirmHint:"Confirm the exact board model to enable flashing.",
readyHint:"Ready. Press Connect and select the board's serial port.",
notesTitle:"What gets flashed",notesText:"The flasher uses the same source and PlatformIO environments as the main Solar2MQTT CI. Each board gets its own full image. ESP Web Tools also validates the connected chip family.",
repoLink:"GitHub repository",releaseLink:"Latest Release",versionError:"unknown"
}};

const boardSelect=document.querySelector("#board");
const boardInfo=document.querySelector("#board-info");
const selectedBoard=document.querySelector("#selected-board");
const selectedChip=document.querySelector("#selected-chip");
const selectedImage=document.querySelector("#selected-image");
const confirmBoard=document.querySelector("#confirm-board");
const installArea=document.querySelector("#install-area");
const installButton=document.querySelector("#install-button");
const selectHint=document.querySelector("#select-hint");
const versionEl=document.querySelector("#version");
const browserStatus=document.querySelector("#browser-status");

let language=localStorage.getItem("solar2mqtt-flasher-language")||(navigator.language?.toLowerCase().startsWith("ru")?"ru":"en");
let version="";
let manifestUrl="";

function t(key){return TEXT[language][key]??key}

function applyLanguage(){
  document.documentElement.lang=language;
  for(const node of document.querySelectorAll("[data-i18n]")){const value=t(node.dataset.i18n);if(value)node.textContent=value}
  for(const button of document.querySelectorAll("[data-lang]"))button.classList.toggle("active",button.dataset.lang===language);
  browserStatus.textContent=("serial" in navigator)?t("browserOk"):t("browserNo");
  browserStatus.classList.toggle("good","serial" in navigator);
  browserStatus.classList.toggle("bad",!("serial" in navigator));
  updateHint();
}

function updateHint(){
  const board=BOARDS[boardSelect.value];
  selectHint.textContent=!board?t("selectHint"):(!confirmBoard.checked?t("confirmHint"):t("readyHint"));
}

function cleanupManifest(){
  if(manifestUrl){URL.revokeObjectURL(manifestUrl);manifestUrl=""}
}

function setManifest(board){
  cleanupManifest();
  const firmwareUrl=new URL("./firmware/"+board.file,window.location.href).href;
  const manifest={
    name:"Solar2MQTT - "+board.name,
    version:version||"unknown",
    new_install_prompt_erase:false,
    builds:[{chipFamily:board.chip,parts:[{path:firmwareUrl,offset:0}]}]
  };
  manifestUrl=URL.createObjectURL(new Blob([JSON.stringify(manifest)],{type:"application/json"}));
  installButton.setAttribute("manifest",manifestUrl);
}

function refreshBoard(){
  const board=BOARDS[boardSelect.value];
  confirmBoard.checked=false;
  confirmBoard.disabled=!board;
  installArea.classList.add("hidden");
  if(!board){boardInfo.classList.add("hidden");cleanupManifest();updateHint();return}
  selectedBoard.textContent=board.name;
  selectedChip.textContent=board.chip;
  selectedImage.textContent=board.file;
  boardInfo.classList.remove("hidden");
  setManifest(board);
  updateHint();
  const url=new URL(window.location.href);
  url.searchParams.set("board",boardSelect.value);
  history.replaceState(null,"",url);
}

async function loadVersion(){
  try{
    const response=await fetch("./version.json",{cache:"no-store"});
    if(!response.ok)throw new Error("HTTP "+response.status);
    const data=await response.json();
    version=data.version||"";
    versionEl.textContent=version||t("versionError");
    const board=BOARDS[boardSelect.value];if(board)setManifest(board);
  }catch(error){console.error("Unable to load firmware version",error);versionEl.textContent=t("versionError")}
}

for(const button of document.querySelectorAll("[data-lang]")){
  button.addEventListener("click",()=>{
    language=button.dataset.lang;
    localStorage.setItem("solar2mqtt-flasher-language",language);
    applyLanguage();
    const url=new URL(window.location.href);url.searchParams.set("lang",language);history.replaceState(null,"",url);
  });
}
boardSelect.addEventListener("change",refreshBoard);
confirmBoard.addEventListener("change",()=>{installArea.classList.toggle("hidden",!confirmBoard.checked);updateHint()});

const params=new URLSearchParams(window.location.search);
const requestedLang=params.get("lang");if(requestedLang&&TEXT[requestedLang])language=requestedLang;
const requestedBoard=params.get("board");if(requestedBoard&&BOARDS[requestedBoard])boardSelect.value=requestedBoard;

applyLanguage();refreshBoard();loadVersion();
window.addEventListener("beforeunload",cleanupManifest);
