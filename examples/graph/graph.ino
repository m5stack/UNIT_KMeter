
#include <Arduino.h>

#include <UNIT_KMeter.h>
#include <M5GFX.h>
#include <stdlib.h>
#include <alloca.h>

#include <esp_log.h>

static float* tempdata_buf;
static size_t tempdata_count;
static size_t tempdata_idx = 0;
static int graph_height;
static int graph_y_offset;
static float min_temp;
static float max_temp;

static constexpr size_t avg_count = 20;
static constexpr size_t delay_msec = 50;

static float avg_buf[avg_count];
static size_t avg_index = 0;

M5GFX display;
UNIT_KMeter sensor;

M5Canvas canvas[2];

void setup(void)
{
  display.begin();

  Wire.begin(SDA, SCL, 400000L);
  //Wire.begin(21, 22, 400000L);

  sensor.begin();

  display.clear();
  display.setEpdMode(epd_mode_t::epd_fast);
  if (display.width() > display.height())
  {
    display.setRotation(display.getRotation() ^ 1);
  }
  display.setFont(&fonts::Font4);
  display.setTextColor((uint32_t)~display.getBaseColor(), display.getBaseColor());
  display.setTextDatum(textdatum_t::top_right);
  display.setTextPadding(display.textWidth("1888.88", &fonts::Font4));
  graph_y_offset = display.fontHeight(&fonts::Font4);

  graph_height = display.height() - graph_y_offset;

  for (int i = 0; i < 2; ++i)
  {
    canvas[i].setColorDepth(display.getColorDepth());
    canvas[i].createSprite(1, graph_height);
    canvas[i].setTextDatum(textdatum_t::bottom_right);
    canvas[i].setTextColor(TFT_LIGHTGRAY);
  }

  tempdata_count = display.width() + avg_count + 1;
  tempdata_buf = (float*)malloc(tempdata_count * sizeof(float));

  float temperature = sensor.getTemperature();
  min_temp = max_temp = temperature;
  for (size_t i = 0; i < tempdata_count; ++i)
  {
    tempdata_buf[i] = temperature;
  }
  for (size_t i = 0; i < avg_count; ++i)
  {
    avg_buf[i] = temperature;
  }
}

static int vertline_idx = 0;

void drawGraph(void)
{
  float min_t = INT16_MAX;
  float max_t = INT16_MIN;
  for (int i = 0; i < tempdata_count; ++i)
  {
    float t = tempdata_buf[i];
    if (min_t > t) { min_t = t; }
    if (max_t < t) { max_t = t; }
  }
  float diff = (max_t - min_t) + 1;
  min_temp = (min_temp + min_t - (diff / 20)) / 2;
  max_temp = (max_temp + max_t + (diff / 20)) / 2;

  float magnify = (float)graph_height / (max_temp - min_temp);

  static constexpr int steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, INT_MAX };
  int step_index = 0;
  while (magnify * steps[step_index] < 10) { ++step_index; }
  int step = steps[step_index];
  bool flip = 0;

  canvas[flip].clear(display.getBaseColor());

  int gauge = ((int)min_temp / step) * step;
  do
  {
    canvas[flip].drawPixel(0, graph_height - ((gauge - min_temp ) * magnify), TFT_DARKGRAY);
  } while (max_temp > (gauge += step));

  size_t buffer_len = canvas[flip].bufferLength();
  auto buffer = (uint8_t*)alloca(buffer_len);
  memcpy(buffer, canvas[flip].getBuffer(), buffer_len);

  int drawindex = tempdata_idx;
  int draw_x = -1;
  int y0 = 0;

  if (++vertline_idx >= 20) { vertline_idx = 0; }
  int vidx = vertline_idx;

  display.startWrite();
  do
  {
    if (++vidx >= 20)
    {
      vidx = 0;
      memset(canvas[flip].getBuffer(), 0x55, buffer_len);
    }
    else
    {
      memcpy(canvas[flip].getBuffer(), buffer, buffer_len);
    }

    int y1 = y0;
    y0 = graph_height - ((tempdata_buf[drawindex] - min_temp) * magnify);
    if (++drawindex >= tempdata_count) { drawindex = 0; }

    if (display.width() - draw_x < 24)
    {
      gauge = ((int)min_temp / step) * step;
      do
      {
        canvas[flip].drawNumber((int)gauge, display.width() - draw_x, graph_height - ((gauge - min_temp) * magnify));
      } while (max_temp > (gauge += step));
    }

    int y_min = y0, y_max = y1;
    if (y_min > y_max) { std::swap(y_min, y_max); }
    canvas[flip].drawFastVLine(0, y_min, y_max - y_min + 1, ~display.getBaseColor());
    canvas[flip].pushSprite(&display, draw_x, graph_y_offset);
    flip = !flip;
  } while (++draw_x < display.width());
  display.endWrite();
}

void loop(void)
{
  float temperature = sensor.getTemperature();

  avg_buf[avg_index] = temperature;
  if (++avg_index >= avg_count) { avg_index = 0; }
  float avg_temp = 0;
  for (size_t i = 0; i < avg_count; ++i)
  {
    avg_temp += avg_buf[i];
  }

  tempdata_buf[tempdata_idx] = avg_temp / avg_count;
  if (++tempdata_idx >= tempdata_count)
  {
    tempdata_idx = 0;
  }

  drawGraph();
  display.drawFloat( temperature, 2, display.width(), 0 );

  static uint32_t prev_msec;
  uint32_t msec = millis();
  if (msec - prev_msec < delay_msec)
  {
    ESP_LOGI("loop", "%2.2f", temperature);
    m5gfx::delay(delay_msec - (msec - prev_msec));
  }
  prev_msec = msec;
}
