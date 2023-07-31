#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <TetrisMatrixDraw.h>
#include <ezTime.h>

#include <SPI.h>
#include <SD.h>

#define E 18
#define B1 27
#define B2 13
#define G1 26
#define G2 12
#define R1 25
#define R2 14

#define SD_SCLK 33
#define SD_MISO 32
#define SD_MOSI 21
#define SD_SS 22

#define ALT 2
#define ENABLE_DOUBLE_BUFFER 1
SPIClass spi = SPIClass(HSPI);

String sd_status = "";
bool card_mounted = false;

uint8_t matrix_brightness = 60;

File gif_file;
AnimatedGIF gif;
MatrixPanel_I2S_DMA *matrix_display = nullptr;

uint16_t myBLACK = matrix_display->color565(0, 0, 0);
uint16_t myWHITE = matrix_display->color565(255, 255, 255);
uint16_t myRED = matrix_display->color565(255, 0, 0);
uint16_t myGREEN = matrix_display->color565(0, 255, 0);
uint16_t myBLUE = matrix_display->color565(0, 0, 255);
uint16_t myYELLOW = matrix_display->color565(255, 255, 0);
uint16_t myMAGENTA = matrix_display->color565(255, 0, 255);
uint16_t myCYAN = matrix_display->color565(0, 255, 255);

const int panelResX = 128;
const int panelResY = 32;
const int panels_in_X_chain = 1;
const int panels_in_Y_chain = 1;
const int totalWidth = panelResX * panels_in_X_chain;
const int totalHeight = panelResY * panels_in_Y_chain;
int16_t xPos = 0, yPos = 0;

String th_filePath = "";
bool allowPlaying = true;
bool isPlayable = false;

int16_t xOne, yOne;
uint16_t w, h;

int textXPosition = panelResX * panels_in_X_chain;
int textYPosition = panelResY / 2 - 7;

unsigned long milliseconds = 0;
unsigned long previousMillidecondsKnob = 0;
int knobMillisecondInterval = 8;
uint8_t wheelval = 0;

uint8_t colorWheelBorderOffset = 0;
unsigned long isAnimationDue;
int delayBetweeenAnimations = 23;

TaskHandle_t Task1, Task2, Task3;

bool twelvehour = false;
bool forceRefresh = true;
unsigned long animationDue = 0;
unsigned long animationDelay = 100;
TetrisMatrixDraw tetris(*matrix_display);
TetrisMatrixDraw tetris2(*matrix_display);
TetrisMatrixDraw tetris3(*matrix_display);
Timezone myTZ;
unsigned long oneSecondLoopDue = 0;
bool showColon = true;
volatile bool finishedAnimating = false;
String lastDisplayedTime = "";
String lastDisplayedAmPm = "";
const int tetrisYOffset = totalHeight / 2;
const int tetrisXOffset = panelResX / 2;

void animationHandler() {
  if (!finishedAnimating) {
    matrix_display->fillScreen(0);
    if (twelvehour) {
      bool tetris1Done = false;
      bool tetris2Done = false;
      bool tetris3Done = false;

      tetris1Done = tetris.drawNumbers(-6 + tetrisXOffset, 10 + tetrisYOffset, showColon);
      tetris2Done = tetris2.drawText(56 + tetrisXOffset, 9 + tetrisYOffset);

      if (tetris2Done) {
        tetris3Done = tetris3.drawText(56 + tetrisXOffset, -1 + tetrisYOffset);
      }

      finishedAnimating = tetris1Done && tetris2Done && tetris3Done;

    } else {
      finishedAnimating = tetris.drawNumbers(2 + tetrisXOffset, 10 + tetrisYOffset, showColon);
    }
    matrix_display->flipDMABuffer();
  }
}

void clockScreenSaver() {
  unsigned long now = millis();
  if (now > oneSecondLoopDue) {
    setMatrixTime();
    showColon = !showColon;

    if (finishedAnimating) {
      handleColonAfterAnimation();
    }
    oneSecondLoopDue = now + 1000;
  }
  now = millis();
  if (now > animationDue) {
    animationHandler();
    animationDue = now + animationDelay;
  }
}

void setMatrixTime() {
  String timeString = "";
  String AmPmString = "";
  if (twelvehour) {
    timeString = myTZ.dateTime("g:i");

    if (timeString.length() == 4) {
      timeString = " " + timeString;
    }

    AmPmString = myTZ.dateTime("A");
    if (lastDisplayedAmPm != AmPmString) {
      lastDisplayedAmPm = AmPmString;
      tetris2.setText("M", forceRefresh);
      tetris3.setText(AmPmString.substring(0, 1), forceRefresh);
    }
  } else {
    timeString = myTZ.dateTime("H:i");
  }

  if (lastDisplayedTime != timeString) {
    lastDisplayedTime = timeString;
    tetris.setTime(timeString, forceRefresh);

    finishedAnimating = false;
  }
}

void handleColonAfterAnimation() {
  uint16_t colour =  showColon ? tetris.tetrisWHITE : tetris.tetrisBLACK;
  int x = twelvehour ? -6 : 2;
  x = x + tetrisXOffset;
  int y = 10 + tetrisYOffset - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  tetris.drawColon(x, y, colour);
  matrix_display->flipDMABuffer();
}

void setup(void) {
  Serial.begin(115200);
  while (!Serial) {
  }

  Serial.print("Initializing LED...");

  HUB75_I2S_CFG mxconfig(panelResX, panelResY, panels_in_X_chain);

  mxconfig.gpio.e = E;
  mxconfig.gpio.r1 = R1;
  mxconfig.gpio.r2 = R2;

  if (digitalRead(ALT) == HIGH) {
    mxconfig.gpio.b1 = B1;
    mxconfig.gpio.b2 = B2;
    mxconfig.gpio.g1 = G1;
    mxconfig.gpio.g2 = G2;
  } else {
    mxconfig.gpio.b1 = G1;
    mxconfig.gpio.b2 = G2;
    mxconfig.gpio.g1 = B1;
    mxconfig.gpio.g2 = B2;
  }

  mxconfig.clkphase = false;

  matrix_display = new MatrixPanel_I2S_DMA(mxconfig);
  matrix_display->begin();
  matrix_display->clearScreen();
  matrix_display->setBrightness8(matrix_brightness);
  matrix_display->setTextWrap(false);

  Serial.print("Initializing SD card...");

  spi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_SS);
  if (SD.begin(SD_SS, spi, 8000000)) {
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("No SD card");
      sd_status = "No card";
    } else {
      card_mounted = true;
      Serial.print("SD Card Type: ");
      if (cardType == CARD_MMC) {
        Serial.println("MMC");
      } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
      } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
      } else {
        Serial.println("UNKNOWN");
      }
      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      Serial.printf("SD Card Size: %lluMB\n", cardSize);
      sd_status = String(cardSize) + "MB";
    }
  } else {
    Serial.println("Card Mount Failed");
    sd_status = "Mount Failed";
  }

  myTZ.setLocation(F("pl"));

  Serial.println("Initializing Tetris...");
  tetris.display = matrix_display;
  tetris2.display = matrix_display;
  tetris3.display = matrix_display;
  finishedAnimating = false;
  tetris.scale = 2;

  Serial.println("Initializing multitasking...");

  xTaskCreatePinnedToCore(GifThread, "GifThreadTask", 5000, NULL, 2, &Task1, 0);
  xTaskCreatePinnedToCore(TextThread, "TextThreadTask", 5000, NULL, 2, &Task2, 0);
  xTaskCreatePinnedToCore(ClockThread, "ClockThreadTask", 5000, NULL, 2, &Task3, 0);
}

void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void loop(void) {
  String cmd;
  cmd = getCommand();

  if (cmd == "/tetris") {
    Serial.println("Drawing Tetris clock");
    hideText();
    stopGif();
    showClock();
    matrix_display->clearScreen();
    // ///////////////////////////////////////////////////
  } else if (cmd.startsWith("/time")) {
    String hour = getArg(cmd, 1);
    String minute = getArg(cmd, 2);
    String second = getArg(cmd, 3);
    String day = getArg(cmd, 4);
    String month = getArg(cmd, 5);
    String year = getArg(cmd, 6);

    if (hour == "" || minute == "" || second == "" || day == "" || month == "" || year == "") {
      Serial.println("Invalid time");
    } else {
      Serial.printf("Setting time to %s:%s:%s %s/%s/%s\n", hour.c_str(), minute.c_str(), second.c_str(), day.c_str(), month.c_str(), year.c_str());
      setTime(hour.toInt(), minute.toInt(), second.toInt(), day.toInt(), month.toInt(), year.toInt());
    }
    // ///////////////////////////////////////////////////
  } else if (cmd == "/rainbow") {
    hideClock();
    stopGif();
    Serial.println("Drawing rainbow");
    showText();
    matrix_display->clearScreen();
    // ///////////////////////////////////////////////////
  } else if (cmd.startsWith("/gif")) {
    hideClock();
    hideText();
    Serial.println("Drawing GIF");
    String path = getArg(cmd, 1);
    if (path == "") {
      Serial.println("No path specified");
    } else {
      Serial.printf("Path: %s\n", path.c_str());
      gif_file = SD.open(path.c_str());
      if (!gif_file) {
        Serial.println("Failed to open GIF file");
      } else {
        playGif(path.c_str());
        matrix_display->clearScreen();
      }
    }
    // ///////////////////////////////////////////////////
  } else if (cmd.startsWith("/ls")) {
    String path = getArg(cmd, 1);
    Serial.printf("Listing directory: %s\n", path.c_str());

    if (path == "") {
      Serial.println("No path specified");
    } else {
      File root = SD.open(path.c_str());
      if (!root) {
        Serial.println("Failed to open directory");
      } else {
        printDirectory(root, 0);
        root.close();
      }
    }
    // ///////////////////////////////////////////////////
  }
}

String getArg(String cmd, int argNum) {
  int index = 0;
  int argCount = 0;

  while (argCount < argNum) {
    index = cmd.indexOf(' ', index + 1);
    if (index == -1) {
      return "";
    }
    argCount++;
  }

  int nextIndex = cmd.indexOf(' ', index + 1);
  if (nextIndex == -1) {
    nextIndex = cmd.length();
  }

  return cmd.substring(index + 1, nextIndex);
}

String new_command = "";
String current_command = "";

String getCommand() {
  if (!Serial.available()) {
    return "";
  }

  new_command = Serial.readStringUntil('\n');
  new_command.trim();

  if (new_command == current_command) {
    return "";
  }

  return new_command;
}

void playGif(String filePath) {
  th_filePath = filePath;
  isPlayable = true;
  allowPlaying = false;
}

void stopGif() {
  isPlayable = false;
  allowPlaying = false;
}

void GifThread(void *parameter) {
  while (true) {
    if (
      isPlayable && (strncmp(th_filePath.c_str(), "/", 1) == 0) && (strlen(th_filePath.c_str()) != 0) && SD.exists(th_filePath)) {
      showGIF(th_filePath.c_str());
      continue;
    }
    delay(1);
  }
}

bool text = false;

void TextThread(void *parameter) {
  while (true) {
    milliseconds = millis();

    if (!text) {
      delay(1);
      continue;
    }

    if (milliseconds - previousMillidecondsKnob < knobMillisecondInterval) {
      delay(1);
      continue;
    }

    previousMillidecondsKnob = milliseconds;
    drawBorderRainbow(2, 64);
    printTextRainbow(wheelval, "COIN-OP ARCADE HEAVEN", 1, 1);
    scrollText(wheelval, "TESTING MODIFIED ESP32 WEB2RGBMATRIX CODE WITH BATOCERA ON ASUS CHROMEBOX i7");
    printTextRainbow(wheelval, "RETRO ARCADE PARADISE", 2, 24);
    wheelval += 4;

    delay(1);
  }
}

bool clck = false;

void ClockThread(void *parameter)
{
  while (true) {
    if (!clck) {
      delay(1);
      continue;
    }

    clockScreenSaver();
    delay(1);
  }
}

void showClock() {
  clck = true;
}

void hideClock() {
  clck = false;
}

void showText() {
  text = true;
}

void hideText() {
  text = false;
}

void showGIF(const char *name) {
  matrix_display->setBrightness8(matrix_brightness);
  matrix_display->clearScreen();

  if (gif.open(name, GIFSDOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    GIFINFO pInfo;
    gif.getInfo(&pInfo);
    Serial.printf("[INFO] Showing %s GIF for %d seconds\n", name, pInfo.iDuration / 1000);
    int i = 0;
    do {
      i++;
    } while (allowPlaying && gif.playFrame(true, NULL));
    if (!allowPlaying) {
      allowPlaying = true;
    }
    gif.close();
  }
}

void showTextLine(String text) {
  int char_width = 6;
  int char_heigth = 8;
  int x = (totalWidth - (text.length() * char_width)) / 2;
  int y = (totalHeight - char_heigth) / 2;

  String hexcolor = "#0313fc";
  hexcolor.replace("#", "");
  char charbuf[8];
  hexcolor.toCharArray(charbuf, 8);
  long int rgb = strtol(charbuf, 0, 16);
  byte r = (byte)(rgb >> 16);
  byte g = (byte)(rgb >> 8);
  byte b = (byte)(rgb);

  matrix_display->clearScreen();
  matrix_display->setBrightness8(matrix_brightness);
  matrix_display->setTextColor(matrix_display->color565(r, g, b));
  matrix_display->setCursor(x, y);
  matrix_display->println(text);
}

void setText(String text) {
  String hexcolor = "#0313fc";
  hexcolor.replace("#", "");
  char charbuf[8];
  hexcolor.toCharArray(charbuf, 8);
  long int rgb = strtol(charbuf, 0, 16);
  byte r = (byte)(rgb >> 16);
  byte g = (byte)(rgb >> 8);
  byte b = (byte)(rgb);

  matrix_display->clearScreen();
  matrix_display->setBrightness8(matrix_brightness);
  matrix_display->setTextColor(matrix_display->color565(r, g, b));
  matrix_display->setCursor(0, 0);
  matrix_display->println(text);
}

void span(uint16_t *src, int16_t x, int16_t y, int16_t width) {
  if (x >= totalWidth) {
    return;
  }

  int16_t x2 = x + width - 1;

  if (x2 < 0) {
    return;
  }

  if (x < 0) {
    width += x;
    src -= x;
    x = 0;
  }

  if (x2 >= totalWidth) {
    width -= (x2 - totalWidth + 1);
  }

  while (x <= x2) {
    int16_t xOffset = (totalWidth - gif.getCanvasWidth()) / 2;
    matrix_display->drawPixel((x++) + xOffset, y, *src++);
  }
}

void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y;

  y = pDraw->iY + pDraw->y;

  int16_t screenY = yPos + y;

  if ((screenY < 0) || (screenY >= totalHeight)) {
    return;
  }

  usPalette = pDraw->pPalette;

  s = pDraw->pPixels;

  if (pDraw->ucHasTransparency) {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + pDraw->iWidth;
    x = 0;
    iCount = 0;
    while (x < pDraw->iWidth) {
      c = ucTransparent - 1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) {
          s--;
        } else {
          *d++ = usPalette[c];
          iCount++;
        }
      }
      if (iCount) {
        span(usTemp, xPos + pDraw->iX + x, screenY, iCount);
        x += iCount;
        iCount = 0;
      }

      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
          iCount++;
        else
          s--;
      }
      if (iCount) {
        x += iCount;
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;

    for (x = 0; x < pDraw->iWidth; x++) {
      usTemp[x] = usPalette[*s++];
    }

    span(usTemp, xPos + pDraw->iX, screenY, pDraw->iWidth);
  }
}

void *GIFOpenFile(const char *fname, int32_t *pSize) {
  Serial.print("Playing gif: ");
  Serial.println(fname);
  gif_file = LittleFS.open(fname);

  if (gif_file) {
    *pSize = gif_file.size();
    return (void *)&gif_file;
  }

  return NULL;
}

void *GIFSDOpenFile(const char *fname, int32_t *pSize) {
  Serial.print("Playing gif from SD: ");
  Serial.println(fname);
  gif_file = SD.open(fname);

  if (gif_file) {
    *pSize = gif_file.size();
    return (void *)&gif_file;
  }

  return NULL;
}

void GIFCloseFile(void *pHandle) {
  File *gif_file = static_cast<File *>(pHandle);

  if (gif_file != NULL) {
    gif_file->close();
  }
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead;
  iBytesRead = iLen;
  File *gif_file = static_cast<File *>(pFile->fHandle);

  if ((pFile->iSize - pFile->iPos) < iLen) {
    iBytesRead = pFile->iSize - pFile->iPos - 1;
  }

  if (iBytesRead <= 0) {
    return 0;
  }

  iBytesRead = (int32_t)gif_file->read(pBuf, iBytesRead);
  pFile->iPos = gif_file->position();
  return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  int i = micros();
  File *gif_file = static_cast<File *>(pFile->fHandle);
  gif_file->seek(iPosition);
  pFile->iPos = (int32_t)gif_file->position();
  i = micros() - i;
  return pFile->iPos;
}


void drawBorderRainbow(int sPeed, int colors) {
  matrix_display->drawRect(0, 0, matrix_display->width(), matrix_display->height(), colorWheel(colors + colorWheelBorderOffset));
  colorWheelBorderOffset += sPeed;
}

void drawBorder(uint16_t color) {
  int w = matrix_display->width();
  matrix_display->drawLine(w, 0, 0, 0, color);
}

void printTextRainbow(int colorWheelOffset, const char *text, int xPos, int yPos) {
  uint8_t w = 0;
  matrix_display->setTextSize(1);

  matrix_display->setCursor(xPos, yPos);
  for (w = 0; w < strlen(text); w++) {
    matrix_display->setTextColor(colorWheel((w * 32) + colorWheelOffset));
    matrix_display->print(text[w]);
  }
}

void printText(uint16_t color, const char *text, int xPos, int yPos) {
  matrix_display->setTextSize(1);
  matrix_display->setCursor(xPos, yPos);
  const char *str = text;
  matrix_display->setTextColor(color);
  matrix_display->print(str);
}

void scrollText(int colorWheelOffset, const char *text) {
  const char *str = text;
  byte offSet = 23;
  unsigned long now = millis();
  if (now > isAnimationDue) {

    matrix_display->setTextSize(2);
    isAnimationDue = now + delayBetweeenAnimations;

    textXPosition -= 1;

    matrix_display->getTextBounds(str, textXPosition, textYPosition, &xOne, &yOne, &w, &h);
    if (textXPosition + w <= 0) {
      textXPosition = matrix_display->width() + offSet;
    }

    matrix_display->setCursor(textXPosition, textYPosition);
    matrix_display->drawRect(0, textYPosition, 2, 14, myBLACK);
    matrix_display->fillRect(0, textYPosition, 127, 14, myBLACK);
    uint8_t w = 0;
    for (w = 0; w < strlen(str); w++) {
      matrix_display->setTextColor(colorWheel((w * 32) + colorWheelOffset));
      matrix_display->print(str[w]);
    }

    matrix_display->flipDMABuffer();
  }
}

void scrollString(String str) {
  matrix_display->getTextBounds(str, textXPosition, textYPosition, &xOne, &yOne, &w, &h);
  int yoff = 1;

  matrix_display->setTextSize(2);

  for (int16_t x = matrix_display->width(); x >= 0 - w; x--) {
    matrix_display->clearScreen();
    matrix_display->setTextColor(myMAGENTA);
    matrix_display->setCursor(x, yoff);

    matrix_display->print(str);
    delay(25);
  }
  matrix_display->flipDMABuffer();
}

void drawText(int colorWheelOffset) {
  matrix_display->setTextSize(1);

  matrix_display->setCursor(5, 0);
  uint8_t w = 0;
  const char *str = "Hi Welcome!";
  for (w = 0; w < strlen(str); w++) {
    matrix_display->setTextColor(colorWheel((w * 32) + colorWheelOffset));
    matrix_display->print(str[w]);
  }
}

uint16_t colorWheel(uint8_t pos) {
  if (pos < 85) {
    return matrix_display->color565(pos * 3, 255 - pos * 3, 0);
  } else if (pos < 170) {
    pos -= 85;
    return matrix_display->color565(255 - pos * 3, 0, pos * 3);
  } else {
    pos -= 170;
    return matrix_display->color565(0, pos * 3, 255 - pos * 3);
  }
}
