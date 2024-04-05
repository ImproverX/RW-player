#define vers "version 11.9"
/* Оптимизация в IDE 1.8.19+ с новыми библиотеками *

  Вариант на "Data Logger Shield" и "LCD Keypad Shield"
  (на первом есть RTC -- используем для показа времени)

  Выход - D3
  Вход  - A1

  "Data Logger Shield V1.0":
  SD-картридер подключен к выводам ардуино:
   MOSI - D11
   MISO - D12
   CLK  - D13
   CS   - D10

  Часы реального времени (RTC DS1307) подключены:
   SDA  - A4
   SDL  - A5

  "LCD Keypad Shield":
  Подключение экрана 1602А:
   LCD RS pin     - D8
   LCD Enable pin - D9
   LCD D4 pin     - D4
   LCD D5 pin     - D5
   LCD D6 pin     - D6
   LCD D7 pin     - D7
   LCD R/W pin    - GND
   LCD 5V pin     - 5V

   Подсветка      - D10

   Кнопки         - A0

   Дополнительно к стандартным требуются библиотеки:
   - RTClib (от Adafruit, библиотека TinyWireM не обязательна)
   - SdFat (от Bill Greiman)
   - TimerOne (от Paul Stoffregen //Jesse Tane...)
*/

// Подгружаемые библиотеки:
#include <LiquidCrystal.h>
#include <SdFat.h>
//#include <sdios.h>    //+++
#include <TimerOne.h>
//#include <Wire.h>    // уже включено в <RTClib.h>
#include <RTClib.h>

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 1
/*
  Set DISABLE_CS_PIN to disable a second SPI device.
  For example, with the Ethernet shield, set DISABLE_CS_PIN
  to 10 to disable the Ethernet controller.
*/
//const int8_t DISABLE_CS_PIN = -1;
const uint8_t SD_CS_PIN = 10;

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)
#define ENABLE_DEDICATED_SPI 0  // чтобы не гас экран при воспроизведении/записи

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS

//инициализация часов
RTC_DS1307 RTC;
// инициализация экрана
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

SdFat32 sd;
File32 dataFile;

char sfileName[13];            // короткое имя текущего файла/директории
uint16_t dirIndex;             // текущая позиция в директории
boolean isDir = false;         // признак того, что текущая позиция -- это директория
boolean isRoot = true;         // признак того, что мы в корне
uint16_t pathIndex[10];        // таблица индексов пути
int Num = 0;                   // номер в таблице индексов пути

unsigned int Nbt;              // размер файла, байт
unsigned int CSF;              // контрольная сумма

volatile byte BUFF[256];       // буфер данных
volatile unsigned int CRB = 0; // индекс чтения из буфера
volatile byte bBit = 15;       // индекс читаемого полубита
volatile unsigned int CWB = 0; // индекс записи в буфер
volatile byte A = 0;           // записываемый байт
volatile byte B = 0;           // записываемый бит

volatile unsigned long PPeriod_sred[2]; // среднее значение границы длинны нечётного полупериода
volatile unsigned long iMicros_old = 0; // предыдущее значение микросекунд
volatile boolean Pik = false;           // есть изменение сигнала
volatile byte PP = 1;                   // =0 -- чётный, =1 -- нечётный полупериод

boolean DT_good = true; // Признак наличия часов

volatile unsigned int Tpp = 256; // Начальная длительность задержки сигнала в микросекундах (один полупериод)
volatile byte Tb = 16;  // Дополнительная задержка сигнала на начало байта в микросекундах
unsigned int TppSTD = 376; // Стандартная длительность задержки сигнала для файлов BAS/MON/ASM...

#define p 3         // номер пина, на который будет вывод сигнала
#define InputPIN A1 // номер пина, на котором будет получение сигнала

const byte BT_none   = 0; // константы -- коды нажатой кнопки
const byte BT_right  = 1;
const byte BT_up     = 2;
const byte BT_down   = 3;
const byte BT_left   = 4;
const byte BT_select = 5;

byte MLevel = 0; // текущий пункт меню
const byte M_play = 0;      // воспроизведение
const byte M_play_in = 10;
//const byte M_dplay = 1;     // воспроизведение в формате DOS
//const byte M_dplay_in = 11;
const byte M_record = 1;    // запись
const byte M_record_in = 11;
const byte M_setup = 2;     // настройки
const byte M_setup_in = 12;

byte PlayROM(int pnt, byte BLs = 0x01); // функция вывода файла ROM, для значения по умолчанию "с первого блока"
byte PlayAll(byte FType, byte StartAddrB = 0); // объявление функции вывода остальных типов файлов, для значения по умолчанию

void setup() {
  //Serial.begin(9600);
  pinMode(InputPIN, INPUT); // объявляем пин как вход (сигнал)
  pinMode(p, OUTPUT);       // объявляем пин как выход (сигнал)
  digitalWrite(p, LOW);     // выход =0
  pinMode(10, OUTPUT);      // CS для SD-картридера и оно же для подсветки экрана
  digitalWrite(10, HIGH);   // включаем подсветку экрана
  pinMode(A0, INPUT);       // объявляем пин с кнопками как вход

  lcd.begin(16, 2);    // объявляем размер экрана 16 символов и 2 строки

  Timer1.initialize(Tpp);               // инициализировать timer1, и установить период Tpp мкс.
  Timer1.attachInterrupt(SendHalfBit);  // прикрепить SendHalfBit(), как обработчик прерывания по переполнению таймера
  Timer1.stop();                        // остановить таймер

  printtextF(F("BEKTOP-MF"), 0);        // вывод названия
  printtextF(F(vers), 1);               // и версии

  //  Wire.begin();                         // уже включено в <RTClib.h>
  RTC.begin();                          // запуск часов
  if (! RTC.isrunning()) {
    //printtextF(F("RTC is NOT run!"), 1);    // часы стоят
    RTC.adjust(DateTime(__DATE__, __TIME__)); // устанавливаем дату/время на момент копмиляции программы
    delay(2000);                        // ждем 2 с
    DateTime now = RTC.now();           // проверяем работу часов
    DT_good = !(now.month() > 12);      // если часы не работают, выставляем признак
  }
  //RTC.adjust(DateTime(__DATE__, __TIME__)); //-- это если понадобится принудительно обновить время

  while (!sd.begin(SD_CONFIG)) {        // SD-карта готова?
    printtextF(F("SD-card failed!"), 1);
    delay(3000);       // ждем 3 с
  }
  //printtextF(F("SD-card is OK."), 1);

  sd.chdir();            // устанавливаем корневую директорию SD

  DIDR1 = (1 << AIN1D) | (1 << AIN0D); // Выключить цифровые входы контактов Digital 6 и 7

  delay(2000);                          // ждем 2с для солидности :-)
}

void loop() {
  byte RCrom = 0;                      // для кода возврата из ПП
  unsigned int StartAddr = 0x100;      // стартовый адрес для MON
  char iFName[13] = "input000.vkt";    // имя файла для записи

  byte button = getPressedButton();     // какая кнопка нажата?
  while (getPressedButton() != BT_none) { // Ждём, пока не будет отпущена кнопка
    delay(50);
  }
  switch (button) // проверяем, что было нажато
  {
    case BT_up:     // вверх
      if (MLevel < 10) {
        if (MLevel > M_play) MLevel--;
        else MLevel = M_setup;
      }
      break;
    case BT_down:   // вниз
      if (MLevel < 10) {
        if (MLevel < M_setup) MLevel++;
        else MLevel = M_play;
      }
      break;
    case BT_right:  // вправо -- вход в меню, запуск действия и т.д.
    case BT_select: // select --//--
      if (MLevel < 10) {
        MLevel += 10;   // заходим в подменю
        switch (MLevel) // выводим надписи меню
        {
          case M_play_in:  // воспроизведение
            //case M_dplay_in: // воспроизведение (DOS)
            printplay();   // вывод надписи "Play file..."
            NextFile();
            break;
          case M_record_in: // запись
            printtextF(F("Record to file:"), 0);
            sd.chdir();     // устанавливаем корневую директорию SD
            while (sd.exists(iFName)) { // ищем первый отсутствующий файл по имени, увеличивая счётчик
              if (iFName[7] < '9') { // увеличиваем единицы
                iFName[7]++;
              }
              else {
                iFName[7] = '0';
                if (iFName[6] < '9') { // увеличиваем десятки
                  iFName[6]++;
                }
                else {
                  iFName[6] = '0'; // увеличиваем сотни
                  iFName[5]++;
                }
              }
            }
            lcd.setCursor(0, 1);
            clrstr(lcd.print(iFName)); // выводим имя файла для записи с очисткой строки до конца
            break;
          case M_setup_in: // настройки
            printtextF(F("Setup"), 0);
            break;
        }
        button = BT_none;
      }
      break;
    case BT_left: // влево
      break;
    //case BT_select: // Возврат в корень меню
    //  MLevel = 0;
    //  break;
    case BT_none: // ничего не нажато
      delay(100);
      break;
  }

  switch (MLevel) // действия в соответствии с текущим пунктом меню
  {
    case M_play: // воспроизведение
      printtextF(F("Play file ->"), 0);
      printtime();
      break;
    //case M_dplay: // воспроизведение (DOS)
    //  printtextF(F("Play to DOS ->"), 0);
    //  printtime();
    //  break;
    case M_record: // запись
      printtextF(F("Record data ->"), 0);
      printtime();
      break;
    case M_setup: // настройки
      printtextF(F("Settings ->"), 0);
      printtime();
      break;
    case M_play_in: // зашли в меню вопроизведения
      switch (button)
      {
        case BT_up: // вверх по файлам
          PrevFile();
          break;
        case BT_down: // вниз по файлам
          NextFile();
          break;
        case BT_left: // в корень или выход
          if (isRoot) {
            MLevel = MLevel - 10; // из корня выходим в стартовое меню
          }
          else {        // возврат в корневую директорию
            cdUp();
            NextFile();
            PrevFile(); // возврат на первый файл
          }
          break;
        case BT_select: // вход в директорию, или запуск файла на воспроизведение (DOS)
        case BT_right: // вход в директорию, или запуск файла на воспроизведение
          if (isDir) { //Если это директория, то переход в неё
            sd.chdir(sfileName); //sd.chdir(sfileName, true);
            if (Num < 9) {
              pathIndex[Num] = dirIndex;
              Num++;
            }
            NextFile();
          }
          else {         // если не директория -- пробуем воспроизвести файл
            if (Nbt != 0xFFFF) { // проверяем размер файла
              RCrom = 11; // для вывода сообщения "неверный формат"
              //printtextF(F("Playing..."), 0);

              byte PNT = 0;
              for (byte i = 0; i <= 12; i++) { // Переводим все буквы имени файла в заглавные
                if ((sfileName[i] >= 0x61) && (sfileName[i] <= 0x7A)) {
                  sfileName[i] &= 0xDF;     // на заглавные буквы
                } else if (sfileName[i] == '.') { // ищем точку в имени файла
                  PNT = i;                  // запоминаем позицию точки
                }
              }

              if (button == BT_select) {   // если вывод в формате DOS
                printtextF(F("[DOS] >>>"), 0);
                RCrom = PlayAll(4);         // вызов ПП для формата DOS
              }
              else {                                // другие форматы
                StartAddr = chr2hex(sfileName[PNT + 3]);
                if (StartAddr > 0x0F) StartAddr++; // если символ определён не верно, то увеличиваем до 0х100
                StartAddr += chr2hex(sfileName[PNT + 2]) * 16; // дополняем вторым символом
                switch (sfileName[PNT + 1])         // следующий после точки символ
                {
                  case 'C': // первый символ == 'c'|'C'
                    if ( (sfileName[PNT + 2] == 'A') & (sfileName[PNT + 3] == 'S') ) { // второй символ == 'a'|'A' и третий == 's'|'S'
                      printtextF(F("[CAS] >>>"), 0);
                      RCrom = PlayAll(0);           // вызов ПП для формата CAS
                      break;
                    } // если не CAS, то далее проверяем на COM/C0M
                  case 'R': // первый символ == 'r'|'R'
                    if ( sfileName[PNT + 3] == 'M' ) { // третий символ == 'm'|'M'
                      if (sfileName[PNT + 2] == 'O') { // второй символ == 'o'|'O'
                        StartAddr = 0x01;              // вывод с первого блока
                      }
                      else if (sfileName[PNT + 2] == '0') { // второй символ == '0'
                        StartAddr = 0x00;              // вывод с нулевого блока
                      }
                    }
                    if (StartAddr < 0x100) { // если стартовый адрес из расширения определён верно
                      printtextF(F("[ROM] >>>"), 0);
                      RCrom = PlayROM(PNT, lowByte(StartAddr)); // вызов ПП для формата ROM с нужного блока
                    }
                    break;
                  case 'V': // первый символ == 'v'|'V'
                    if ( (sfileName[PNT + 2] == 'K') & (sfileName[PNT + 3] == 'T') ) { // второй символ == 'k'|'K' и третий == 't'|'T'
                      printtextF(F("[VKT] >>>"), 0);
                      RCrom = PlayVKT(); // вызов ПП для формата VKT
                    }
                    break;
                  case 'B': // первый символ == 'b'|'B'
                    if ( (sfileName[PNT + 2] == 'A') & (sfileName[PNT + 3] == 'S') ) { // второй символ == 'a'|'A' и третий == 's'|'S'
                      printtextF(F("[BAS] >>>"), 0);
                      RCrom = PlayAll(1);    // вызов ПП для формата BAS(C)
                      break; // выход из case, если это был BAS-файл
                    }
                  case 'M': // первый символ == 'm'|'M' (или 'b'|'B')
                    if ( (sfileName[PNT + 2] == 'O') & (sfileName[PNT + 3] == 'N') ) { // второй символ == 'o'|'O' и третий == 'n'|'N'
                      StartAddr = 0x01; // для расширения MON ставим адрес начала 0x0100
                    }
                    if (StartAddr < 0x100) { // если стартовый адрес из расширения определён верно
                      printtextF(F("[MON] >>>"), 0);
                      RCrom = PlayAll(2, lowByte(StartAddr)); // вызов ПП для формата MON или Бейсика BLOAD
                    }
                    break;
                  case 'A': // первый символ == 'a'|'A'
                    if ( (sfileName[PNT + 2] == 'S') & (sfileName[PNT + 3] == 'M') ) { // второй символ == 's'|'S' и третий == 'm'|'M'
                      printtextF(F("[ASM] >>>"), 0);
                      RCrom = PlayAll(3);    // вызов ПП для формата ASM
                    }
                    break;
                }
              }
            }
            else {
              if (dirIndex == 0) {
                RCrom = 13; // просто ошибка
              }
              else {
                RCrom = 10; // для вывода сообщения "большой файл"
              }
            }

            digitalWrite(p, LOW);             // выход = 0
            switch (RCrom)                    // Проверяем код возврата
            {
              case 0:
                printtextF(F("Done."), 0);          // Всё закончилось успешно.
                break;
              case 1:
                printtextF(F("Stopped"), 0);        // Сообщение об остановке
                while (getPressedButton() != BT_none) { // Ждём, пока не будет отпущена кнопка
                  delay(50);
                }
                break;
              case 10:
                printtextF(F("File is too big"), 0); // большой файл
                break;
              case 11:
                printtextF(F("Unknown format"), 0);  // выбран не ROM/R0M/VKT-файл, либо не найдена метка скорости в VKT
                break;
              case 12:
                printtextF(F("Bad speed mark"), 0);  // метка скорости в VKT не правильная (больше или меньше границы)
                break;
              default:
                printtextF(F("ERROR!"), 0);          // Сообщение об ошибке
            }
            delay(1000);   // ждем 1 с
            printplay();   // вывод надписи "Play file..."
            dataFile.open(sfileName, O_READ);
            printFileName();    // показать имя текущего файла
          }
      }
      break;
    case M_record_in: // зашли в меню записи
      if (button == BT_right) { // нажали кнопку вправо?
        if (dataFile.open(iFName, FILE_WRITE)) { // открываем файл на запись
          printtextF(F("Prepare..."), 0); // готовимся...
          dataFile.write(0xFF);      // пишем байт для создания файла
          dataFile.seekSet(0);       // переход на начало файла
          CRB = 0;                   // Сбрасываем индекс
          printtextF(F("Waiting..."), 0); // ожидание сигнала

          ADCSRA = 0;          // выключить АЦП
          ADCSRB |= (1 << ACME); // включить мультиплексор
          ADMUX = (1 << MUX0)  // включить вход на A1
                  | (1 << REFS1) | (1 << REFS0); // опорное напряжение АЦП (?)

          ACSR  = (1 << ACIE)  // Разрешить прерывание аналогового компаратора
                  | (1 << ACBG);     // внутреннее опорное напряжение
          delay(1);            // ждём немного
          Pik = false;         // сбрасываем первое срабатывание
          while (!Pik) { // Ждём сигнал
            delay(10);   // задержка...
            if (digitalRead(A0) != HIGH) { // кнопка нажата?
              break;                     // прерывание режима записи при нажатии кнопки
            }
          }
          printtextF(F("Recording..."), 0); // сигнал зафиксирован, записываем
          printtextF(F("Bytes:"), 1);
          delay(200);           // задержка для накопления инфы...
          noInterrupts();       // запрет прерываний
          unsigned int CWB_temp = CWB;       // сохраняем CWB во временную переменную
          interrupts();         // разрешение прерываний

          //====================================
          do { // пока есть данные в буфере
            dataFile.write(BUFF[lowByte(CRB)]); // пишем байт из буфера
            if (CRB % 256 == 0) {  // каждый 256-й байт (1 псевдоблок)
              lcd.setCursor(7, 1); // устанавливаем курсор в позицию 8 в строке 1
              lcd.print(CRB);      // количество сохранённых байт
            }
            CRB++;
            if (CWB_temp <= CRB) {// если индекс записи меньше или равен индексу чтения
              delay(300);     // задержка на ввод данных: макс.скорость 160 * 16 полубит * ~117 байт или ~46 байт на минимальной
              noInterrupts(); // запрет прерываний
              CWB_temp = CWB; // сохраняем CWB во временную переменную
              interrupts();   // разрешение прерываний
            }
          }
          while (CWB_temp >= CRB); // если запись на SD обогнала чтение, значит сигнала нет, выходим из цикла
          //=====================================
          ACSR = 0;     // отключаем обработку прерывания компаратора
          ADCSRA = 135; // включить АЦП, установить делитель =128
          Pik = false;  // сбрасываем флаг "есть сигнал"

          Tpp = ((PPeriod_sred[0] + PPeriod_sred[1]) / 384); // расчитываем полупериод для последующего вывода... (((S1+S2)/2)/128)*2/3
          if ((Tpp % 8) < 4) {                           // если остаток от деления на 8 меньше 4
            Tpp = (Tpp / 8) * 8;                         // округляем в меньшую сторону
          }
          else {
            Tpp = (Tpp / 8 + 1) * 8;                   // округляем в большую сторону
          }

          if (CRB > 25) { // проверка -- если было ложное срабатывание, то ничего не пишем.
            dataFile.write(0xFF);            // записываем маркер в файл
            dataFile.write(highByte(Tpp));   // сохраняем скорость в файле
            dataFile.write(lowByte(Tpp));
            if (DT_good) {                   // если часы работают...
              DateTime now = RTC.now();      // получаем текущее время и сохраняем в атрибутах файла
              dataFile.timestamp(T_CREATE, now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
              dataFile.timestamp(T_WRITE, now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
            }
            printtextF(F("Done.  Speed:"), 0);
            lcd.setCursor(7, 1);             // устанавливаем курсор в позицию 8 в строке 1
            lcd.print(CRB);                  // количество сохранённых байт
            lcd.setCursor(13, 0);            // устанавливаем курсор в позицию 13 в строке 0
            lcd.print(Tpp);                  // расчётное значение длинны полупериода для вывода
            lcd.setCursor(0, 0);             // устанавливаем курсор в позицию 0 в строке 0
            if ((PPeriod_sred[1] > (PPeriod_sred[0] + 3840)) | (PPeriod_sred[0] > (PPeriod_sred[1] + 3840))) { // если средние значения слишьком сильно отличаются
              lcd.print(F("BadSig"));           // пишем сообщение о плохом сигнале

              // отладочный вывод инфы о полупериодах
              /*
                printtextF(F(""), 1);
                lcd.setCursor(0, 1);
                lcd.print(PPeriod_sred[0] / 192);
                lcd.setCursor(5, 1);
                lcd.print(PPeriod_sred[1] / 192);
                lcd.setCursor(10, 1);
                lcd.print(CRB);                // количество сохранённых байт
              */
              delay(3000);                   // ждём 3 сек.
            }
            else if ((Tpp < 160) | (Tpp > 512)) { // если скорость вне допустимых пределов
              lcd.print(F("Error?"));           // пишем сообщение о возможной ошибке
              delay(3000);                   // ждём 3 сек.
            }
          }
          else {
            printtextF(F("Canceled."), 0);
            if (!dataFile.remove()) {        // удаляем недозаписанный файл
              delay(1000);
              printtextF(F("Error del.file"), 0);
            }
          }
          dataFile.close();                  // закрываем файл
          Tpp = 256;                         // устанавливаем полупериод на "стандартное" значение
        }
        else {                               // что-то не открылся файл...
          printtextF(F("Error open file"), 0);
        }

        delay(1000);
        MLevel = M_record;                 // переход в корневое меню на пункт записи
      }
      else {
        if (button == BT_left) MLevel = M_record; // нажали кнопку влево? -- переход в корневое меню
      }
      break;
    case M_setup_in: // зашли в меню настроек
      printtextF(F("Period(mks):"), 1);
      switch (button)
      {
        case BT_up:   // вверх
          if (Tpp < 400) Tpp += 8; // увеличиваем скорость
          break;
        case BT_down: // вниз
          if (Tpp > 160) Tpp = Tpp - 8; // уменьшаем скорость
          break;
        case BT_left: // влево
        case BT_right:
        case BT_select:
          MLevel = M_setup; // выход в коневое меню на пункт "настройки"
      }
      lcd.setCursor(12, 1);
      lcd.print(Tpp); // пришем текущее значение скорости
      break;
  }
}

//=================================================================
byte chr2hex(byte Ch1) { // символ (байт) в шестнадцатиричное значение
  if ((Ch1 >= '0') & (Ch1 <= '9')) { // преобразуем символ в HEX
    return (Ch1 - '0');
  }
  else if ((Ch1 >= 'A') & (Ch1 <= 'F')) {
    return (Ch1 - 55);
  }
  return 0xFF;
}

byte getPressedButton()  // функция проверки нажатой кнопки
{
  int buttonValue = analogRead(0);
  //Serial.println(buttonValue);
  if (buttonValue < 60) return BT_right;
  else if (buttonValue < 200) return BT_up;
  else if (buttonValue < 400) return BT_down;
  else if (buttonValue < 600) return BT_left;
  else if (buttonValue < 800) return BT_select;
  return BT_none;
}

void clrstr(byte l) {  // Очистка строки с текущей позиции до конца экрана
  for (byte i = l; i < 16; i++) lcd.print(' ');
}

void printtextF(__FlashStringHelper* text, byte l) {  // Вывод текста на экран в строке l с очисткой строки
  lcd.setCursor(0, l);
  clrstr(lcd.print(text)); // вывод текста и очистка строки до конца
}

void printtime() { // вывод времени и даты
  lcd.setCursor(0, 1); // устанавливаем курсор в позицию 0 в строке 1
  if (DT_good) {                              // если часы работают
    char DT[17];// = "00:00:00   00/00";
    DateTime now = RTC.now();                 // получаем и выводим текущее время и дату
    //sprintf(DT, "%02d:%02d:02d %-2d/%02d", now.hour(), now.minute(), now.second(), now.day(), now.month());
    DT[0] = now.hour() / 10 + '0';            // перевод из целого в символ
    DT[1] = now.hour() % 10 + '0';            // часы
    DT[2] = ':';
    DT[3] = now.minute() / 10 + '0';          // минуты
    DT[4] = now.minute() % 10 + '0';          // минуты
    DT[5] = ':';
    DT[6] = now.second() / 10 + '0';          // секунды
    DT[7] = now.second() % 10 + '0';          // секунды
    DT[8] = ' ';
    DT[9] = ' ';
    DT[10] = ' ';
    DT[11] = now.day() / 10 + '0';            // день
    DT[12] = now.day() % 10 + '0';            // день
    DT[13] = '/';
    DT[14] = now.month() / 10 + '0';          // месяц
    DT[15] = now.month() % 10 + '0';          // месяц
    DT[16] = 0x00;
    lcd.print(DT);                    // выводим время и дату
  }
  else {
    clrstr(lcd.print(millis() / 1000));       // выводим количество секунд с момента влючения ардуины вместо времени и очистка строки после текста
  }
}

void printplay() {
  lcd.setCursor(0, 0);
  clrstr(lcd.print(F("Play file:"))); // вывод с очисткой строки после текста
}

void printFileName() {
  if (dirIndex != 0) {
    if (!dataFile.isOpen()) {
      dataFile.open(sfileName, O_READ);
    }
    dataFile.getSFN(sfileName, 13);    // сохраняем короткое имя файла
    isDir = dataFile.isDir();          // признак директории
    if (dataFile.fileSize() <= 0xFFFE) { // проверка размера файла <=65534 или для VKT без служебной информации будет <=44458
      Nbt = dataFile.fileSize();       // размер файла ОК
    }
    else {
      Nbt = 0xFFFF;                    // слишком большой для загрузки
    }
    dataFile.close();                  // закрываем файл

    lcd.setCursor(0, 1);
    lcd.print(sfileName);
    if (isDir) {
      clrstr(lcd.print('>'));         // если это директория, добавляем в конце символ '>' и очищаем строку
    } else {                          // это не директория
      clrstr(lcd.print(' '));         // очистка строки после текста
      if (Nbt < 0xFFFF) {             // если размер файла в допустимых пределах
        //lcd.setCursor(16, 1);
        if (Nbt >= 1000) {            // если размер больше 999 байт
          //WRB = lcd.print(Nbt / 1024);// подсчёт числа символов
          lcd.setCursor(15 - lcd.print(Nbt / 1024), 1);
          lcd.print(Nbt / 1024);      // вывод размера файла в кБ
          lcd.print('k');
        }
        else {
          //WRB = lcd.print(Nbt);       // подсчёт числа символов
          lcd.setCursor(16 - lcd.print(Nbt), 1);
          lcd.print(Nbt);             // вывод размера файла в байтах
        }
      }
    }
  }
}

void NextFile() { // переход к следующему файлу
  File32 dir;
  dir.openCwd();
  isRoot = dir.isRoot();

  if (dirIndex != 0) {
    dataFile.open(&dir, dirIndex, O_RDONLY);  // читаем данные текущего файла
    dataFile.close();
  }

  boolean OpenOk = false;
  boolean LoopFiles = false;
  do {
    if (dataFile.openNext(&dir, O_RDONLY)) {  // читаем данные текущего файла
      OpenOk = !dataFile.isHidden();
      if (!OpenOk) {
        dataFile.close();
      }
    }
    else {
      if (!LoopFiles) {
        dir.rewindDirectory();// переход к первому файлу в директории
        LoopFiles = true;
      }
      else {
        dir.close();
        NoFiles();
        return;
      }
    }
  }
  while (!OpenOk);
  dir.close();

  dirIndex = dataFile.dirIndex();    // индекс в директории
  printFileName();
}

void PrevFile() { // переход к предыдущему файлу
  File32 dir;
  dir.openCwd();
  isRoot = dir.isRoot();

  uint16_t maxIndex = 0;
  uint16_t prevIndex = 0;
  uint16_t curIndex = 0;

  if (dirIndex != 0) {

    dir.rewindDirectory();                       // переход к первому файлу в директории
    while (dataFile.openNext(&dir, O_RDONLY)) {  // ищем максимальный и предыдущий индексы
      if (!dataFile.isHidden()) {
        curIndex = dataFile.dirIndex();
        if (curIndex > maxIndex) {
          maxIndex = curIndex;
        }
        if ((curIndex < dirIndex) & (curIndex > prevIndex)) {
          prevIndex = curIndex;
        }
      }
      dataFile.close();
    }

    if (prevIndex == 0) {
      prevIndex = maxIndex;
    }

    if (prevIndex != 0) {
      dataFile.open(&dir, prevIndex, O_RDONLY);  // читаем данные текущего файла
      dir.close();
      dirIndex = prevIndex;
      printFileName();
      return;
    }
  }
  dir.close();
  NoFiles();
}

void cdUp() { // переход на директорию выше
  File32 dir;
  sd.chdir();                         // выходим в корень

  if (Num > 0) {
    Num--;
    for (int i = 0; i < Num; i++) {   // по индексам пройденных директорий читаем их имена и переходим по ним
      dir.openCwd();
      dataFile.open(&dir, pathIndex[i], O_RDONLY);
      dataFile.getSFN(sfileName, 13);
      dataFile.close();
      dir.close();
      sd.chdir(sfileName);
    }
  }
}

void NoFiles() {  // вывод надписи "нет файлов"
  isDir = false;
  dirIndex = 0;
  lcd.setCursor(0, 1);
  clrstr(lcd.print(F("- no files -"))); // с очисткой строки после текста
  Nbt = 0xFFFF;                         // слишком большой для загрузки
}

void CalcTb()  // Вычисление значения задержки на начало байта Tb
{
  if (Tpp <= 176) {     // для полупериода меньше или равном 176
    Tb = 88;
  }
  else {
    if (Tpp <= 240) {   // для полупериода от 184 до 240
      Tb = 264 - Tpp;
    }
    else {
      if (Tpp <= 264) { // для полупериода от 248 до 264
        Tb = 16;
      }
      else Tb = 0;      // для полупериода больше 264
    }
  }
}

void SwapTpp() // поменять местами значения переменных Tpp и TppSTD
{
  Tpp = Tpp + TppSTD;     // меняем местами значения Tpp и TppSTD
  TppSTD = Tpp - TppSTD;
  Tpp = Tpp - TppSTD;
  CalcTb();
}

void WaitBuffer() // ожидание очистки буфера при воспроизведении
{
  unsigned int CRB_tmp;
  do {                    // Ждём опустошения буфера
    delay(Tpp / 64);      // задержка на вывод 1 байта (~Tpp*16/1000)
    BUFF[lowByte(CWB + 1)] = 0x00; // обнуляем следующий за последним байт -- иногда и он успевает прочитаться...
    noInterrupts();       // запрет прерываний
    CRB_tmp = CRB;        // сохраняем CRB во временную переменную
    interrupts();         // разрешение прерываний
  }
  while (CRB_tmp < CWB);
  Timer1.stop();  // Останавливаем таймер............
}

byte PlayVKT() // функция вывода файла VKT
{
  delay(1000);                                     // ждем 1 с
  if (dataFile.open(sfileName, O_READ)) {          // открываем файл. Открылся?
    dataFile.seekSet(Nbt - 3);                     // на позицию -3 байт от конца файла
    if (dataFile.read() != 0xFF) return 11;        // метка не найдена
    Tpp = dataFile.read() * 256 + dataFile.read(); // считываем Tpp
    if ((Tpp < 160) | (Tpp > 511)) return 12;      // не правильная метка
    dataFile.seekSet(0);                           // на начало файла
    Nbt = Nbt - 4; // уменьшаем размер данных на метку (4) и предварительно считанные данные (251)

    CRB = 0;   // Сбрасываем индексы.
    bBit = 15;
    CalcTb();  // Вычисляем значение задержки на начало байта Tb

    // Начинаем наполнять буфер
    for (CWB = 0; CWB <= 250; CWB++) { // первые 251 байт
      BUFF[CWB] = dataFile.read();
    }
    CWB = 251;             // продолжаем с 251-го байта буфера

    lcd.setCursor(12, 0);  // выводим на экран общее кол-во "псевдоблоков" по 256 байт
    lcd.print(Nbt >> 8);   // всего данных / 256

    Timer1.setPeriod(Tpp); // Выставляем период таймера
    Timer1.start();        // Запускаем таймер............

    byte RCV = PlayFile(false); // выводим данные из файла без подсчёта контрольной суммы
    if (RCV == 0) WaitBuffer(); // ожидаем окончания вывода
    return RCV;                 // выходим с кодом
  }
  return 3; // ошибка / нет файла
}

byte PlayAll(byte FType, byte StartAddrB) // функция вывода остальных типов файлов
// входные данные: тип файла, начальный адрес. Тип файла это:
//    0=CAS и VKT; 1=BAS(C); 2=MON и BAS(B); 3=ASM; 4=DOS
// Возвращаемые RC:
//    = 0 -- всё ок
//    = 1 -- прерывание по кнопке
//    = 2 -- чтение обогнало запись в буфер
{
  int i;
  byte rcp;
  byte pnta;
  byte EOFd = 0x1A; // символ для дополнения файлов ДОС

  char sName[11];                 // выводимое имя файла
  for (i = 0; i < 9; i++) {       // формируем выводимое имя файла, цикл до точки (макс 9 символ)
    if (sfileName[i] != '.') {
      if (sfileName[i] != 0x7E) { // не тильда
        sName[i] = sfileName[i];  // берём символ имени файла
      }
      else {                      // тильда -- часто встречающийся символ, остальные нужно переименовать вручную на SD
        if (FType != 4) {
          sName[i] = '_';         // меняем тильду на подчёркивание
        }
        else sName[i] = '0';      // для DOS меняем тильду на нолик, иначе при загрузке будет ошибка, DOS поддерживаются только 'A'-'Z','0'-'9'
      }
    }
    else {                        // если точка
      pnta = i;                   // запоминаем длинну имени
      break;                      // выход из цикла
    }
  }

  if (FType == 4) {               // если DOS, то дополняем пробелы и расширение файла
    for (i = pnta; i < 8; i++) {  // пробелы...
      sName[i] = ' ';
    }
    // расширение файла...
    if ( ((sfileName[pnta + 1] == 'R') | (sfileName[pnta + 1] == 'C')) & (sfileName[pnta + 2] == 'O') & (sfileName[pnta + 3] == 'M') ) {
      sName[8] = 'C';             // меняем ROM на СОМ
      EOFd = 0;
    }
    else sName[8] = sfileName[pnta + 1];
    // и последние два символа расширения
    sName[9] = sfileName[pnta + 2];
    sName[10] = sfileName[pnta + 3];
    pnta = 11;                    // выставляем полную длинну имени файла
  }

  delay(1000);            // ждем 1 с
  if (dataFile.open(sfileName, O_READ)) { // открываем файл. Открылся?
    CRB = 0;   // Сбрасываем индексы.
    bBit = 15;

    // Начинаем наполнять буфер
    for (CWB = 0; CWB <= 255; CWB++) { // первые 256 байт
      if ((FType != 3) | ((CWB / 64 == 1) | (CWB / 64 == 3))) { // если не ASM и это 64-127 или 192-255 байты
        BUFF[CWB] = 0;
      }
      else {                                      // иначе, если ASM и это 0-63 или 128-191 байты
        BUFF[CWB] = 0x55;
      }
    }
    CWB = 256;             // продолжаем с 256-го байта буфера

    SwapTpp();             // выставляем стандартную скорость вывода
    Timer1.setPeriod(Tpp); // Выставляем период таймера
    Timer1.start();        // Запускаем таймер............

    delay(10);             // немного ждём, чтобы начался вывод первых байт
    ToBUFF(0xE6);          // синхробайт
    switch (FType)
    {
      case 0: // это CAS
        rcp = PlayFile(false);      // выводим данные из файла без подсчёта контрольной суммы
        if (rcp == 0) WaitBuffer(); // ожидаем окончания вывода
        SwapTpp();                  // восстанавливаем Tpp
        return rcp;                 // выходим с кодом
      case 1: // это BAS(C)
        for (i = 0; i <= 3; i++) ToBUFF(0xD3); // 4x 0xD3
        break;
      case 2: // это MON или BAS(B)
        for (i = 0; i <= 3; i++) ToBUFF(0xD2); // 4x 0xD2
        break;
      case 3: // это ASM
        for (i = 0; i <= 3; i++) ToBUFF(0xE6); // 4x 0xE6
        break;
      case 4: // это DOS
        ToBUFF(0x01);
        ToBUFF(0x00);              // 0x0100 -- начальный адрес (всегда)
        ToBUFF(highByte(Nbt - 1) + 1); // старший байт конечного адреса (нач.адрес+длинна данных)
        ToBUFF(0xFF);              // младший байт конечного адреса (всегда)
        rcp = PlayFile(true);      // выводим байты из файла с подсчётом КС
        if (rcp > 0) {
          SwapTpp();               // восстанавливаем Tpp
          return rcp;              // выходим с кодом
        }
        for (i = lowByte(Nbt - 1); i < 0xFF; i++) {
          ToBUFF(EOFd); // дополняем, если надо, символами конца файла
          CSF += EOFd;  // подсчёт КС
        }
        ToBUFF(lowByte(CSF));      // выводим младший байт КС
    }

    for (i = 0; i < pnta; i++) {   // вывод имени файла
      CSF += sName[i];             // считаем КС
      ToBUFF(sName[i]);            // заносим в буфер очередную букву имени
    }

    // это DOS
    if (FType == 4) {
      ToBUFF(lowByte(CSF));  // выводим младший байт КС (данные + имя файла)
      WaitBuffer();          // ожидаем окончания вывода
      SwapTpp();             // восстанавливаем Tpp
      return 0;              // выходим с кодом 0
    }

    for (i = 0; i <= 2; i++) ToBUFF(0x00); // 3x 0x00

    // это BAS(C)
    if (FType == 1) {
      for (i = 0; i <= 767; i++) ToBUFF(0x55); // 768x 0x55
      ToBUFF(0xE6);                            // синхробайт
      for (i = 0; i <= 2; i++) ToBUFF(0xD3);   // 3x 0xD3
      ToBUFF(0x00);                            // 1x 0x00
      rcp = PlayFile(true);                    // выводим данные из файла с подсчётом КС
      if (rcp == 0) {
        //for (i = 0; i <= 2; i++) ToBUFF(0x00); // 3x 0x00 -- должно быть в конце файлов BAS
        ToBUFF(lowByte(CSF));                  // младший байт КС
        ToBUFF(highByte(CSF));                 // старший байт КС
        WaitBuffer();                          // ожидаем окончания вывода
      }
      SwapTpp();                               // восстанавливаем Tpp
      return rcp;                              // выходим с кодом
    }

    // это MON, BAS(B) или ASM
    for (i = 0; i <= 255; i++) ToBUFF(0x00); // 256x 0x00
    ToBUFF(0xE6);                          // синхробайт

    if (FType == 2) {  // это MON или BAS(B)
      ToBUFF(StartAddrB);                    // старший байт адреса начала записи
      ToBUFF(0x00);                          // младший байт = 0
      ToBUFF(lowByte(StartAddrB + highByte(Nbt - 1))); // старший байт адреса конца записи
      ToBUFF(lowByte(Nbt - 1));              // младший байт адреса конца записи
      rcp = PlayFile(true);                  // выводим данные из файла с подсчётом КС
      if (rcp == 0) {
        ToBUFF(lowByte(CSF));                // младший байт КС
        WaitBuffer();                        // ожидаем окончания вывода
      }
      SwapTpp();                             // восстанавливаем Tpp
      return rcp;                            // выходим с кодом
    }

    // остался только ASM
    ToBUFF(lowByte(Nbt));                    // младший байт длинны записи
    ToBUFF(highByte(Nbt));                   // старший байт длинны записи
    rcp = PlayFile(true);                    // выводим данные из файла с подсчётом КС
    if (rcp == 0) {
      ToBUFF(0xFF);                          // дополняем FFh в конце
      ToBUFF(lowByte(CSF));                  // младший байт КС
      ToBUFF(highByte(CSF));                 // старший байт КС
      WaitBuffer();                          // ожидаем окончания вывода
    }
    SwapTpp();                               // восстанавливаем Tpp
    return rcp;                              // выходим с кодом
  }
  return 3; // ошибка / нет файла
}

byte PlayFile(boolean CSFp) // Функция вывода битов файла
// CSFp признак необходимости подсчёта контрольной суммы файла
{
  byte SB;
  unsigned int CWB_tmp;
  CSF = 0; // обнуляем контрольную сумму
  for (unsigned int i = 0; i < Nbt; i++) { // данные из файла
    SB = dataFile.read();
    ToBUFF(SB);
    if (CSFp) CSF += SB;

    if ((Nbt - i) % 256 == 0) { // если остаток кратен 256
      lcd.setCursor(12, 0);  // выводим на экран кол-во оставшихся "псевдоблоков" по 256 байт
      lcd.print((Nbt - i) >> 8); // остаток данных / 256
      lcd.print(' ');
    }
    if (getPressedButton() != BT_none) { // кнопка нажата?
      Timer1.stop();       // Останавливаем таймер
      dataFile.close();    // закрываем файл
      //SwapTpp();           // восстанавливаем Tpp
      return 1;            // выход из ПП с ошибкой 1.
    }
    noInterrupts();               // запрет прерываний
    CWB_tmp = CRB;                // сохраняем CRB во временную переменную
    interrupts();                 // разрешение прерываний
    if (CWB_tmp > CWB) {   // проверка -- не обогнало ли воспроизведение запись в буфер?
      Timer1.stop();       // Останавливаем таймер
      dataFile.close();    // закрываем файл
      //SwapTpp();           // восстанавливаем Tpp
      return 2;            // выход из ПП с ошибкой 2.
    }
  }
  dataFile.close();        // закрываем файл
  return 0;
}

byte PlayROM(int pnt, byte BLs) // функция вывода файла ROM
// pnt -- позиция точки в имени файла
// BLs -- стартовый блок
{
  delay(1000);           // ждем 1 с

  if (dataFile.open(sfileName, O_READ)) { // открываем файл. Открылся?
    byte BLe = Nbt / 256; // всего блоков
    byte BLt;             // осталось блоков
    byte Nst;             // номер строки
    byte St;              // выводимый байт

    byte CSz = 0x00;      // контрольная сумма заголовка
    byte CSs = 0x00;      // контрольная сумма строки
    byte i;
    byte j;
    unsigned int CRB_tmp;

    if (Nbt % 256 != 0) BLe++; // корректировка количества блоков, если размер файла не кратен 256

    // Заголовок блока
    byte SB[25];
    SB[0] = 0x4E;
    SB[1] = 0x4F;
    SB[2] = 0x44;
    SB[3] = 0x49;
    SB[4] = 0x53;
    SB[5] = 0x43;
    SB[6] = 0x30;
    SB[7] = 0x30;  // NODISK00
    SB[22] = 0x52;
    SB[23] = 0x4F;
    SB[24] = 0x4D; // расширение "ROM"

    for (i = 0; i <= 7; i++) {           // заносим в SB имя файла
      if (i < pnt) {                     // имя ещё не закончилось?
        if (sfileName[i] != 0x7E) {      // не тильда
          SB[i + 14] = sfileName[i];     // заносим символ
        }
        else {
          SB[i + 14] = '_';              // меняем тильду на подчёркивание, иначе это будет русская "Ч"
        }
      }
      else SB[i + 14] = 0x20;            // дополняем пробелами
    }

    uint16_t d, t;
    dataFile.getModifyDateTime(&d, &t);     // Считываем дату файла и сохраняем в заголовке
    SB[8] = FS_DAY(d) / 10 + '0';           // перевод из целого в символ -- день
    SB[9] = FS_DAY(d) % 10 + '0';
    SB[10] = FS_MONTH(d) / 10 + '0';        // месяц
    SB[11] = FS_MONTH(d) % 10 + '0';
    SB[12] = (FS_YEAR(d) % 100) / 10 + '0'; // последние две цифры года
    SB[13] = FS_YEAR(d) % 10 + '0';

    CRB = 0;                        // Сбрасываем индексы.
    CWB = 0;
    bBit = 15;
    CalcTb(); // Вычисляем значение задержки на начало байта Tb

    // Начинаем наполнять буфер
    for (i = 0; i <= 3; i++) {       // преамбула (4*(00H*25+55H*25))
      for (j = 0; j <= 24; j++) {
        BUFF[lowByte(CWB)] = 0x00;
        CWB++;
      }
      for (j = 0; j <= 24; j++) {
        BUFF[lowByte(CWB)] = 0x55;
        CWB++;
      }
    }

    Timer1.setPeriod(Tpp); // Выставляем период таймера
    Timer1.start();        // Запускаем таймер............

    for (BLt = BLe; BLt >= 1; BLt--) { // Вывод блоков данных в цикле
      CSz = BLs;
      CSz += BLe;
      CSz += BLt;

      for (j = 0; j <= 15; j++) ToBUFF(0x00); // 00h*16
      for (j = 0; j <= 3; j++)  ToBUFF(0x55); // 55h*4
      ToBUFF(0xE6);                           // E6h*1
      for (j = 0; j <= 3; j++)  ToBUFF(0x00); // 00h*4

      for (j = 0; j <= 24; j++) {        // заголовок блока
        CSz += SB[j];
        ToBUFF(SB[j]);
      }
      ToBUFF(0x00);
      ToBUFF(0x00);
      ToBUFF(BLs);                       // начальный блок
      ToBUFF(BLe);                       // конечный блок
      ToBUFF(BLt);                       // осталось блоков

      lcd.setCursor(12, 0);              // выводим на экран кол-во оставшихся блоков
      lcd.print(BLt);
      lcd.print(' ');

      ToBUFF(CSz);                       // контр.сумма заголовка

      for (Nst = 0x80; Nst <= 0x87; Nst++) { // вывод строк (8 шт.)
        for (j = 0; j <= 3; j++) ToBUFF(0x00); // 00h*4
        ToBUFF(0xE6);                     // E6h*1
        CSs = Nst;
        ToBUFF(Nst);                      // номер строки
        CSs += CSz;
        ToBUFF(CSz);                      // контр.сумма заголовка

        // начинаем вывод строки данных
        for (j = 0; j <= 31; j++) { // цикл на 32 байта
          if (Nbt > 0) {            // ещё есть данные?
            St = dataFile.read();   // читаем очередной байт из файла
            Nbt--;
          }
          else {                    // нет -- дополняем нулями
            St = 0x00;
          }
          ToBUFF(St);               // передаём считанный байт
          CSs += St;
          if (getPressedButton() != BT_none) { // кнопка нажата?
            Timer1.stop();          // Останавливаем таймер
            dataFile.close();       // закрываем файл
            return 1;               // выход из ПП с ошибкой 1.
          }
          noInterrupts();               // запрет прерываний
          CRB_tmp = CRB;                // сохраняем CRB во временную переменную
          interrupts();                 // разрешение прерываний
          if (CRB_tmp > CWB) {      // проверка -- не обогнало ли чтение запись?
            Timer1.stop();          // Останавливаем таймер
            dataFile.close();       // закрываем файл
            return 2;               // выход из ПП с ошибкой 2.
          }
        }
        ToBUFF(CSs);                // контр.сумма строки
      }
    }
    dataFile.close(); // закрываем файл

    for (j = 0; j <= 31; j++) ToBUFF(0x00); // 00h*32 -- завершение вывода программы (?)
    WaitBuffer();  // ожидаем окончания вывода
    return 0;      // выход из ПП с кодом 0.
  }
  return 3; // ошибка / нет файла
}

void ToBUFF(byte SBb) {     // Подпрограмма записи байта в буфер
  unsigned int CRB_tmp;
  noInterrupts();               // запрет прерываний
  CRB_tmp = CRB;                // сохраняем CRB во временную переменную
  interrupts();                 // разрешение прерываний
  if (CWB > (CRB_tmp + 250)) {  // Если позиция записи больше, чем позиция чтения + размер буфера - 6
    delay(Tpp);                 // Задержка (Tpp*1000 мкс = Tpp мс = 125 байт)
  }
  BUFF[lowByte(CWB)] = SBb;
  CWB++;
}

void SendHalfBit() {  // Подпрограмма вывода полубита по циклу таймера
  byte Pd = PORTD;
  if (bBit & 1) {     // проверка индекса полубитов на чётность
    if (( (Pd >> p) ^ ( BUFF[lowByte(CRB)] >> (bBit >> 1) )) & 1) { // Если состояние порта и выводимый бит разные
      Pd ^= (1 << p); // инвертируем бит в позиции p
    }
  }
  else {              // чётный -- просто инвертируем порт
    Pd ^= (1 << p);   // инвертируем бит в позиции p(=3)
  }
  PORTD = Pd;         // вывод в порт p
  if (bBit > 0) {     // правим счётчики полубитов и байтов
    bBit--;
    if (bBit == 14) Timer1.setPeriod(Tpp); // Выставляем период таймера (биты)
  }
  else {
    bBit = 15;
    CRB++;
    Timer1.setPeriod(Tpp + Tb); // Выставляем увеличенный период таймера (начало байта)
  }
}

ISR(ANALOG_COMP_vect) // ПП обработки прерывания компаратора -- определяет и заносит полученные биты в буфер
{
  unsigned long iMicros = micros();
  unsigned long PPeriod = iMicros - iMicros_old;
  iMicros_old = iMicros;
  if (PPeriod < 65000) {  // началось...
    if (bBit < 16) {      // если это не последний полубит
      if (PPeriod <= (PPeriod_sred[PP] >> 7)) { // sred/128, если тек. полупериод короткий
        if (CWB <= 255) {   // расчёт среднего значения, если меньше 256 байт считано
          PPeriod_sred[PP] = (PPeriod_sred[PP] * CWB + PPeriod * 192) / (CWB + 1); // "среднее значение" = 1,5*128 от короткого полупериода
        }
        if (bBit & 1) { // нечётный полубит
          A = (A << 1) + B; // заносим бит
        }               // нечётный -- пропускаем
      }
      else { // получен длинный полупериод
        B ^= 1;         // инвертируем бит
        A = (A << 1) + B; // заносим бит
        bBit--;         // уменьшаем счётчик полубитов
      }
    }
    else { // если последний полубит, вводим корректировку на задержку между байтами
      // граница будет =([1,5*Tpp*128]*25/19)/128 =~[1,5*Tpp*128]/97 =~1,98*Tpp
      if (PPeriod > (PPeriod_sred[PP] / 97)) { // если тек. полупериод длинный
        B ^= 1;         // инвертируем бит
        A = (A << 1) + B; // заносим бит
        bBit--;         // уменьшаем счётчик полубитов
      }
    }
    // корректировка счётчиков
    PP ^= 1; // инвертируем бит признака чётности полупериода
    if (bBit > 1) {
      bBit--;                  // счётчик полубитов -1
    }
    else {
      BUFF[lowByte(CWB)] = A;  // заносим байт в буфер
      BUFF[lowByte(++CWB)] = 0;// счётчик байтов +1 и обнуляем очередной байт в буфере
      A = 0;                   // обнуляем буфер для битов
      bBit += 15;              // = +16 -1
    }
  }
  else { // был перерыв в сигнале
    PPeriod_sred[0] = 98304; // берём заведомо большое среднее значение (192*512)
    PPeriod_sred[1] = 98304; // берём заведомо большое среднее значение
    CWB = 0;                 // начинаем запись с начала
    bBit = 15;
    A = 0;
    B = 0;
    PP = 1;                 // = нечётный полупериод
    Pik = true;             // есть сигнал!
  }
}
