// じゃんけん鬼コントローラのディスプレイと選択LEDを表示させるスケッチ
// 2ビットの信号を読んで、対応した画面を表示する
// 画面はグラフィックデータにすることも可能だが、現在は文字表示のみ

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

#include <Adafruit_ZeroTimer.h>

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
#define ANNOTATION_SHOW_COUNT 300 // この値×10[ms]でアノテーションが消える

enum class OperationMode
{
  System, Camera, Move, Jump
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

  DrawScreen(OperationMode::System);
  ModeLEDOn(OperationMode::System);
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
  String modeName = "";

  switch(mode)
  {
    case OperationMode::System:
      bgColor = disp_.color565(128, 128, 0);
      modeName = "System";
      break;
    case OperationMode::Camera:
      bgColor = disp_.color565(128, 0, 0);
      modeName = "Camera";
      break;
    case OperationMode::Move:
      bgColor = disp_.color565(0, 128, 0);
      modeName = "Move";
      break;
    case OperationMode::Jump:
      bgColor = disp_.color565(0, 0, 128);
      modeName = "Jump";
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
  disp_.print(modeName);
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
    case static_cast<int>(OperationMode::System):
      DrawScreen(OperationMode::System);
      ModeLEDOn(OperationMode::System);
      break;
    case static_cast<int>(OperationMode::Camera):
      DrawScreen(OperationMode::Camera);
      ModeLEDOn(OperationMode::Camera);
      break;
    case static_cast<int>(OperationMode::Move):
      DrawScreen(OperationMode::Move);
      ModeLEDOn(OperationMode::Move);
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

void DoAnnotationEraseProc()
{
  uint16_t wallpapaerColor = disp_.color565(0, 0, 20);
  disp_.fillRect(0, 62, 160, 80, RGB2BGR(wallpapaerColor));
}

void ModeLEDOn(OperationMode mode)
{
  switch(mode)
  {
    case OperationMode::System:
      digitalWrite(MODE_LED1_PIN, HIGH);
      digitalWrite(MODE_LED2_PIN, LOW);
      digitalWrite(MODE_LED3_PIN, LOW);
      digitalWrite(MODE_LED4_PIN, LOW);
      break;
    case OperationMode::Camera:
      digitalWrite(MODE_LED1_PIN, LOW);
      digitalWrite(MODE_LED2_PIN, HIGH);
      digitalWrite(MODE_LED3_PIN, LOW);
      digitalWrite(MODE_LED4_PIN, LOW);
      break;
    case OperationMode::Move:
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