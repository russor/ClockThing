#include <Arduino.h>
#include "config.h"
#include "lvgl/lvgl.h"
#include <WiFi.h>
#include "time.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

//#define ALARM_FREQ 500
#define ALARM_FREQ 1046

TTGOClass *ttgo;

volatile int ticked = 0;

time_t last_synced;
time_t last_fetched;
time_t last_success;
time_t last_touch;
time_t last_alarm;
time_t next_alarm;

struct
{
  struct
  {
    time_t start;
    uint32_t offset;
    unsigned char buffer[8];
  } offsets[4];
  size_t num_offsets;
  struct
  {
    time_t start;
    unsigned char buffer[20];
  } alarms[100];
  size_t num_alarms;
  time_t alarm_skip;
  char feed_url[256];
} state;
Preferences preferences;

WiFiManager wifiManager;
WiFiManagerParameter feed_url("feed", "ical feed url", "", 255);

volatile int touched = 0;
volatile int beeping = 0;

void relayTurnOn(void)
{
  ttgo->turnOnRelay();
}

void relayTurnOff(void)
{
  ttgo->turnOffRelay();
}

void setBrightness(uint8_t level)
{
  ttgo->setBrightness(level);
}

void turnOnUSB()
{
  ttgo->turnOnUSB();
}

void turnOffUSB()
{
  ttgo->turnOffUSB();
}

float getVoltage()
{
  return ttgo->getVoltage();
}

void time_synced(struct timeval *tv)
{
  Serial.println("time synced");
  ticked = 1;
  last_synced = time(NULL);
  struct tm *t = gmtime(&last_synced);
  RTC_Date rtcnow(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                  t->tm_hour, t->tm_min, t->tm_sec);
  ttgo->rtc->setDateTime(rtcnow);
  Serial.println("rtc set");
}

void andevent(arduino_event_id_t event)
{
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED)
  {
    Serial.println("wifi connected");
    configTime(0, 0, "0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_synced);
  }
  else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
  {
    Serial.println("wifi disconnected, expect wifi manager to restart");
  }
  ticked = 1;
}

void save_data(const char *location)
{
  char buf[sizeof(state)];
  bzero(buf, sizeof(state));
  Serial.printf("save data %s", location);
  if (preferences.getBytes("s", buf, sizeof(state)))
  {
    if (memcmp(&state, buf, sizeof(state)) == 0)
    {
      Serial.println(" unchanged");
      return;
    }
  }
  preferences.putBytes("s", &state, sizeof(state));
  Serial.println(" saved");
}

void saveParamsCallback()
{
  if (strncmp(feed_url.getValue(), state.feed_url, sizeof(state.feed_url)) != 0)
  {
    if (strlcpy(state.feed_url, feed_url.getValue(), sizeof(state.feed_url)) >= sizeof(state.feed_url))
    {
      Serial.println("feed url too big");
    }
    state.num_offsets = state.num_alarms = 0;
    last_fetched = 0;
    ticked = 1;
    if (beeping)
    {
      touched = 1;
    }
    save_data("save params callback");
  }
  wifiManager.stopWebPortal();
}

TaskHandle_t beeptask;

#define BEEP_ON 250
#define BEEP_OFF 350

void beep(void *)
{
  while (1)
  {
    vTaskSuspend(NULL);
    touched = 0;
    int pwm = 10;
    beeping = 1;
    ticked = 1;
    int maxcount = 59000 / (BEEP_ON + BEEP_OFF); // beep for 59 seconds
    while (touched == 0 && --maxcount)
    {
      ledcWrite(1, pwm);
      vTaskDelay(BEEP_ON / portTICK_PERIOD_MS);
      ledcWrite(1, 0);
      vTaskDelay(BEEP_OFF / portTICK_PERIOD_MS);
      pwm += 5;
      if (pwm >= 128)
      {
        pwm = 127;
      }
    }
    beeping = 0;

    ticked = 1;
  }
}

void clicked()
{
  time_t now = time(NULL);
  if (beeping)
  {
    if (!touched)
    {
      touched = 1;
      Serial.println("touch to end beeping");
    }
  }
  else if ((now - last_touch) > 1)
  {
    Serial.println("new touch");
    last_touch = now;
    if (next_alarm != state.alarm_skip)
    {
      state.alarm_skip = next_alarm;
    }
    else
    {
      state.alarm_skip = 0;
    }
    save_data("clicked");
    ticked = 1;
  }
  else
  {
    // Serial.println("continuing touch");
    last_touch = now;
  }
}

void longclicked()
{
  if (beeping)
  {
    return clicked();
  }
  wifiManager.startWebPortal();
}

lv_obj_t *timelabel, *datelabel, *alarmlabel, *tzlabel, *amlabel, *pmlabel;
lv_obj_t *warninglabel, *wifilabel, *ntplabel, *icallabel;

lv_style_t my_style, time_style, date_style, alarm_style;

void setup()
{
  Serial.begin(115200);
  preferences.begin("clock", false, NULL);
  if (preferences.getBytes("s", &state, sizeof(state)))
  {
    Serial.println("got saved data");
    feed_url.setValue(state.feed_url, sizeof(state.feed_url) - 1);
  }
  else
  {
    Serial.println("no saved data");
    bzero(&state, sizeof(state));
  }

  // Get watch instance
  ttgo = TTGOClass::getWatch();

  // Initialize the hardware
  ttgo->begin();

  ttgo->lvgl_begin();

  ttgo->lvgl_whirling(3);
  // Turn on the backlight
  ttgo->tft->fillScreen(TFT_BLACK);
  ttgo->openBL();

  ledcSetup(1, ALARM_FREQ, 8);

  ledcAttachPin(33, 1);
  xTaskCreate(beep, "beep", 1024, NULL, tskIDLE_PRIORITY, &beeptask);

  // Check if RTC is online
  time_t now = 1643768522; // super twosday
  if (!ttgo->deviceProbe(0x51))
  {
    Serial.println("RTC CHECK FAILED");
    ttgo->tft->fillScreen(TFT_BLACK);
    ttgo->tft->setTextFont(4);
    ttgo->tft->setCursor(0, 0);
    ttgo->tft->setTextColor(TFT_RED);
    ttgo->tft->println("RTC CHECK FAILED");
    delay(5000);
  }
  else
  {
    ticked = 1;

    RTC_Date rtcnow = ttgo->rtc->getDateTime();
    struct tm t = {
        .tm_sec = rtcnow.second,
        .tm_min = rtcnow.minute,
        .tm_hour = rtcnow.hour,
        .tm_mday = rtcnow.day,
        .tm_mon = rtcnow.month - 1,
        .tm_year = rtcnow.year - 1900};
    now = mktime(&t);
    if (now == -1)
    {
      now = 1293843661; // super onesday
    }
  }
  struct timeval tv
  {
    .tv_sec = now, .tv_usec = 0
  };
  settimeofday(&tv, NULL);

  lv_style_init(&my_style);
  lv_style_init(&time_style);
  lv_style_init(&date_style);
  lv_style_init(&alarm_style);

  lv_style_set_text_font(&my_style, LV_STATE_DEFAULT, &lv_font_montserrat_28);
  lv_style_set_text_font(&time_style, LV_STATE_DEFAULT, &dseg_175);
  lv_style_set_text_font(&date_style, LV_STATE_DEFAULT, &lv_font_montserrat_48);
  lv_style_set_text_font(&alarm_style, LV_STATE_DEFAULT, &lv_font_montserrat_38);

  lv_style_set_bg_color(&my_style, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  lv_obj_add_style(lv_scr_act(), LV_OBJ_PART_MAIN, &my_style);
  timelabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_add_style(timelabel, LV_OBJ_PART_MAIN, &time_style);
  lv_obj_set_pos(timelabel, -102, 10);

  tzlabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_set_pos(tzlabel, 80, 185);

  amlabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_set_pos(amlabel, 261, 185);

  pmlabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_set_pos(pmlabel, 399, 185);

  datelabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_add_style(datelabel, LV_OBJ_PART_MAIN, &date_style);
  lv_obj_set_pos(datelabel, 40, 214);

  alarmlabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_add_style(alarmlabel, LV_OBJ_PART_MAIN, &alarm_style);
  lv_obj_set_pos(alarmlabel, 0, 282);

  warninglabel = lv_label_create(lv_scr_act(), NULL);
  lv_obj_set_pos(warninglabel, 480, 289);

  ttgo->button->setClickHandler(clicked);
  ttgo->button->setLongClickHandler(longclicked);

  WiFi.mode(WIFI_STA);
  WiFi.onEvent(andevent);
  // wifiManager.resetSettings();
  wifiManager.addParameter(&feed_url);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setSaveConfigCallback(saveParamsCallback);
  wifiManager.setBreakAfterConfig(true);
  if (wifiManager.autoConnect())
  {
  };

  Serial.println("end of setup");
}

const char *months[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *weekdays[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};

char buffer[80] = {0};

time_t lasttime = 0;

void try_fetch()
{
  last_fetched = time(NULL);
  if (state.feed_url[0] == 0)
  {
    Serial.println("skipping feed fetch; no url");
    last_success = time(NULL);
    return;
  }
  HTTPClient https;
  if (!https.begin(state.feed_url))
  {
    Serial.println("https begin failed");
    return;
  }
  int httpCode = https.GET();

  if (httpCode != 200)
  {
    char buf[128];
    snprintf(buf, sizeof(buf), "http code: %d", httpCode);
    Serial.println(buf);
  }
  if (httpCode <= 0)
  {
    return;
  }

  String payload = https.getString();
  int start = 0;
  int offs = 0;
  int als = 0;
  while (start < payload.length())
  {
    int e = payload.indexOf('\n', start);
    if (e == -1)
    {
      e = payload.length();
    }
    String line = payload.substring(start, e);
    int x = payload.indexOf('\t', start);
    if (x == -1 || x >= e)
    {
      Serial.println("bad tag");
      return;
    }
    String tmp = payload.substring(start, x);
    if (tmp.equals("TZ") && offs < (sizeof(state.offsets) / sizeof(state.offsets[0])))
    {
      start = x + 1;
      x = payload.indexOf('\t', start);
      if (x == -1 || x >= e)
      {
        Serial.println("bad TZ");
        return;
      }
      time_t start_time = payload.substring(start, x).toInt();
      start = x + 1;
      x = payload.indexOf('\t', start);
      if (x == -1 || x >= e)
      {
        Serial.println("bad TZ");
        return;
      }
      uint32_t offset = payload.substring(start, x).toInt();
      start = x + 1;
      state.offsets[offs].start = start_time;
      state.offsets[offs].offset = offset;
      payload.substring(start, e).getBytes(state.offsets[offs].buffer, sizeof(state.offsets[offs].buffer) - 1);
      ++offs;
      state.num_offsets = offs;
    }
    else if (tmp.equals("ALARM") && state.num_alarms < (sizeof(state.alarms) / sizeof(state.alarms[0])))
    {
      start = x + 1;
      x = payload.indexOf('\t', start);
      if (x == -1 || x >= e)
      {
        Serial.println("bad ALARM");
        return;
      }
      time_t start_time = payload.substring(start, x).toInt();
      if (start_time >= last_fetched)
      {
        state.alarms[als].start = start_time - (start_time % 60); // fold alarms to minutes
        start = x + 1;
        payload.substring(start, e).getBytes(state.alarms[als].buffer, sizeof(state.alarms[als].buffer) - 1);
        ++als;
        state.num_alarms = als;
      }
    }
    else if (tmp.equals("COMPLETE"))
    {
      start = x + 1;
      time_t complete_time = payload.substring(start, e).toInt();
      state.num_alarms = als;
      if (last_fetched - 86400 < complete_time && last_fetched + 86400 > complete_time)
      {
        last_success = time(NULL);
      }
    }

    // Serial.println(line);
    start = e + 1;
  }
  save_data("try fetch");
}

void loop()
{
  wifiManager.process();
  time_t now = time(NULL);
  if (ticked || now != lasttime)
  {
    time_t display_now = now;
    time_t alarm_now = now - (now % 60);
    int offset = -1;
    while (offset + 1 < state.num_offsets && state.offsets[offset + 1].start <= now)
    {
      ++offset;
    }
    if (offset >= 0)
    {
      display_now += state.offsets[offset].offset;
    }

    int found_alarm = 0;
    int al = 0;
    time_t altime;
    for (; al < state.num_alarms; ++al)
    {
      if (state.alarms[al].start >= alarm_now && state.alarms[al].start < alarm_now + (3600 * 22))
      {
        altime = state.alarms[al].start;
        if (altime == alarm_now && last_alarm != alarm_now)
        {
          last_alarm = alarm_now;
          if (state.alarm_skip != alarm_now)
          {
            vTaskResume(beeptask);
            delay(1);
          }
        }
        found_alarm = 1;
        break;
      }
    }

    struct tm *t = gmtime(&display_now);
    if (beeping)
    {
      lv_style_set_text_color(&my_style, LV_STATE_DEFAULT, LV_COLOR_GREEN);
      ttgo->setBrightness(255);
    }
    else if ((t->tm_hour <= 7) || (t->tm_hour >= 20))
    {
      lv_style_set_text_color(&my_style, LV_STATE_DEFAULT, LV_COLOR_RED);
      ttgo->setBrightness(96);
    }
    else
    {
      lv_style_set_text_color(&my_style, LV_STATE_DEFAULT, LV_COLOR_WHITE);
      ttgo->setBrightness(255);
    }

    if (offset >= 0)
    {
      lv_label_set_text(tzlabel, (char *)state.offsets[offset].buffer);
    }
    else
    {
      lv_label_set_text_static(tzlabel, "UTC");
    }
    char tens = '!';
    bool ispm = false;

    if (t->tm_hour == 0)
    {
      t->tm_hour = 12;
    }
    else if (t->tm_hour == 12)
    {
      ispm = true;
    }
    else if (t->tm_hour > 12)
    {
      t->tm_hour -= 12;
      ispm = true;
    }
    if (t->tm_hour >= 10)
    {
      tens = '1';
    }

    lv_label_set_text_fmt(timelabel, "%c%01d:%02d", tens, t->tm_hour % 10, t->tm_min);
    if (ispm)
    {
      lv_label_set_text_static(pmlabel, "PM");
      lv_label_set_text_static(amlabel, "");
    }
    else
    {
      lv_label_set_text_static(pmlabel, "");
      lv_label_set_text_static(amlabel, "AM");
    }

    int dayofweek = t->tm_wday;
    lv_label_set_text_fmt(datelabel, "%s %s %d, %d", weekdays[dayofweek], months[t->tm_mon], t->tm_mday, t->tm_year + 1900);

    String alarm_prefix = "";

    if (found_alarm)
    {
      next_alarm = altime;
      if (state.alarm_skip && state.alarm_skip != next_alarm)
      {
        Serial.printf("unset skip %ld != %ld\n", state.alarm_skip, next_alarm);
        state.alarm_skip = 0;
        save_data("unset skip");
      }
      offset = -1;
      while (offset + 1 < state.num_offsets && state.offsets[offset + 1].start <= altime)
      {
        ++offset;
      }
      if (offset >= 0)
      {
        altime += state.offsets[offset].offset;
      }

      t = gmtime(&altime);
      static char am[3] = "AM";
      static char pm[3] = "PM";
      char *ampm = am;
      if (t->tm_hour == 0)
      {
        t->tm_hour = 12;
      }
      else if (t->tm_hour == 12)
      {
        ampm = pm;
      }
      else if (t->tm_hour > 12)
      {
        t->tm_hour -= 12;
        ampm = pm;
      }
      if (beeping)
      {
        alarm_prefix = LV_SYMBOL_EYE_OPEN;
      }
      else if (alarm_now != last_alarm && next_alarm != state.alarm_skip)
      {
        alarm_prefix = LV_SYMBOL_BELL;
      }
      else
      {
        alarm_prefix = LV_SYMBOL_PAUSE;
      }

      lv_label_set_text_fmt(alarmlabel, "%s %d:%02d %s %s", alarm_prefix.c_str(),
                            t->tm_hour, t->tm_min, ampm, state.alarms[al].buffer);
    }
    else
    {
      if (state.alarm_skip != 0 && next_alarm != 0)
      {
        state.alarm_skip = next_alarm = 0;
        save_data("no imminent alarm");
      }
      lv_label_set_text_static(alarmlabel, "no imminent alarm");
    }

    int warn = 0;

    String warning_text = "";

    if (WiFi.status() != WL_CONNECTED)
    {
      warning_text += LV_SYMBOL_WIFI;
      warn = 1;
      if (!beeping && t->tm_sec >= 10 && t->tm_sec < 20)
      {
        lv_label_set_text_fmt(alarmlabel, "%s Wi-Fi Disconnected", alarm_prefix.c_str());
      }
    }

    if (now - last_synced > (3600 * 2))
    {
      warning_text += LV_SYMBOL_REFRESH;
      warn = 1;
      if (!beeping && t->tm_sec >= 20 && t->tm_sec < 30)
      {
        lv_label_set_text_fmt(alarmlabel, "%s no recent NTP sync", alarm_prefix.c_str());
      }
    }

    if (warn == 0 && now - last_fetched > 60 * 60)
    {
      try_fetch();
    }

    if (now - last_success > (3600 * 4))
    {
      warning_text += LV_SYMBOL_BELL;
      warn = 1;
      if (!beeping && t->tm_sec >= 30 && t->tm_sec < 40)
      {
        lv_label_set_text_fmt(alarmlabel, "%s no recent iCal sync", alarm_prefix.c_str());
      }
    }

    if (warn)
    {
      warning_text = LV_SYMBOL_WARNING + warning_text;
    }

    if (wifiManager.getWebPortalActive())
    {
      warning_text += LV_SYMBOL_SETTINGS;
      if (!beeping && t->tm_sec >= 40 && t->tm_sec < 50)
      {
        lv_label_set_text_fmt(alarmlabel, "%s SSID: %s", alarm_prefix.c_str(), wifiManager.getWiFiSSID().c_str());
        lv_label_set_text_fmt(datelabel, "http://%s", WiFi.localIP().toString().c_str());
      }
    }

    if (wifiManager.getConfigPortalActive())
    {
      if (!beeping)
      {
        lv_label_set_text_fmt(alarmlabel, "%s SSID: %s", alarm_prefix.c_str(), wifiManager.getConfigPortalSSID().c_str());
        lv_label_set_text_fmt(datelabel, "http://%s", WiFi.softAPIP().toString().c_str());
        warning_text = "";
      }
    }

    lv_label_set_text(warninglabel, warning_text.c_str());
    lv_obj_set_pos(warninglabel, 480 - ((warning_text.length() / 3) * 30), 289);

    ticked = 0;
    lasttime = now;
    lv_task_handler();
  }

  vTaskDelay(5);
  ttgo->button->loop();
  if (ttgo->touched())
  {
    clicked();
  }
}