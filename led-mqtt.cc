// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// This code is public domain
// (but note, once linked against the led-matrix library, this is
// covered by the GPL v2)

#include "led-matrix.h"
#include "threaded-canvas-manipulator.h"
#include "transformer.h"
#include "graphics.h"
#include "mqtt/async_client.h"
#include "jsoncpp/json/json.h" // sudo aptitude install libjsoncpp-dev

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
// #include <stdlib.h>
#include <unistd.h>
#include <algorithm>

#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>

#include <unordered_map>

#define MQTT_SERVER "tcp://mqtt.munichmakerlab.de:1883"
#define MQTT_TOPIC "mumalab/room/ledpanel/#"
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_CLIENTID "led-mqtt-client"
#define LED_TEXT1 "Welcome to the MunichMakerLab!"
#define LED_TEXT2 "We love LEDs!"
#define PANEL_ROWS 32
#define PANEL_CHAINS 4
#define PANEL_PARALLEL 1
#define PANEL_PWM_BITS 5
#define PANEL_BRIGHTNESS 100
#define PANEL_ROTATION 0
#define PANEL_LARGE_DISPLAY false
#define DO_LUMINANCE_CORRECT true
bool as_daemon = false;

using std::min;
using std::max;

using namespace rgb_matrix;

typedef std::unordered_map<std::string, std::string> params_map;

struct text {
  std::string content;
  int scroll_offset;
  Color color;
  int speed;
  Font font;
  int x;
  int y;
};

/*
 * The following are demo image generators. They all use the utility
 * class ThreadedCanvasManipulator to generate new frames.
 */

class TextScroller : public ThreadedCanvasManipulator {
public:
  TextScroller(RGBMatrix *m, std::string text1)
  : ThreadedCanvasManipulator(m),
    text1_(text1),
    text2_(""),
    once1_(""),
    once2_(""),
    color1_(Color(0,0,255)),
    color2_(Color(0,0,255)),
    speed1_(16),
    speed2_(10),
    matrix_(m),
    offscreen_(matrix_->CreateFrameCanvas()),
    screen_height(matrix_->transformer()->Transform(offscreen_)->height()),
    screen_width(matrix_->transformer()->Transform(offscreen_)->width())
  { 
    font1_.LoadFont("fonts/10x20.bdf");
    font2_.LoadFont("fonts/7x14.bdf");

    scroll_offset1_ = calc_offset_(text1_, font1_);
    scroll_offset2_ = calc_offset_(text2_, font2_);

    x1_ = screen_width - 1;
    x2_ = screen_width - 1;
    y1_ = font1_.baseline();
    y2_ = screen_height;
  }

  void Run() {
    typedef std::chrono::steady_clock::time_point timepoint_t;
    timepoint_t now = std::chrono::steady_clock::now();
    timepoint_t time_next1 = now + std::chrono::milliseconds(speed1_);
    timepoint_t time_next2 = now + std::chrono::milliseconds(speed2_);
    
    while (running()) {
      matrix_->transformer()->Transform(offscreen_)->Clear();
      if (!once1_.empty()) {
        DrawText(matrix_->transformer()->Transform(offscreen_), font1_, x1_, y1_, color1_, once1_.c_str());
      } 
      else if (!textleft1_.empty() || !textmid1_.empty() || !textright1_.empty()) {
        if (!textleft1_.empty()) {
          DrawText(matrix_->transformer()->Transform(offscreen_), font1_, 1, y1_, color1_, textleft1_.c_str());
        }
        if (!textmid1_.empty()) {
          DrawText(matrix_->transformer()->Transform(offscreen_), font1_, (screen_width % 2), y1_, color1_, textleft1_.c_str());
        }
        if (!textright1_.empty()) {
          DrawText(matrix_->transformer()->Transform(offscreen_), font1_, screen_width, y1_, color1_, textleft1_.c_str());
        }
      }
      else {
        DrawText(matrix_->transformer()->Transform(offscreen_), font1_, x1_, y1_, color1_, text1_.c_str());
      }

      if (!once2_.empty()) {
        DrawText(matrix_->transformer()->Transform(offscreen_), font2_, x2_, y2_, color2_, once2_.c_str());
      } 
      else if (!textleft2_.empty() || !textmid2_.empty() || !textright2_.empty()) {
        if (!textleft2_.empty()) {
          DrawText(matrix_->transformer()->Transform(offscreen_), font2_, 1, y2_, color2_, textleft2_.c_str());
        }
        if (!textmid2_.empty()) {
          DrawText(matrix_->transformer()->Transform(offscreen_), font2_, (screen_width % 2), y2_, color2_, textmid1_.c_str());
        }
        if (!textright2_.empty()) {
          DrawText(matrix_->transformer()->Transform(offscreen_), font2_, screen_width, y2_, color2_, textright2_.c_str());
        }
      }
      else {
        DrawText(matrix_->transformer()->Transform(offscreen_), font2_, x2_, y2_, color2_, text2_.c_str());
      }
      
      //std::this_thread::yield();
      offscreen_ = matrix_->SwapOnVSync(offscreen_);
      //std::this_thread::yield();
      now = std::chrono::steady_clock::now();
      if (time_next1 < time_next2) {
        if (time_next1 - std::chrono::milliseconds(2) > now)
          std::this_thread::sleep_for(time_next1 - now);
      } 
      else {
        if (time_next2 - std::chrono::milliseconds(2) > now)
          std::this_thread::sleep_for(time_next2 - now);
      }
      if (now >= time_next1) {
        time_next1 = now + std::chrono::milliseconds(speed1_);
        --x1_;
        if (x1_ < scroll_offset1_) {
          if (!once1_.empty()) {
            once1_ = "";
            scroll_offset1_ = calc_offset_(text1_, font1_) - 1;
          }
          x1_ = canvas()->width() - 1;
        }
      }
      if (now >= time_next2) {
        time_next2 = now + std::chrono::milliseconds(speed2_);
        --x2_;
        if (x2_ < scroll_offset2_) {
          if (!once2_.empty()) {
            once2_ = "";
            scroll_offset2_ = calc_offset_(text2_, font2_) - 1;
          }
          x2_ = canvas()->width() - 1;
        }
      }
    }
  }

  void set_option(const std::string key, const std::string value) {
    if (key == "text1" || key == "text") {
      text1_ = value;
      scroll_offset1_ = calc_offset_(text1_, font1_) - 1;
    }
    if (key == "text2") {
      text2_ = value;
      scroll_offset2_ = calc_offset_(text2_, font2_) - 1;
    }
    if (key == "textleft1") {
      textleft1_ = value;
      scroll_offset1_ = 0;
    }
    if (key == "textmid1") {
      textmid1_ = value;
      scroll_offset1_ = 0;
    }
    if (key == "textright1") {
      textright1_ = value;
      scroll_offset1_ = 0;
    }
    if (key == "textleft2") {
      textleft2_ = value;
      scroll_offset1_ = 0;
    }
    if (key == "textmid2") {
      textmid2_ = value;
      scroll_offset1_ = 0;
    }
    if (key == "textright2") {
      textright2_ = value;
      scroll_offset1_ = 0;
    }
    if (key == "once1" || key == "once") {
      once1_ = value;
      x1_ = canvas()->width() - 1;
      scroll_offset1_ = calc_offset_(once1_, font1_) - 1;
    }
    if (key == "once2") {
      once2_ = value;
      x2_ = canvas()->width() - 1;
      scroll_offset2_ = calc_offset_(once2_, font2_) - 1;
    }
    if (key == "color1" || key == "color") {
      color1_ = transform_color(value);
    }
    if (key == "color2") {
      color2_ = transform_color(value);
    }
    if (key == "speed1" || key == "speed") {
      speed1_ = std::stoi(value);
    }
    if (key == "speed2") {
      speed2_ = std::stoi(value);
    }
    if (key == "font1" || key == "font") {
      font1_.LoadFont(("fonts/" + value + ".bdf").c_str());
    }
    if (key == "font2") {
      font2_.LoadFont(("fonts/" + value + ".bdf").c_str());
    }
  }

private:
  
  int calc_offset_(std::string str, Font& f) {
    int so = 0;
    for(char& c : str) {
      so += f.CharacterWidth(c);
    }
    /*const char *p = t;
    while(*p != '\0') {
      so += font_.CharacterWidth(p[0]);
      ++p;
    }*/
    return so * (-1);
  }
  
  Color transform_color(const std::string& str) {
    std::size_t pos = 0;
    std::string s = str;
    try {
      pos = s.find(",");
      int r = std::stoi(s.substr(0, pos));
      s = s.substr(pos + 1);
      pos = s.find(",");
      int g = std::stoi(s.substr(0, pos));
      int b = std::stoi(s.substr(pos + 1));
      return (Color(r, g, b));
    }
    catch (std::string& s) {
      std::cout << "caught it: \"" << s << "\"" << std::endl;
      return (Color(0, 0, 255));
    }
  }
  
  std::string text1_;
  std::string text2_;
  std::string once1_;
  std::string once2_;
  std::string textleft1_;
  std::string textmid1_;
  std::string textright1_;
  std::string textleft2_;
  std::string textmid2_;
  std::string textright2_;  
  int scroll_offset1_;
  int scroll_offset2_;
  Color color1_;
  Color color2_;
  int speed1_;
  int speed2_;
  Font font1_;
  Font font2_;
  RGBMatrix* matrix_;
  FrameCanvas* offscreen_;
  const int screen_height;
  const int screen_width;
  int x1_;
  int x2_;
  int y1_;
  int y2_;
};



// Simple generator that pulses through RGB and White.
class ColorPulseGenerator : public ThreadedCanvasManipulator {
public:
  ColorPulseGenerator(RGBMatrix *m) : ThreadedCanvasManipulator(m), matrix_(m) {
    off_screen_canvas_ = m->CreateFrameCanvas();
  }
  void Run() {
    uint32_t continuum = 0;
    while (running()) {
      usleep(5 * 1000);
      continuum += 1;
      continuum %= 3 * 255;
      int r = 0, g = 0, b = 0;
      if (continuum <= 255) {
        int c = continuum;
        b = 255 - c;
        r = c;
      } else if (continuum > 255 && continuum <= 511) {
        int c = continuum - 256;
        r = 255 - c;
        g = c;
      } else {
        int c = continuum - 512;
        g = 255 - c;
        b = c;
      }
      matrix_->transformer()->Transform(off_screen_canvas_)->Fill(r, g, b);
      off_screen_canvas_ = matrix_->SwapOnVSync(off_screen_canvas_);
    }
  }
  
private:
  RGBMatrix *const matrix_;
  FrameCanvas *off_screen_canvas_;
};

// Simple generator that pulses through brightness on red, green, blue and white
class BrightnessPulseGenerator : public ThreadedCanvasManipulator {
public:
  BrightnessPulseGenerator(RGBMatrix *m) : ThreadedCanvasManipulator(m), matrix_(m) {}
  void Run() {
    const uint8_t max_brightness = matrix_->brightness();
    const uint8_t c = 255;
    uint8_t count = 0;

    while (running()) {
      if (matrix_->brightness() < 1) {
        matrix_->SetBrightness(max_brightness);
        count++;
      } else {
        matrix_->SetBrightness(matrix_->brightness() - 1);
      }

      switch (count % 4) {
        case 0: matrix_->Fill(c, 0, 0); break;
        case 1: matrix_->Fill(0, c, 0); break;
        case 2: matrix_->Fill(0, 0, c); break;
        case 3: matrix_->Fill(c, c, c); break;
      }

      usleep(20 * 1000);
    }
  }
  
private:
  RGBMatrix *const matrix_;
};

class SimpleSquare : public ThreadedCanvasManipulator {
public:
  SimpleSquare(Canvas *m) : ThreadedCanvasManipulator(m) {}
  void Run() {
    const int width = canvas()->width() - 1;
    const int height = canvas()->height() - 1;
    // Borders
    DrawLine(canvas(), 0, 0,      width, 0,      Color(255, 0, 0));
    DrawLine(canvas(), 0, height, width, height, Color(255, 255, 0));
    DrawLine(canvas(), 0, 0,      0,     height, Color(0, 0, 255));
    DrawLine(canvas(), width, 0,  width, height, Color(0, 255, 0));

    // Diagonals.
    DrawLine(canvas(), 0, 0,        width, height, Color(255, 255, 255));
    DrawLine(canvas(), 0, height, width, 0,        Color(255,   0, 255));
  }
};

class GrayScaleBlock : public ThreadedCanvasManipulator {
public:
  GrayScaleBlock(Canvas *m) : ThreadedCanvasManipulator(m) {}
  void Run() {
    const int sub_blocks = 16;
    const int width = canvas()->width();
    const int height = canvas()->height();
    const int x_step = max(1, width / sub_blocks);
    const int y_step = max(1, height / sub_blocks);
    uint8_t count = 0;
    while (running()) {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          int c = sub_blocks * (y / y_step) + x / x_step;
          switch (count % 4) {
          case 0: canvas()->SetPixel(x, y, c, c, c); break;
          case 1: canvas()->SetPixel(x, y, c, 0, 0); break;
          case 2: canvas()->SetPixel(x, y, 0, c, 0); break;
          case 3: canvas()->SetPixel(x, y, 0, 0, c); break;
          }
        }
      }
      count++;
      sleep(2);
    }
  }
};

// Simple class that generates a rotating block on the screen.
class RotatingBlockGenerator : public ThreadedCanvasManipulator {
public:
  RotatingBlockGenerator(Canvas *m, int scroll_ms = 15)
    : ThreadedCanvasManipulator(m), scroll_ms_(scroll_ms) {}

  uint8_t scale_col(int val, int lo, int hi) {
    if (val < lo) return 0;
    if (val > hi) return 255;
    return 255 * (val - lo) / (hi - lo);
  }

  void Run() {
    const int cent_x = canvas()->width() / 2;
    const int cent_y = canvas()->height() / 2;

    // The square to rotate (inner square + black frame) needs to cover the
    // whole area, even if diagnoal. Thus, when rotating, the outer pixels from
    // the previous frame are cleared.
    const int rotate_square = min(canvas()->width(), canvas()->height()) * 1.41;
    const int min_rotate = cent_x - rotate_square / 2;
    const int max_rotate = cent_x + rotate_square / 2;

    // The square to display is within the visible area.
    const int display_square = min(canvas()->width(), canvas()->height()) * 0.7;
    const int min_display = cent_x - display_square / 2;
    const int max_display = cent_x + display_square / 2;

    const float deg_to_rad = 2 * 3.14159265 / 360;
    int rotation = 0;
    while (running()) {
      ++rotation;
      usleep(scroll_ms_ * 1000);
      rotation %= 360;
      for (int x = min_rotate; x < max_rotate; ++x) {
        for (int y = min_rotate; y < max_rotate; ++y) {
          float rot_x, rot_y;
          Rotate(x - cent_x, y - cent_x,
                 deg_to_rad * rotation, &rot_x, &rot_y);
          if (x >= min_display && x < max_display &&
              y >= min_display && y < max_display) { // within display square
            canvas()->SetPixel(rot_x + cent_x, rot_y + cent_y,
                               scale_col(x, min_display, max_display),
                               255 - scale_col(y, min_display, max_display),
                               scale_col(y, min_display, max_display));
          } else {
            // black frame.
            canvas()->SetPixel(rot_x + cent_x, rot_y + cent_y, 0, 0, 0);
          }
        }
      }
    }
  }
  
private:
  
  void Rotate(int x, int y, float angle,
              float *new_x, float *new_y) {
    *new_x = x * cosf(angle) - y * sinf(angle);
    *new_y = x * sinf(angle) + y * cosf(angle);
  }
  const int scroll_ms_;
};

class ImageScroller : public ThreadedCanvasManipulator {
public:
  // Scroll image with "scroll_jumps" pixels every "scroll_ms" milliseconds.
  // If "scroll_ms" is negative, don't do any scrolling.
  ImageScroller(RGBMatrix *m, int scroll_jumps, int scroll_ms = 30)
    : ThreadedCanvasManipulator(m), scroll_jumps_(scroll_jumps),
      scroll_ms_(scroll_ms),
      horizontal_position_(0),
      matrix_(m) {
      offscreen_ = matrix_->CreateFrameCanvas();
  }

  virtual ~ImageScroller() {
    Stop();
    WaitStopped();   // only now it is safe to delete our instance variables.
  }

  // _very_ simplified. Can only read binary P6 PPM. Expects newlines in headers
  // Not really robust. Use at your own risk :)
  // This allows reload of an image while things are running, e.g. you can
  // life-update the content.
  bool LoadPPM(const char *filename) {
    FILE *f = fopen(filename, "r");
    // check if file exists
    if (f == NULL && access(filename, F_OK) == -1) {
      fprintf(stderr, "File \"%s\" doesn't exist\n", filename);
      return false;
    }
    if (f == NULL) return false;
    char header_buf[256];
    const char *line = ReadLine(f, header_buf, sizeof(header_buf));
#define EXIT_WITH_MSG(m) { fprintf(stderr, "%s: %s |%s", filename, m, line); \
      fclose(f); return false; }
    if (sscanf(line, "P6 ") == EOF)
      EXIT_WITH_MSG("Can only handle P6 as PPM type.");
    line = ReadLine(f, header_buf, sizeof(header_buf));
    int new_width, new_height;
    if (!line || sscanf(line, "%d %d ", &new_width, &new_height) != 2)
      EXIT_WITH_MSG("Width/height expected");
    int value;
    line = ReadLine(f, header_buf, sizeof(header_buf));
    if (!line || sscanf(line, "%d ", &value) != 1 || value != 255)
      EXIT_WITH_MSG("Only 255 for maxval allowed.");
    const size_t pixel_count = new_width * new_height;
    Pixel *new_image = new Pixel [ pixel_count ];
    assert(sizeof(Pixel) == 3);   // we make that assumption.
    if (fread(new_image, sizeof(Pixel), pixel_count, f) != pixel_count) {
      line = "";
      EXIT_WITH_MSG("Not enough pixels read.");
    }
#undef EXIT_WITH_MSG
    fclose(f);
    fprintf(stderr, "Read image '%s' with %dx%d\n", filename,
            new_width, new_height);
    horizontal_position_ = 0;
    MutexLock l(&mutex_new_image_);
    new_image_.Delete();  // in case we reload faster than is picked up
    new_image_.image = new_image;
    new_image_.width = new_width;
    new_image_.height = new_height;
    return true;
  }

  void Run() {
    const int screen_height = matrix_->transformer()->Transform(offscreen_)->height();
    const int screen_width = matrix_->transformer()->Transform(offscreen_)->width();
    while (running()) {
      {
        MutexLock l(&mutex_new_image_);
        if (new_image_.IsValid()) {
          current_image_.Delete();
          current_image_ = new_image_;
          new_image_.Reset();
        }
      }
      if (!current_image_.IsValid()) {
        usleep(100 * 1000);
        continue;
      }
      for (int x = 0; x < screen_width; ++x) {
        for (int y = 0; y < screen_height; ++y) {
          const Pixel &p = current_image_.getPixel(
                     (horizontal_position_ + x) % current_image_.width, y);
          matrix_->transformer()->Transform(offscreen_)->SetPixel(x, y, p.red, p.green, p.blue);
        }
      }
      offscreen_ = matrix_->SwapOnVSync(offscreen_);
      horizontal_position_ += scroll_jumps_;
      if (horizontal_position_ < 0) horizontal_position_ = current_image_.width;
      if (scroll_ms_ <= 0) {
        // No scrolling. We don't need the image anymore.
        current_image_.Delete();
      } else {
        usleep(scroll_ms_ * 1000);
      }
    }
  }
  
private:
  struct Pixel {
    Pixel() : red(0), green(0), blue(0){}
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  struct Image {
    Image() : width(-1), height(-1), image(NULL) {}
    ~Image() { Delete(); }
    void Delete() { delete [] image; Reset(); }
    void Reset() { image = NULL; width = -1; height = -1; }
    inline bool IsValid() { return image && height > 0 && width > 0; }
    const Pixel &getPixel(int x, int y) {
      static Pixel black;
      if (x < 0 || x >= width || y < 0 || y >= height) return black;
      return image[x + width * y];
    }

    int width;
    int height;
    Pixel *image;
  };

  // Read line, skip comments.
  char *ReadLine(FILE *f, char *buffer, size_t len) {
    char *result;
    do {
      result = fgets(buffer, len, f);
    } while (result != NULL && result[0] == '#');
    return result;
  }

  const int scroll_jumps_;
  const int scroll_ms_;

  // Current image is only manipulated in our thread.
  Image current_image_;

  // New image can be loaded from another thread, then taken over in main thread.
  Mutex mutex_new_image_;
  Image new_image_;

  int32_t horizontal_position_;

  RGBMatrix* matrix_;
  FrameCanvas* offscreen_;
};


// Abelian sandpile
// Contributed by: Vliedel
class Sandpile : public ThreadedCanvasManipulator {
public:
  Sandpile(Canvas *m, int delay_ms=50)
    : ThreadedCanvasManipulator(m), delay_ms_(delay_ms) {
    width_ = canvas()->width() - 1; // We need an odd width
    height_ = canvas()->height() - 1; // We need an odd height

    // Allocate memory
    values_ = new int*[width_];
    for (int x=0; x<width_; ++x) {
      values_[x] = new int[height_];
    }
    newValues_ = new int*[width_];
    for (int x=0; x<width_; ++x) {
      newValues_[x] = new int[height_];
    }

    // Init values
    srand(time(NULL));
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        values_[x][y] = 0;
      }
    }
  }

  ~Sandpile() {
    for (int x=0; x<width_; ++x) {
      delete [] values_[x];
    }
    delete [] values_;
    for (int x=0; x<width_; ++x) {
      delete [] newValues_[x];
    }
    delete [] newValues_;
  }

  void Run() {
    while (running()) {
      // Drop a sand grain in the centre
      values_[width_/2][height_/2]++;
      updateValues();

      for (int x=0; x<width_; ++x) {
        for (int y=0; y<height_; ++y) {
          switch (values_[x][y]) {
            case 0:
              canvas()->SetPixel(x, y, 0, 0, 0);
              break;
            case 1:
              canvas()->SetPixel(x, y, 0, 0, 200);
              break;
            case 2:
              canvas()->SetPixel(x, y, 0, 200, 0);
              break;
            case 3:
              canvas()->SetPixel(x, y, 150, 100, 0);
              break;
            default:
              canvas()->SetPixel(x, y, 200, 0, 0);
          }
        }
      }
      usleep(delay_ms_ * 1000); // ms
    }
  }
  
private:
  void updateValues() {
    // Copy values to newValues
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        newValues_[x][y] = values_[x][y];
      }
    }

    // Update newValues based on values
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        if (values_[x][y] > 3) {
          // Collapse
          if (x>0)
            newValues_[x-1][y]++;
          if (x<width_-1)
            newValues_[x+1][y]++;
          if (y>0)
            newValues_[x][y-1]++;
          if (y<height_-1)
            newValues_[x][y+1]++;
          newValues_[x][y] -= 4;
        }
      }
    }
    // Copy newValues to values
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        values_[x][y] = newValues_[x][y];
      }
    }
  }

  int width_;
  int height_;
  int** values_;
  int** newValues_;
  int delay_ms_;
};


// Conway's game of life
// Contributed by: Vliedel
class GameLife : public ThreadedCanvasManipulator {
public:
  GameLife(Canvas *m, int delay_ms=500, bool torus=true)
    : ThreadedCanvasManipulator(m), delay_ms_(delay_ms), torus_(torus) {
    width_ = canvas()->width();
    height_ = canvas()->height();

    // Allocate memory
    values_ = new int*[width_];
    for (int x=0; x<width_; ++x) {
      values_[x] = new int[height_];
    }
    newValues_ = new int*[width_];
    for (int x=0; x<width_; ++x) {
      newValues_[x] = new int[height_];
    }

    // Init values randomly
    srand(time(NULL));
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        values_[x][y]=rand()%2;
      }
    }
    r_ = rand()%255;
    g_ = rand()%255;
    b_ = rand()%255;

    if (r_<150 && g_<150 && b_<150) {
      int c = rand()%3;
      switch (c) {
        case 0:
          r_ = 200;
          break;
        case 1:
          g_ = 200;
          break;
        case 2:
          b_ = 200;
          break;
      }
    }
  }

  ~GameLife() {
    for (int x=0; x<width_; ++x) {
      delete [] values_[x];
    }
    delete [] values_;
    for (int x=0; x<width_; ++x) {
      delete [] newValues_[x];
    }
    delete [] newValues_;
  }

  void Run() {
    while (running()) {

      updateValues();

      for (int x=0; x<width_; ++x) {
        for (int y=0; y<height_; ++y) {
          if (values_[x][y])
            canvas()->SetPixel(x, y, r_, g_, b_);
          else
            canvas()->SetPixel(x, y, 0, 0, 0);
        }
      }
      usleep(delay_ms_ * 1000); // ms
    }
  }

private:
  int numAliveNeighbours(int x, int y) {
    int num=0;
    if (torus_) {
      // Edges are connected (torus)
      num += values_[(x-1+width_)%width_][(y-1+height_)%height_];
      num += values_[(x-1+width_)%width_][y                    ];
      num += values_[(x-1+width_)%width_][(y+1        )%height_];
      num += values_[(x+1       )%width_][(y-1+height_)%height_];
      num += values_[(x+1       )%width_][y                    ];
      num += values_[(x+1       )%width_][(y+1        )%height_];
      num += values_[x                  ][(y-1+height_)%height_];
      num += values_[x                  ][(y+1        )%height_];
    }
    else {
      // Edges are not connected (no torus)
      if (x>0) {
        if (y>0)
          num += values_[x-1][y-1];
        if (y<height_-1)
          num += values_[x-1][y+1];
        num += values_[x-1][y];
      }
      if (x<width_-1) {
        if (y>0)
          num += values_[x+1][y-1];
        if (y<31)
          num += values_[x+1][y+1];
        num += values_[x+1][y];
      }
      if (y>0)
        num += values_[x][y-1];
      if (y<height_-1)
        num += values_[x][y+1];
    }
    return num;
  }

  void updateValues() {
    // Copy values to newValues
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        newValues_[x][y] = values_[x][y];
      }
    }
    // update newValues based on values
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        int num = numAliveNeighbours(x,y);
        if (values_[x][y]) {
          // cell is alive
          if (num < 2 || num > 3)
            newValues_[x][y] = 0;
        }
        else {
          // cell is dead
          if (num == 3)
            newValues_[x][y] = 1;
        }
      }
    }
    // copy newValues to values
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        values_[x][y] = newValues_[x][y];
      }
    }
  }

  int** values_;
  int** newValues_;
  int delay_ms_;
  int r_;
  int g_;
  int b_;
  int width_;
  int height_;
  bool torus_;
};

// Langton's ant
// Contributed by: Vliedel
class Ant : public ThreadedCanvasManipulator {
public:
  Ant(Canvas *m, int delay_ms=500)
    : ThreadedCanvasManipulator(m), delay_ms_(delay_ms) {
    numColors_ = 4;
    width_ = canvas()->width();
    height_ = canvas()->height();
    values_ = new int*[width_];
    for (int x=0; x<width_; ++x) {
      values_[x] = new int[height_];
    }
  }

  ~Ant() {
    for (int x=0; x<width_; ++x) {
      delete [] values_[x];
    }
    delete [] values_;
  }

  void Run() {
    antX_ = width_/2;
    antY_ = height_/2-3;
    antDir_ = 0;
    for (int x=0; x<width_; ++x) {
      for (int y=0; y<height_; ++y) {
        values_[x][y] = 0;
        updatePixel(x, y);
      }
    }

    while (running()) {
      // LLRR
      switch (values_[antX_][antY_]) {
        case 0:
        case 1:
          antDir_ = (antDir_+1+4) % 4;
          break;
        case 2:
        case 3:
          antDir_ = (antDir_-1+4) % 4;
          break;
      }

      values_[antX_][antY_] = (values_[antX_][antY_] + 1) % numColors_;
      int oldX = antX_;
      int oldY = antY_;
      switch (antDir_) {
        case 0:
          antX_++;
          break;
        case 1:
          antY_++;
          break;
        case 2:
          antX_--;
          break;
        case 3:
          antY_--;
          break;
      }
      updatePixel(oldX, oldY);
      if (antX_ < 0 || antX_ >= width_ || antY_ < 0 || antY_ >= height_)
        return;
      updatePixel(antX_, antY_);
      usleep(delay_ms_ * 1000);
    }
  }
  
private:
  void updatePixel(int x, int y) {
    switch (values_[x][y]) {
      case 0:
        canvas()->SetPixel(x, y, 200, 0, 0);
        break;
      case 1:
        canvas()->SetPixel(x, y, 0, 200, 0);
        break;
      case 2:
        canvas()->SetPixel(x, y, 0, 0, 200);
        break;
      case 3:
        canvas()->SetPixel(x, y, 150, 100, 0);
        break;
    }
    if (x == antX_ && y == antY_)
      canvas()->SetPixel(x, y, 0, 0, 0);
  }

  int numColors_;
  int** values_;
  int antX_;
  int antY_;
  int antDir_; // 0 right, 1 up, 2 left, 3 down
  int delay_ms_;
  int width_;
  int height_;
};



// Imitation of volume bars
// Purely random height doesn't look realistic
// Contributed by: Vliedel
class VolumeBars : public ThreadedCanvasManipulator {
public:
  VolumeBars(Canvas *m, int delay_ms=50, int numBars=8)
    : ThreadedCanvasManipulator(m), delay_ms_(delay_ms),
      numBars_(numBars), t_(0) {
  }

  ~VolumeBars() {
    delete [] barHeights_;
    delete [] barFreqs_;
    delete [] barMeans_;
  }

  void Run() {
    const int width = canvas()->width();
    height_ = canvas()->height();
    barWidth_ = width/numBars_;
    barHeights_ = new int[numBars_];
    barMeans_ = new int[numBars_];
    barFreqs_ = new int[numBars_];
    heightGreen_  = height_*4/12;
    heightYellow_ = height_*8/12;
    heightOrange_ = height_*10/12;
    heightRed_    = height_*12/12;

    // Array of possible bar means
    int numMeans = 10;
    int means[10] = {1,2,3,4,5,6,7,8,16,32};
    for (int i=0; i<numMeans; ++i) {
      means[i] = height_ - means[i]*height_/8;
    }
    // Initialize bar means randomly
    srand(time(NULL));
    for (int i=0; i<numBars_; ++i) {
      barMeans_[i] = rand()%numMeans;
      barFreqs_[i] = 1<<(rand()%3);
    }

    // Start the loop
    while (running()) {
      if (t_ % 8 == 0) {
        // Change the means
        for (int i=0; i<numBars_; ++i) {
          barMeans_[i] += rand()%3 - 1;
          if (barMeans_[i] >= numMeans)
            barMeans_[i] = numMeans-1;
          if (barMeans_[i] < 0)
            barMeans_[i] = 0;
        }
      }

      // Update bar heights
      t_++;
      for (int i=0; i<numBars_; ++i) {
        barHeights_[i] = (height_ - means[barMeans_[i]])
          * sin(0.1*t_*barFreqs_[i]) + means[barMeans_[i]];
        if (barHeights_[i] < height_/8)
          barHeights_[i] = rand() % (height_/8) + 1;
      }

      for (int i=0; i<numBars_; ++i) {
        int y;
        for (y=0; y<barHeights_[i]; ++y) {
          if (y<heightGreen_) {
            drawBarRow(i, y, 0, 200, 0);
          }
          else if (y<heightYellow_) {
            drawBarRow(i, y, 150, 150, 0);
          }
          else if (y<heightOrange_) {
            drawBarRow(i, y, 250, 100, 0);
          }
          else {
            drawBarRow(i, y, 200, 0, 0);
          }
        }
        // Anything above the bar should be black
        for (; y<height_; ++y) {
          drawBarRow(i, y, 0, 0, 0);
        }
      }
      usleep(delay_ms_ * 1000);
    }
  }
  
private:
  void drawBarRow(int bar, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t x=bar*barWidth_; x<(bar+1)*barWidth_; ++x) {
      canvas()->SetPixel(x, height_-1-y, r, g, b);
    }
  }

  int delay_ms_;
  int numBars_;
  int* barHeights_;
  int barWidth_;
  int height_;
  int heightGreen_;
  int heightYellow_;
  int heightOrange_;
  int heightRed_;
  int* barFreqs_;
  int* barMeans_;
  int t_;
};

/// Genetic Colors
/// A genetic algorithm to evolve colors
/// by bbhsu2 + anonymous
class GeneticColors : public ThreadedCanvasManipulator {
public:
  GeneticColors(Canvas *m, int delay_ms = 200)
    : ThreadedCanvasManipulator(m), delay_ms_(delay_ms) {
    width_ = canvas()->width();
    height_ = canvas()->height();
    popSize_ = width_ * height_;

    // Allocate memory
    children_ = new citizen[popSize_];
    parents_ = new citizen[popSize_];
    srand(time(NULL));
  }

  ~GeneticColors() {
    delete [] children_;
    delete [] parents_;
  }

  static int rnd (int i) { return rand() % i; }

  void Run() {
    // Set a random target_
    target_ = rand() & 0xFFFFFF;

    // Create the first generation of random children_
    for (int i = 0; i < popSize_; ++i) {
      children_[i].dna = rand() & 0xFFFFFF;
    }

    while(running()) {
      swap();
      sort();
      mate();
      std::random_shuffle (children_, children_ + popSize_, rnd);

      // Draw citizens to canvas
      for(int i=0; i < popSize_; i++) {
        int c = children_[i].dna;
        int x = i % width_;
        int y = (int)(i / width_);
        canvas()->SetPixel(x, y, R(c), G(c), B(c));
      }

      // When we reach the 85% fitness threshold...
      if(is85PercentFit()) {
        // ...set a new random target_
        target_ = rand() & 0xFFFFFF;

        // Randomly mutate everyone for sake of new colors
        for (int i = 0; i < popSize_; ++i) {
          mutate(children_[i]);
        }
      }
      usleep(delay_ms_ * 1000);
    }
  }
  
private:
  /// citizen will hold dna information, a 24-bit color value.
  struct citizen {
    citizen() { }

    citizen(int chrom)
      : dna(chrom) {
    }

    int dna;
  };

  /// for sorting by fitness
  class comparer {
  public:
    comparer(int t)
      : target_(t) { }

    inline bool operator() (const citizen& c1, const citizen& c2) {
      return (calcFitness(c1.dna, target_) < calcFitness(c2.dna, target_));
    }

  private:
    const int target_;
  };

  static int R(const int cit) { return at(cit, 16); }
  static int G(const int cit) { return at(cit, 8); }
  static int B(const int cit) { return at(cit, 0); }
  static int at(const int v, const  int offset) { return (v >> offset) & 0xFF; }

  /// fitness here is how "similar" the color is to the target
  static int calcFitness(const int value, const int target) {
    // Count the number of differing bits
    int diffBits = 0;
    for (unsigned int diff = value ^ target; diff; diff &= diff - 1) {
      ++diffBits;
    }
    return diffBits;
  }

  /// sort by fitness so the most fit citizens are at the top of parents_
  /// this is to establish an elite population of greatest fitness
  /// the most fit members and some others are allowed to reproduce
  /// to the next generation
  void sort() {
    std::sort(parents_, parents_ + popSize_, comparer(target_));
  }

  /// let the elites continue to the next generation children
  /// randomly select 2 parents of (near)elite fitness and determine
  /// how they will mate. after mating, randomly mutate citizens
  void mate() {
    // Adjust these for fun and profit
    const float eliteRate = 0.30f;
    const float mutationRate = 0.20f;

    const int numElite = popSize_ * eliteRate;
    for (int i = 0; i < numElite; ++i) {
      children_[i] = parents_[i];
    }

    for (int i = numElite; i < popSize_; ++i) {
      //select the parents randomly
      const float sexuallyActive = 1.0 - eliteRate;
      const int p1 = rand() % (int)(popSize_ * sexuallyActive);
      const int p2 = rand() % (int)(popSize_ * sexuallyActive);
      const int matingMask = (~0) << (rand() % bitsPerPixel);

      // Make a baby
      int baby = (parents_[p1].dna & matingMask)
        | (parents_[p2].dna & ~matingMask);
      children_[i].dna = baby;

      // Mutate randomly based on mutation rate
      if ((rand() / (float)RAND_MAX) < mutationRate) {
        mutate(children_[i]);
      }
    }
  }

  /// parents make children,
  /// children become parents,
  /// and they make children...
  void swap() {
    citizen* temp = parents_;
    parents_ = children_;
    children_ = temp;
  }

  void mutate(citizen& c) {
    // Flip a random bit
    c.dna ^= 1 << (rand() % bitsPerPixel);
  }

  /// can adjust this threshold to make transition to new target seamless
  bool is85PercentFit() {
    int numFit = 0;
    for (int i = 0; i < popSize_; ++i) {
        if (calcFitness(children_[i].dna, target_) < 1) {
            ++numFit;
        }
    }
    return ((numFit / (float)popSize_) > 0.85f);
  }

  static const int bitsPerPixel = 24;
  int popSize_;
  int width_, height_;
  int delay_ms_;
  int target_;
  citizen* children_;
  citizen* parents_;
};

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s <options> -D <demo-nr> [optional parameter]\n",
          progname);
  fprintf(stderr, "Options:\n"
          "\t-r <rows>          : Panel rows. '16' for 16x32 (1:8 multiplexing),\n"
          "\t                   '32' for 32x32 (1:16), '8' for 1:4 multiplexing; "
          "Default: 32\n"
          "\t-P <parallel>      : For Plus-models or RPi2: parallel chains. 1..3. "
          "Default: 1\n"
          "\t-c <chained>       : Daisy-chained boards. Default: 4.\n"
          "\t-L                 : 'Large' display, composed out of 4 times 32x32\n"
          "\t-p <pwm-bits>      : Bits used for PWM. Something between 1..11\n"
          "\t-l                 : Don't do luminance correction (CIE1931)\n"
          "\t-D <animation-nr>  : Defaults to 3\n"
          "\t-b <brightnes>     : Sets brightness percent. Default: 100.\n"
          "\t-R <rotation>      : Sets the rotation of matrix. Allowed: 0, 90, 180, 270. Default: 0.\n"
          "\t-S <MQTT server>   : Sets the MQTT server\n"
          "\t-T <MQTT topic>    : Sets the MQTT topic listen to\n"
          "\t-U <MQTT username> : Sets the MQTT username\n"
          "\t-W <MQTT password> : Sets the MQTT password\n"
            );
  fprintf(stderr, "Animations, choosen with -D\n");
  fprintf(stderr, "\t0  - some rotating square\n"
          "\t1  - forward scrolling an image (-m <scroll-ms>)\n"
          "\t2  - backward scrolling an image (-m <scroll-ms>)\n"
          "\t3  - test image: a square\n"
          "\t4  - Pulsing color\n"
          "\t5  - Grayscale Block\n"
          "\t6  - Abelian sandpile model (-m <time-step-ms>)\n"
          "\t7  - Conway's game of life (-m <time-step-ms>)\n"
          "\t8  - Langton's ant (-m <time-step-ms>)\n"
          "\t9  - Volume bars (-m <time-step-ms>)\n"
          "\t10 - Evolution of color (-m <time-step-ms>)\n"
          "\t11 - Brightness pulse generator\n"
          "\t12 - Text animator\n");
  fprintf(stderr, "Example:\n\t%s -D 1 runtext.ppm\n"
          "Scrolls the runtext\n", progname);
  return 1;
}

class Display{
public:
  Display(int rows, int chain, int parallel, int pwm_bits, int brightness, int rotation, bool large_display, bool do_luminance_correct)
  : matrix_(NULL), canvas_(NULL), image_gen_(NULL)
  {
    io_.Init();
    matrix_ = new RGBMatrix(&io_, rows, chain, parallel);
    matrix_->set_luminance_correct(do_luminance_correct);
    matrix_->SetBrightness(brightness);
    if (pwm_bits >= 0 && !matrix_->SetPWMBits(pwm_bits)) {
      fprintf(stderr, "Invalid range of pwm-bits\n");
    }
        
    LinkedTransformer *transformer_ = new LinkedTransformer();
    matrix_->SetTransformer(transformer_);
    if (large_display) {
      // Mapping the coordinates of a 32x128 display mapped to a square of 64x64
      transformer_->AddTransformer(new LargeSquare64x64Transformer());
    }
    
    if (rotation > 0) {
      transformer_->AddTransformer(new RotateTransformer(rotation));
    }
        
    canvas_ = matrix_;
  }
    
  ~Display() {
    delete image_gen_;
    delete canvas_;  
    transformer_->DeleteTransformers();
    delete transformer_;
  }

  void set_option(const std::string key, const std::string value) {
    std::cout << "Set option key: " << key << " value: " << value << std::endl;
    image_gen_->set_option(key, value);
  }

  int set_display(int animation, params_map& anim_params) {
    std::cout << "Set display to animation nr: " << animation << std::endl;
    int scroll_ms = 30;
    const char *image = NULL;
    std::string text = "Initializing";
    std::string color = "0,0,255";
    
    for ( auto param = anim_params.begin(); param != anim_params.end(); ++param ) {
      std::cout << " " << param->first << ":" << param->second;
      if (param->first == "scroll_ms")
        scroll_ms = std::stoi(param->second);      
      if (param->first == "image")
        image = param->second.c_str();
      if (param->first == "text")
        text = param->second.c_str();  
      //if (param->first == "color")
      //  color = param->second;  
    }
    std::cout << std::endl;
        
    if (image_gen_) {
      image_gen_->Stop();
      matrix_->Clear();
      canvas_->Clear();
    }
    switch (animation) {
      case 0:
        image_gen_ = new RotatingBlockGenerator(canvas_, scroll_ms);
        break;
                
      case 1:
      case 2:
        if (image) {
          ImageScroller *scroller = new ImageScroller(matrix_,
                                                        animation == 1 ? 1 : -1,
                                                        scroll_ms);
          if (!scroller->LoadPPM(image))
            return 1;
          image_gen_ = scroller;
        } else {
          fprintf(stderr, "Demo %d Requires PPM image as parameter\n", animation);
          return 1;
        }
        break;
      
      case 3:
        image_gen_ = new SimpleSquare(canvas_);
        break;
        
      case 4:
        image_gen_ = new ColorPulseGenerator(matrix_);
        break;
                
      case 5:
        image_gen_ = new GrayScaleBlock(canvas_);
        break;
                
      case 6:
        image_gen_ = new Sandpile(canvas_, scroll_ms);
        break;
                
      case 7:
        image_gen_ = new GameLife(canvas_, scroll_ms);
        break;
                
      case 8:
        image_gen_ = new Ant(canvas_, scroll_ms);
        break;
                
      case 9:
        image_gen_ = new VolumeBars(canvas_, scroll_ms, canvas_->width()/2);
        break;

      case 10:
        image_gen_ = new GeneticColors(canvas_, scroll_ms);
        break;
         
      case 11:
        image_gen_ = new BrightnessPulseGenerator(matrix_);
        break;

      case 12:
        image_gen_ = new TextScroller(matrix_, text);
        break;
    }

    if (image_gen_ == NULL) {
      fprintf(stderr, "Error creating the Animation\n");
      return 1;
    }
    image_gen_->Start();
    return 0;
  }

    
private:
  GPIO io_;
  RGBMatrix *matrix_;
  Canvas *canvas_;
  LinkedTransformer *transformer_;
  ThreadedCanvasManipulator *image_gen_;
};

/***
* 
* MQTT relevant classes
* 
***/

class action_listener : public virtual mqtt::iaction_listener {
public:
  action_listener(const std::string& name) : name_(name) {}

private:
  std::string name_;

  virtual void on_failure(const mqtt::token& tok) {
    std::cout << name_ << " failure";
    if (tok.get_message_id() != 0)
      std::cout << " (token: [" << tok.get_message_id() << "]" << std::endl;
    std::cout << std::endl;
  }

  virtual void on_success(const mqtt::token& tok) {
    std::cout << name_ << " success";
    if (tok.get_message_id() != 0)
      std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
    auto top = tok.get_topics();
    if (top && !top->empty())
      std::cout << "\ttoken topic: '" << (*top)[0] << "', ..." << std::endl;
    std::cout << std::endl;
  }
};

class callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener {
public:
  callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, action_listener& listener, std::string& topic, int qos, Display* display)
                                : cli_(cli), connOpts_(connOpts), listener_(listener), topic_(topic), qos_(qos), display_(display) {}
private:
  int nretry_;
  mqtt::async_client& cli_;
  mqtt::connect_options& connOpts_;
  action_listener& listener_;
  std::string topic_;
  int qos_;
  Display* display_;

  void reconnect() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    try {
      cli_.connect(connOpts_, nullptr, *this);
    }
    catch (const mqtt::exception& exc) {
      std::cerr << "Error: " << exc.what() << std::endl;
      exit(1);
    }
  }

  // Re-connection failure
  virtual void on_failure(const mqtt::token& tok) {
    std::cout << "Reconnection failed." << std::endl;
    if (++nretry_ > 5)
      exit(1);
    reconnect();
  }

  // Re-connection success
  virtual void on_success(const mqtt::token& tok) {
    std::cout << "Reconnection success" << std::endl;;
    cli_.subscribe(topic_, qos_, nullptr, listener_);
  }

  virtual void connection_lost(const std::string& cause) {
    std::cout << "\nConnection lost" << std::endl;
    if (!cause.empty())
      std::cout << "\tcause: " << cause << std::endl;

    std::cout << "Reconnecting." << std::endl;
    nretry_ = 0;
    reconnect();
  }

  virtual void message_arrived(const std::string& topic, mqtt::message_ptr msg) {
    params_map anim_params;
    std::string topic_short;
    
    std::cout << "Message arrived" << std::endl;
    std::cout << "\ttopic: '" << topic << "'" << std::endl;
    
    // remove the prefix (the part we have subscribed to) first
    topic_short = topic.substr(topic_.size()-1);
    
    // next part up to the / is the comment
    std::string command = topic_short.substr(0,topic_short.find_first_of("/"));
    std::cout << "\tCommand: " << command << std::endl;
    
    // next part (the rest of the topic) is option for the command
    std::string option = topic_short.substr(topic_short.find_first_of("/")+1);
    std::cout << "\tOption: " << option << std::endl;

    // we are trying to parse the message
    if (!msg->to_string().empty()) {
      Json::Value jsonData;
      Json::Reader jsonReader;
      if (jsonReader.parse(msg->to_string(), jsonData) && msg->to_string().substr(0,1) == "{") {
        std::cout << "\tJSON data" << std::endl;
        std::cout << "\t" << jsonData.toStyledString() << std::endl;  
        for ( auto it = jsonData.begin(); it != jsonData.end(); ++it ) {
          if((*it).isArray()) {
            std::cout << "\t" << it.key().asString() << " : array" << std::endl;
            // tbd
          }
          else if((*it).isString() || (*it).isInt()) {
            std::cout << "\t" << it.key().asString() << ":" << (*it).asString() << std::endl;
            anim_params[it.key().asString()] = (*it).asString();
          }
          else {
            std::cout << "\t" << it.key().asString() << " : unknown value" << std::endl;
          }
        }
      }
      else {
        std::cout << "\tString received:" << std::endl;
        std::cout << "\t" << msg->to_string() << std::endl;
        anim_params[option] = msg->to_string();
      }
    }

    if (command == "set") {
      std::cout << "\tOperation: set" << std::endl;
      if (option == "animation") {
        if (anim_params.find("animation") != anim_params.end())
          display_->set_display(stoi(anim_params["animation"]), anim_params);
      }
      else if (option == "text" || option == "text1" || option == "text2"
              || option == "textleft" || option == "textleft1" || option == "textleft2"
              || option == "textmid" || option == "textmid1" || option == "textmid2"
              || option == "textright" || option == "textright1" || option == "textright2"
              || option == "once" || option == "once1" || option == "once2"
              || option == "color" || option == "color1" || option == "color2"
              || option == "speed" || option == "speed1" || option == "speed2"
              || option == "font"  || option == "font1"  || option == "font2") {
        if (anim_params.find(option) != anim_params.end())
          display_->set_option(option, anim_params[option]);
      }
    } // end command
  }

  virtual void delivery_complete(mqtt::delivery_token_ptr token) {}
};

/***
*
* MAIN
*
***/

int main(int argc, char *argv[]) {
  int rows = PANEL_ROWS;
  int chain = PANEL_CHAINS;
  int parallel = PANEL_PARALLEL;
  int pwm_bits = PANEL_PWM_BITS;
  int brightness = PANEL_BRIGHTNESS;
  int rotation = PANEL_ROTATION;
  bool large_display = PANEL_LARGE_DISPLAY;
  bool do_luminance_correct = DO_LUMINANCE_CORRECT;
  std::string mqtt_server(MQTT_SERVER);
  std::string mqtt_topic(MQTT_TOPIC);
  std::string mqtt_username(MQTT_USERNAME);
  std::string mqtt_password(MQTT_PASSWORD);
  std::string text1(LED_TEXT1);
  std::string text2(LED_TEXT2);
  const std::string mqtt_clientid(MQTT_CLIENTID);
  const int  mqtt_qos = 1;
  int animation = 12; // Initial Display animation
  params_map anim_params;
  int opt;
  bool as_daemon = false;
  
  while ((opt = getopt(argc, argv, "dlD:r:P:c:p:b:m:LR:S:T:U:W:t:u:")) != -1) {
    switch (opt) {
    case 'D':
      animation = atoi(optarg);
      break;
      
    case 'd':
      as_daemon = true;
      break;
      
    case 'r':
      rows = atoi(optarg);
      break;

    case 'P':
      parallel = atoi(optarg);
      break;

    case 'c':
      chain = atoi(optarg);
      break;

    case 'm':
      anim_params["scroll_ms"] = optarg;
      break;

    case 'p':
      pwm_bits = atoi(optarg);
      break;

    case 'b':
      brightness = atoi(optarg);
      break;

    case 'l':
      do_luminance_correct = !do_luminance_correct;
      break;

    case 'L':
      // The 'large' display assumes a chain of four displays with 32x32
      chain = 4;
      rows = 32;
      large_display = true;
      break;

    case 'R':
      rotation = atoi(optarg);
      break;
      
    case 'S':
      mqtt_server.assign(optarg);
      break;
    
    case 'T':
      mqtt_topic.assign(optarg);
      break;

    case 'U':
      mqtt_username.assign(optarg);
      break;

    case 'W':
      mqtt_password.assign(optarg);
      break;

    case 't':
      text1.assign(optarg);
      break;
      
    case 'u':
      text2.assign(optarg);
      break;
                    
    default: /* '?' */
      return usage(argv[0]);
    }
  }

  if (optind < argc) {
    anim_params["image"] = argv[optind];
  }

  if (getuid() != 0) {
    fprintf(stderr, "Must run as root to be able to access /dev/mem\n"
            "Prepend 'sudo' to the command:\n\tsudo %s ...\n", argv[0]);
    return 1;
  }

  if (rows != 8 && rows != 16 && rows != 32) {
    fprintf(stderr, "Rows can one of 8, 16 or 32 "
            "for 1:4, 1:8 and 1:16 multiplexing respectively.\n");
    return 1;
  }

  if (chain < 1) {
    fprintf(stderr, "Chain outside usable range\n");
    return 1;
  }
  if (chain > 8) {
    fprintf(stderr, "That is a long chain. Expect some flicker.\n");
  }
  if (parallel < 1 || parallel > 3) {
    fprintf(stderr, "Parallel outside usable range.\n");
    return 1;
  }

  if (brightness < 1 || brightness > 100) {
    fprintf(stderr, "Brightness is outside usable range.\n");
    return 1;
  }

  if (rotation % 90 != 0) {
    fprintf(stderr, "Rotation %d not allowed! Only 0, 90, 180 and 270 are possible.\n", rotation);
    return 1;
  }

  // Start daemon before we start any threads.
  if (as_daemon) {
    pid_t pid = fork();
    
    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

  Display* display = new Display(rows, chain, parallel, pwm_bits, brightness, rotation, large_display, do_luminance_correct);
  display->set_display(animation, anim_params);
  sleep(3); // display needs some time to load font etc.
  
  action_listener subListener("Subscription");
  
  mqtt::connect_options connOpts;
  connOpts.set_keep_alive_interval(20);
  connOpts.set_clean_session(true);
  if (!mqtt_username.empty())
    connOpts.set_user_name(mqtt_username);
  if (!mqtt_password.empty())
    connOpts.set_password(mqtt_password);

  mqtt::async_client client(mqtt_server, mqtt_clientid);
  callback cb(client, connOpts, subListener, mqtt_topic, mqtt_qos, display);
  client.set_callback(cb);

  try {
    std::cout << "MQTT Waiting for the connection..." << std::flush;
    client.connect(connOpts, nullptr, cb)->wait();
    std::cout << "OK" << std::endl;
    std::cout << "MQTT Subscribing to topic " << mqtt_topic << "\n"
      << "for client " << mqtt_clientid
      << " using QoS" << mqtt_qos << std::endl;
    client.subscribe(mqtt_topic, mqtt_qos, nullptr, subListener)->wait();
    std::cout << "Ready..." << std::endl << std::flush;
    display->set_option("text1", text1);
    display->set_option("text2", text2);
    display->set_option("color2", "200,10,80");
    display->set_option("speed2", "10");

    if (as_daemon) {
      while(true){
        sleep(INT_MAX);
      }
    } else {
      std::cout << "Press <RETURN> to exit and reset LEDs"  << std::endl;
      getchar();
    }

    std::cout << "MQTT Disconnecting..." << std::flush;
    client.disconnect()->wait();
    std::cout << "MQTT OK" << std::endl;
  }
  catch (const mqtt::exception& exc) {
    std::cerr << "MQTT Error: " << exc.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
