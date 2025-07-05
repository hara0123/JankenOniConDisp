// じゃんけん鬼コントローラのディスプレイと選択LEDを表示させるスケッチ
// 2ビットの信号を読んで、対応した画面を表示する
// 画面はグラフィックデータにすることも可能だが、現在は文字表示のみ

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <efontEnableJaMini.h>
#include <efont.h>
#include <SPI.h>

#include <Adafruit_ZeroTimer.h>

#define SCREEN_WIDTH 160
#define SCREEN_WIDTH 80

// モード名はここで設定
// 各モードの背景色はDrawScreen関数内でハードコーディング
#define MODE1_NAME "カメラ"
#define MODE2_NAME "移　動"
#define MODE3_NAME "ダッシュ"
#define MODE4_NAME "ジャンプ"

// 各ピンの定義
#define DISP_CS_PIN 10
#define DISP_RST_PIN 8
#define DISP_DC_PIN 7

#define CPU_ALREADY_SET_PIN 2
#define CPU_MODE_BIT1_PIN A0
#define CPU_MODE_BIT2_PIN A1

#define MODE_LED1_PIN 3
#define MODE_LED2_PIN 4
#define MODE_LED3_PIN 5
#define MODE_LED4_PIN 6

// 時間に関するdefine
#define ANNOTATION_SHOW_COUNT 250 // この値×10[ms]でアノテーションが消える

enum class OperationMode
{
  Camera, Move, Dash, Jump
};

Adafruit_ST7735 disp_ = Adafruit_ST7735(DISP_CS_PIN, DISP_DC_PIN, DISP_RST_PIN);

Adafruit_ZeroTimer zt3_ = Adafruit_ZeroTimer(3);
void TC3_Handler()
{
  Adafruit_ZeroTimer::timerHandler(3);
}

uint16_t RGB2BGR(uint16_t rgb16);
void DrawScreen(enum class OperationMode mode);
void ModeLEDOn(enum class OperationMode mode);
void ModeCheck();
void DoModeChangedProc();
void DoSerialOutProc();
void DoAnnotationEraseProc();

uint8_t selectedMode_;
int16_t selectedShowTime_;

bool ModeChangedFlag_;
bool SerialOutFlag_;
bool AnnotationEraseFlag_;

char debugBuf_[100];

void setup() {
  // put your setup code here, to run once:
  SerialUSB.begin(115200);

  selectedMode_ = 0;
  selectedShowTime_ = 0;

  debugBuf_[0] = '\0';

  // ディスプレイ初期化
  disp_.initR(INITR_MINI160x80);
  disp_.setRotation(3);

  // タイマ設定
  // モードチェックを10msごとに行う
  // プリスケーラ: DIV1、DIV2、DIV4、DIV8、DIV16、DIV64、DIV256、DIV1024
  // カウンタ: 8BIT、16BIT、32BIT
  // 波形生成: NORMAL_FREQ、MATCH_FREQ、NORMAL_PWM、MATCH_PWM
  // 48MHzを256分周するので48000000/256=187500Hz
  zt3_.configure(TC_CLOCK_PRESCALER_DIV256, TC_COUNTER_SIZE_16BIT, TC_WAVE_GENERATION_MATCH_PWM);

  // コンパレータ設定
  // チャネル: 0、1
  // コンペア値: 16ビットなら最大0xFFFF
  // 10ms周期=100Hzで駆動するので18750/100=1875カウントごとにチェック
  zt3_.setCompare(0, 1875);

  // コールバック設定
  // チャネル: CHANNEL0 or CHANNEL1 -> CHANNEL0
  // コールバック関数: Timer3Callback0
  zt3_.setCallback(true, TC_CALLBACK_CC_CHANNEL0, Timer3Callback0);

  // 割込ループ開始
  zt3_.enable(true);

  // ピンモード設定
  pinMode(CPU_ALREADY_SET_PIN, INPUT);
  pinMode(CPU_MODE_BIT1_PIN, INPUT);
  pinMode(CPU_MODE_BIT2_PIN, INPUT);

  pinMode(MODE_LED1_PIN, OUTPUT);
  pinMode(MODE_LED2_PIN, OUTPUT);
  pinMode(MODE_LED3_PIN, OUTPUT);
  pinMode(MODE_LED4_PIN, OUTPUT);

  ModeChangedFlag_ = false;
  SerialOutFlag_ = false;
  AnnotationEraseFlag_ = false;;

  DrawScreen(OperationMode::Camera);
  ModeLEDOn(OperationMode::Camera);
}

void loop() {
  if(ModeChangedFlag_)
  {
    ModeChangedFlag_ = false;
    DoModeChangedProc();
  }

  if(AnnotationEraseFlag_)
  {
    AnnotationEraseFlag_ = false;
    DoAnnotationEraseProc();
  }

  if(SerialOutFlag_)
  {
    SerialOutFlag_ = false;
    DoSerialOutProc();
  }
}

void DrawScreen(enum class OperationMode mode)
{
  uint16_t bgColor = 0;
  uint16_t wallpapaerColor = disp_.color565(0, 0, 20);
  char modeName[16]; // モード名は最大全角5文字+'\0'、1文字あたり3バイト計算、4バイトの拡張漢字は考慮しない

  switch(mode)
  {
    case OperationMode::Camera:
      bgColor = disp_.color565(128, 0, 128);
      strcpy(modeName, MODE1_NAME);
      break;
    case OperationMode::Move:
      bgColor = disp_.color565(200, 0, 0);
      strcpy(modeName, MODE2_NAME);
      break;
    case OperationMode::Dash:
      bgColor = disp_.color565(0, 128, 0);
      strcpy(modeName, MODE3_NAME);
      break;
    case OperationMode::Jump:
      bgColor = disp_.color565(0, 0, 128);
      strcpy(modeName, MODE4_NAME);
      break;
  }

  // 全体背景
  disp_.fillScreen(RGB2BGR(wallpapaerColor)); // ネイビーブルー

  // ラベル「Mode」の描画
  disp_.setTextColor(RGB2BGR(ST77XX_WHITE));
  disp_.setCursor(6, 8);
  disp_.setTextSize(1);
  disp_.print("Mode");
  disp_.drawLine(0, 19, 160, 19, RGB2BGR(ST77XX_WHITE));

  // モード名の描画
  int16_t x1, y1; // 実際には利用しない
  uint16_t w, h;
  disp_.fillRect(0, 20, 160, 40, RGB2BGR(bgColor));
  disp_.setTextSize(3);
  disp_.getTextBounds(modeName, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (disp_.width() - w) / 2;
  int16_t y = (disp_.height() - h) / 2;
  disp_.setCursor(x, y);
  //disp_.print(modeName);

  int16_t scale = 2;
  // 横開始位置は自前で計算
  int posX = CalcRightPosition(scale, modeName);
  printEfont(posX, 24, scale, RGB2BGR(ST77XX_WHITE), RGB2BGR(bgColor), modeName);

  disp_.drawLine(0, 61, 160, 61, RGB2BGR(ST77XX_WHITE));

  disp_.fillRect(0, 62, 160, 80, RGB2BGR(wallpapaerColor));

  // アノテーションの描画
  String msg = "is Selected.";
  disp_.setTextSize(1);
  disp_.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  x = disp_.width() - 4 - w;
  y = disp_.height() - 6 - h;
  disp_.setCursor(x, y);
  disp_.print(msg);
}

uint16_t RGB2BGR(uint16_t rgb16)
{           
    uint16_t r = (rgb16 & 0xF800) >> 11;
    uint16_t g = rgb16 & 0x07E0;
    uint16_t b = (rgb16 & 0x001F) << 11;

    return b | g | r;
}

// 10msに1回モード確認
void Timer3Callback0()
{
  ModeCheck();

  if(selectedShowTime_ > 0)
  {
    selectedShowTime_--;
    if(selectedShowTime_ == 0)
    {
      // アノテーションを消去
      AnnotationEraseFlag_ = true;
    }
  }
}

void ModeCheck()
{
  if(digitalRead(CPU_ALREADY_SET_PIN) == HIGH)
  {
    // CPU側でモードは設定済み
    uint8_t previousMode = selectedMode_&0x3;
    selectedMode_ <<= 2; // 上位2ビットが履歴

    uint8_t currentMode = digitalRead(CPU_MODE_BIT1_PIN);
    currentMode <<= 1;
    currentMode |= digitalRead(CPU_MODE_BIT2_PIN);
    selectedMode_ |= currentMode;

    // 前回のチェック時から変化があったときのみ出力
    if(previousMode != currentMode)
    {
      sprintf(debugBuf_, "%d -> %d", previousMode, currentMode);
      SerialOutFlag_ = true;

      ModeChangedFlag_ = true;
      selectedShowTime_ = ANNOTATION_SHOW_COUNT; // 消すまでにコールバック関数が呼ばれる回数
    }
  }
}

void DoModeChangedProc()
{
  switch(selectedMode_ & 0x3)
  {
    case static_cast<int>(OperationMode::Camera):
      DrawScreen(OperationMode::Camera);
      ModeLEDOn(OperationMode::Camera);
      break;
    case static_cast<int>(OperationMode::Move):
      DrawScreen(OperationMode::Move);
      ModeLEDOn(OperationMode::Move);
      break;
    case static_cast<int>(OperationMode::Dash):
      DrawScreen(OperationMode::Dash);
      ModeLEDOn(OperationMode::Dash);
      break;
    case static_cast<int>(OperationMode::Jump):
      DrawScreen(OperationMode::Jump);
      ModeLEDOn(OperationMode::Jump);
      break;
    default:
      break;
  }
}

void DoSerialOutProc()
{
  SerialUSB.println(debugBuf_);
}

// "is selected."の表示を消す
void DoAnnotationEraseProc()
{
  uint16_t wallpapaerColor = disp_.color565(0, 0, 20);
  disp_.fillRect(0, 62, 160, 80, RGB2BGR(wallpapaerColor));
}

// 現在選択されているモードを表すLEDを点灯
void ModeLEDOn(OperationMode mode)
{
  switch(mode)
  {
    case OperationMode::Camera:
      digitalWrite(MODE_LED1_PIN, HIGH);
      digitalWrite(MODE_LED2_PIN, LOW);
      digitalWrite(MODE_LED3_PIN, LOW);
      digitalWrite(MODE_LED4_PIN, LOW);
      break;
    case OperationMode::Move:
      digitalWrite(MODE_LED1_PIN, LOW);
      digitalWrite(MODE_LED2_PIN, HIGH);
      digitalWrite(MODE_LED3_PIN, LOW);
      digitalWrite(MODE_LED4_PIN, LOW);
      break;
    case OperationMode::Dash:
      digitalWrite(MODE_LED1_PIN, LOW);
      digitalWrite(MODE_LED2_PIN, LOW);
      digitalWrite(MODE_LED3_PIN, HIGH);
      digitalWrite(MODE_LED4_PIN, LOW);
      break;
    case OperationMode::Jump:
      digitalWrite(MODE_LED1_PIN, LOW);
      digitalWrite(MODE_LED2_PIN, LOW);
      digitalWrite(MODE_LED3_PIN, LOW);
      digitalWrite(MODE_LED4_PIN, HIGH);
      break;
    default:
      break;
  }
}

//efont 文字列をビデオに表示する
//**********************************************************************************************
void printEfont(int16_t x,int16_t y,int16_t txtsize,uint16_t color,uint16_t bgcolor,char *str) {
  int posX = x;
  int posY = y;
  int16_t textsize = txtsize;
  uint16_t textcolor = color;
  uint16_t textbgcolor = bgcolor;
  byte font[32];
  while( *str != 0x00 ){
    // 改行処理
    if( *str == '\n' ){
      // 改行
      posY += 16 * textsize;
      posX += 16 * textsize;
      str++;
      continue;
    }
    // フォント取得
    uint16_t strUTF16;
    str = efontUFT8toUTF16( &strUTF16, str );
    getefontData( font, strUTF16 );
    // 文字横幅
    int width = 16 * textsize;
    if( strUTF16 < 0x0100 ){
      // 半角
      width = 8 * textsize;
    }
    // 背景塗りつぶし
    disp_.fillRect(posX, posY, width, 16 * textsize, textbgcolor);
    // 取得フォントの確認
    for (uint8_t row = 0; row < 16; row++) {
      word fontdata = font[row*2] * 256 + font[row*2+1];
      for (uint8_t col = 0; col < 16; col++) {

        if( (0x8000 >> col) & fontdata ){
          int drawX = posX + col * textsize;
          int drawY = posY + row * textsize;
          if( textsize == 1 ){
            disp_.drawPixel(drawX, drawY, textcolor);
          } else {
            disp_.fillRect(drawX, drawY, textsize, textsize, textcolor);
          }
        }
      }

    }
    // 描画カーソルを進める
    posX += width;
    
    // 折返しは無効にした
    // 折返し処理
    //if( SCREEN_WIDTH <= posX ){ 
    //  posX = 0;
    //  posY += 16 * textsize;
    //}
  }
  // カーソルを更新
  disp_.setCursor(posX, posY);
}

// UTF8の文字数を数える
size_t utf8_strlen(const char* str, int* nZen, int* nHan)
{
  size_t len = 0;
  *nZen = 0; 
  *nHan = 0;
  while (*str) {
    unsigned char c = (unsigned char)*str;
    if ((c & 0x80) == 0) {
      // ASCII（1バイト）
      str++;
      (*nHan)++;
    } else if ((c & 0xE0) == 0xC0) {
      // 2バイト文字（キリル文字やヘブライ文字など）
      str += 2;
    } else if ((c & 0xF0) == 0xE0) {
      // 3バイト文字（ひらがな・漢字）
      str += 3;
      (*nZen)++;
    } else if ((c & 0xF8) == 0xF0) {
      // 4バイト文字（絵文字など）
      str += 4;
    } else {
      // 無効なUTF-8
      str++;
    }
    len++;
  }
  return len;
}

int16_t CalcRightPosition(uint16_t scale, char* name)
{
  const int screenW = 160;
  const int charZenW = 16 * scale;
  const int charHanW = 8 * scale;

  int nHan, nZen;
  utf8_strlen(name, &nZen, &nHan);

  int x;

  if(nHan + nZen * 2 >= 10)
  {
    x = 0; 
  }
  else
  {
    x = (screenW - nZen * charZenW - nHan * charHanW) / 2;
  }

  return x;
}
