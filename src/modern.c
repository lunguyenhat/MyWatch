#include "modern.h"
#include "pebble.h"
#include "string.h"
#include "stdlib.h"
#include "math.h"

#define KEY_BATTERY_PHONE 0
#define KEY_BATTERY_PEBBLE 1

Window *window;
GFont date_font;

Layer *hands_layer;
Layer *battery_layer;
Layer *battery_phone_layer;

TextLayer *day_label;
char day_buffer[10];

TextLayer *num_label;
char num_buffer[10];

TextLayer *battery_phone_label;
char battery_phone_buffer[10];


TextLayer *dktime;
char dktime_buffer[6];

static GPath *minute_arrow;
static GPath *hour_arrow;

static BitmapLayer *s_lightning_layer;
static GBitmap *s_lightning_bitmap;

static BitmapLayer *s_connection_layer;
static GBitmap *s_connection_bitmap;

static int16_t battery_percent;

static struct tm *utc_time;
static int timezone = 7;

DictionaryIterator *iter;

void send_int(uint8_t key, uint8_t cmd)
{
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  Tuplet value = TupletInteger(key, cmd);
  dict_write_tuplet(iter, &value);
  app_message_outbox_send();
}

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  //APP_LOG(APP_LOG_LEVEL_INFO, "Message received!");
  
  //Get data
  Tuple *t = dict_read_first(iter);
  while(t != NULL)
  {
    //Get key
    int key = t->key;
   
    //Get integer value, if present
    int value = t->value->int32;
   
    //Get string value, if present
    char string_value[32];
    strcpy(string_value, t->value->cstring);
   
    //Decide what to do
    switch(key) {
      case KEY_BATTERY_PHONE:
        snprintf(battery_phone_buffer, sizeof(battery_phone_buffer), "%d%%", value);
        text_layer_set_text(battery_phone_label, battery_phone_buffer);
        break;
      
      case KEY_BATTERY_PEBBLE:
        send_int(0, battery_percent);
        break;
    }

    //Get next
    t = dict_read_next(iter);
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  //APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  //APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  //APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

static void update_date(struct tm *t)
{
  strftime(day_buffer, sizeof(day_buffer), "%d %b", t);
  text_layer_set_text(day_label, day_buffer);
  strftime(num_buffer, sizeof(num_buffer), "%A", t);
  text_layer_set_text(num_label, num_buffer);

  // dk time
  strftime(dktime_buffer, sizeof(dktime_buffer), "%R", utc_time);
  text_layer_set_text(dktime, dktime_buffer);
}

static void hands_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const GPoint center = grect_center_point(&bounds);
  const int16_t secondHandLength = (bounds.size.w / 2) - 2;
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  
  GPoint secondHand;

  // minute/hour hand
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  
  gpath_rotate_to(hour_arrow, (TRIG_MAX_ANGLE * (((t->tm_hour % 12) * 6) + (t->tm_min / 10))) / (12 * 6));
  gpath_draw_filled(ctx, hour_arrow);
  gpath_draw_outline(ctx, hour_arrow);
  
  gpath_rotate_to(minute_arrow, TRIG_MAX_ANGLE * t->tm_min / 60);
  gpath_draw_filled(ctx, minute_arrow);
  gpath_draw_outline(ctx, minute_arrow);

  int32_t second_angle = TRIG_MAX_ANGLE * t->tm_sec / 60;
  secondHand.y = (int16_t)(-cos_lookup(second_angle) * (int32_t)secondHandLength / TRIG_MAX_RATIO) + center.y;
  secondHand.x = (int16_t)(sin_lookup(second_angle) * (int32_t)secondHandLength / TRIG_MAX_RATIO) + center.x;

  // second hand
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_line(ctx, secondHand, center);

  // dot in the middle
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(bounds.size.w / 2 - 1, bounds.size.h / 2 - 1, 3, 3), 0, GCornerNone);
  
  if (t->tm_sec == 0)
  {
    time_t utc;
    utc = now - (timezone * 60 * 60) + 3600;
    utc_time = gmtime(&utc);
    
    // dk time
    strftime(dktime_buffer, sizeof(dktime_buffer), "%R", utc_time);
    text_layer_set_text(dktime, dktime_buffer);
  }
  
  // date
  if (t->tm_hour == 0 && t->tm_min == 0 && t->tm_sec == 0)
  {
    update_date(t);
  }
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  
  int16_t battery_width = (int16_t)(battery_percent * 16 / 100);
  
  graphics_draw_rect(ctx, GRect(120, 5, 20, 10));
  graphics_fill_rect(ctx, GRect(122, 7, battery_width, 6), 0, GCornerNone);
}

static void handle_battery(BatteryChargeState charge_state) {
  battery_percent = charge_state.charge_percent;
  layer_set_hidden(bitmap_layer_get_layer(s_lightning_layer), !charge_state.is_charging);
  send_int(0, battery_percent);
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(window_get_root_layer(window));
}

static void handle_bluetooth(bool connected) {
  if (connected)
  {
    s_connection_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH);
  }
  else
  {
    s_connection_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH_DISABLED);
    vibes_short_pulse();
  }
  
  bitmap_layer_set_bitmap(s_connection_layer, s_connection_bitmap);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const GPoint center = grect_center_point(&bounds);
  date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIGITALDREAM_NARROW_12));
 
  // init day
  day_label = text_layer_create(GRect(0, (bounds.size.h - 20), bounds.size.w - 5, 18));
  text_layer_set_text(day_label, day_buffer);
  text_layer_set_background_color(day_label, GColorBlack);
  text_layer_set_text_color(day_label, GColorWhite);
  text_layer_set_font(day_label, date_font);
  text_layer_set_text_alignment(day_label, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(day_label));
  
  // init num
  num_label = text_layer_create(GRect(0, 3, bounds.size.w, 18));
  text_layer_set_text(num_label, num_buffer);
  text_layer_set_background_color(num_label, GColorBlack);
  text_layer_set_text_color(num_label, GColorWhite);
  text_layer_set_font(num_label, date_font);
  text_layer_set_text_alignment(num_label, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(num_label));

  // init hands
  hands_layer = layer_create(bounds);
  layer_set_update_proc(hands_layer, hands_update_proc);
  layer_add_child(window_layer, hands_layer);
  
  // init battery
  battery_layer = layer_create(bounds);
  layer_set_update_proc(battery_layer, battery_update_proc);
  layer_add_child(window_layer, battery_layer);
  
  // int lightning
  s_lightning_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_CHARGE);
  s_lightning_layer = bitmap_layer_create(GRect(108, 5, 10, 10));
  bitmap_layer_set_bitmap(s_lightning_layer, s_lightning_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_lightning_layer));
  
  // init connection
  s_connection_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH);
  s_connection_layer = bitmap_layer_create(GRect(5, 5, 12, 15));
  bitmap_layer_set_bitmap(s_connection_layer, s_connection_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_connection_layer));
  
  // init hand paths
  minute_arrow = gpath_create(&MINUTE_HAND_POINTS);
  hour_arrow = gpath_create(&HOUR_HAND_POINTS);

  gpath_move_to(minute_arrow, center);
  gpath_move_to(hour_arrow, center);
  
  // init dk time
  dktime = text_layer_create(GRect(5, (bounds.size.h - 20), bounds.size.w - 5, 18));
  text_layer_set_text_color(dktime, GColorWhite);
  text_layer_set_background_color(dktime, GColorClear);
  text_layer_set_font(dktime, date_font);
  text_layer_set_text_alignment(dktime, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(dktime));
  
  // init battery phone
  battery_phone_label = text_layer_create(GRect(5, (bounds.size.h - 36), bounds.size.w - 5, 18));
  text_layer_set_text_color(battery_phone_label, GColorWhite);
  text_layer_set_background_color(battery_phone_label, GColorClear);
  text_layer_set_font(battery_phone_label, date_font);
  text_layer_set_text_alignment(battery_phone_label, GTextAlignmentLeft);
  text_layer_set_text(battery_phone_label, battery_phone_buffer);
  layer_add_child(window_layer, text_layer_get_layer(battery_phone_label));
}

static void window_unload(Window *window) {
  layer_destroy(hands_layer);
  
  text_layer_destroy(day_label);
  text_layer_destroy(num_label);
  text_layer_destroy(dktime);
  text_layer_destroy(battery_phone_label);
  
  fonts_unload_custom_font(date_font);
  
  bitmap_layer_destroy(s_lightning_layer);
  bitmap_layer_destroy(s_connection_layer);
}


static void init(void) {
  // Register callbacks
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  // Open AppMessage
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  window = window_create();
  window_set_fullscreen(window, true);
  window_set_background_color(window, GColorBlack);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  day_buffer[0] = '\0';
  num_buffer[0] = '\0';
  dktime_buffer[0] = '\0';
  battery_phone_buffer[0] = '\0';
  
  battery_percent = 100;

  // Push the window onto the stack
  window_stack_push(window, true);
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  handle_second_tick(t, SECOND_UNIT);
  handle_battery(battery_state_service_peek());
  handle_bluetooth(bluetooth_connection_service_peek());
  
  time_t utc;
  utc = now - (timezone * 60 * 60) + 3600;
  utc_time = gmtime(&utc);
  update_date(t);
  
  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
  battery_state_service_subscribe(handle_battery);
  bluetooth_connection_service_subscribe(handle_bluetooth);
}


static void deinit(void) {
  gpath_destroy(minute_arrow);
  gpath_destroy(hour_arrow);
  gbitmap_destroy(s_lightning_bitmap);
  gbitmap_destroy(s_connection_bitmap);

  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
