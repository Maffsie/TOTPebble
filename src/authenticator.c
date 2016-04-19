#include <pebble.h>
#include "configuration.h"
#include "lib/sha1.h"

static Window      *window;
static TextLayer   *label_layer;
static TextLayer   *token_layer;
static BitmapLayer *ticker_gfx_layer;
static TextLayer   *ticker_layer;

static int          token;
static bool         token_valid = false;

//Persistent storage
//KEY_TOKEN: Stores which token was last viewed.
enum { KEY_TOKEN };

//80ms pulse to alert the user that the token has expired
static void vibes_tiny_pulse(void) {
	vibes_enqueue_custom_pattern((VibePattern) {
		.durations = (uint32_t[]){ 80 },
		.num_segments = 1,
	});
}

//Make use of the unused centre button on the pebble; lights up the screen for 15 seconds.
static void deilluminate(void *ctx) {
	light_enable(false);
}
static void illuminate(int secs) {
	light_enable(true);
	app_timer_register(secs*1000, deilluminate, NULL);
}

static uint32_t get_token(time_t time_utc) {
	/* TOTP calculation is fun.
	 * We first have K, our authentication key, which is a bytestring stored as base32 text
	 * The configure script converts this back to bytes as part of generating the header
	 * We then have T0 and TI (epoch and validity interval). By default, these are 1/1/1900 and 30
	 * I've never heard of anything that used custom epochs or intervals.
	 * We then have our hash method, SHA-1, which is used to generate the token
	 * Finally, we have TOTP token length. By default this is 6, some services use different lengths.
	 */
	sha1nfo s;
	

	// TOTP uses seconds since epoch in the upper half of an 8 byte payload
	// TOTP is HOTP with a time based payload
	// HOTP is HMAC with a truncation function to get a short decimal key

	long epoch = time(NULL)/30;
	APP_LOG(APP_LOG_LEVEL_DEBUG,"time_utc %u epoch %l diff %u",(unsigned int)time_utc,epoch,(unsigned int)(time_utc%30));

	uint8_t sha1time;
	for(int i=8;i--;epoch >>= 8) sha1time[i] = epoch;
	APP_LOG(APP_LOG_LEVEL_DEBUG,"sha1time: 0x%X",(unsigned int)sha1time);
	//sha1_time[4] = (epoch >> 24) & 0xFF;
	//sha1_time[5] = (epoch >> 16) & 0xFF;
	//sha1_time[6] = (epoch >> 8 ) & 0xFF;
	//sha1_time[7] =  epoch        & 0xFF;
	
	//We first get HMAC(K,C) where K is our secret and C is our message (the time)
	sha1_initHmac(&s, otp_keys[token], otp_sizes[token]);
	sha1_write(&s, (char*)sha1time, 8);
	sha1_resultHmac(&s);
	
	//offset = the offset at which we should truncate. This is computed as (HS length - 1) & 0xF (so where HS length is 20, the end result is 3)
	//Thus, our offset is 4 bytes
	uint8_t offset = s.state.b[HASH_LENGTH-1] & 0x0F;
	uint32_t otp = 0;
	//We then truncate
	otp = s.state.b[offset] << 24 | s.state.b[offset + 1] << 16 | s.state.b[offset + 2] << 8 | s.state.b[offset + 3];
	APP_LOG(APP_LOG_LEVEL_DEBUG,"4 bytes beginning at offset 0x%X: 0x%X",offset,(unsigned int)otp);
	//Then strip the top half byte to prevent something I forget what
	otp &= 0x7FFFFFFF;
	APP_LOG(APP_LOG_LEVEL_DEBUG,"final uint32 0x%X",(unsigned int)otp);
	//To turn it into something we can display as a six-digit integer, modulo by 1000000
	otp %= 1000000;
	APP_LOG(APP_LOG_LEVEL_DEBUG,"totp %u formatted %06lu",(unsigned int)otp,otp);
	return otp;
}

static void render_ticker(Layer *layer, GContext *ctx) {
	//get current time
	time_t curtime_utc = time(NULL);
	struct tm *curtime = localtime(&curtime_utc);
	//Draw outer circle
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_circle(ctx, GPoint(71,22), 19);
	//Erase part of the outer circle that we don't want
	graphics_context_set_fill_color(ctx, GColorBlack);
	graphics_fill_circle(ctx, GPoint(71,22), 17);
	//Calculate arc size
	int validity = 30 - (curtime->tm_sec % 30);
	int start = DEG_TO_TRIGANGLE(0);
	int end = DEG_TO_TRIGANGLE(12*validity);
	//Draw arc
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_stroke_color(ctx, GColorWhite);
	graphics_context_set_stroke_width(ctx, 4);
	graphics_draw_arc(ctx,
		(GRect) {
			.origin = GPoint(54,5),
			.size = GSize(35,35)
		},
		GOvalScaleModeFitCircle,start,end);
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
	int validity = 30 - (tick_time->tm_sec % 30);
	if (validity == 30) {
		vibes_tiny_pulse();
		illuminate(3);
	}
	if (!token_valid || validity == 30) {
		token_valid = true;
		static char token_text[] = "000000";
		snprintf(token_text, sizeof(token_text), "%06lu", get_token(mktime(tick_time)));
		text_layer_set_text(label_layer, otp_labels[token]);
		text_layer_set_text(token_layer, token_text);
	}
	static char ticker_text[] = "00";
	snprintf(ticker_text, sizeof(ticker_text), "%d", validity);
	text_layer_set_text(ticker_layer, ticker_text);
}

static void click_handler(ClickRecognizerRef recognizer, Window *window) {
	switch ((int)click_recognizer_get_button_id(recognizer)) {
		case BUTTON_ID_UP:
			token = (token - 1 + NUM_SECRETS) % NUM_SECRETS;
			token_valid = false;
			break;
		case BUTTON_ID_DOWN:
			token = (token + 1) % NUM_SECRETS;
			token_valid = false;
			break;
		case BUTTON_ID_SELECT:
			illuminate(15);
			break;
	}
	time_t t = time(NULL);
	handle_second_tick(gmtime(&t), SECOND_UNIT);
}

static void click_config_provider(void *ctx) {
	const uint16_t repeat_interval_ms = 100;
	window_single_repeating_click_subscribe(BUTTON_ID_UP    , repeat_interval_ms, (ClickHandler) click_handler);
	window_single_repeating_click_subscribe(BUTTON_ID_DOWN  , repeat_interval_ms, (ClickHandler) click_handler);
	window_single_repeating_click_subscribe(BUTTON_ID_SELECT, repeat_interval_ms, (ClickHandler) click_handler);
}

static void window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	label_layer = text_layer_create((GRect) { .origin = { 0, 8 }, .size = bounds.size });
	text_layer_set_text_color(label_layer, GColorWhite);
	text_layer_set_background_color(label_layer, GColorClear);
	text_layer_set_font(label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
	text_layer_set_text_alignment(label_layer, GTextAlignmentCenter);

	token_layer = text_layer_create((GRect) { .origin = { 0, 44 }, .size = bounds.size });
	text_layer_set_text_color(token_layer, GColorWhite);
	text_layer_set_background_color(token_layer, GColorClear);
	text_layer_set_font(token_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HELVETICA_NEUE_ULTRALIGHT_42)));
	text_layer_set_text_alignment(token_layer, GTextAlignmentCenter);

	ticker_gfx_layer = bitmap_layer_create((GRect) { .origin = { 0, 100 }, .size = bounds.size });
	layer_set_update_proc(bitmap_layer_get_layer(ticker_gfx_layer), &render_ticker);

	ticker_layer = text_layer_create((GRect) { .origin = { 0, 110 }, .size = bounds.size });
	text_layer_set_text_color(ticker_layer, GColorWhite);
	text_layer_set_background_color(ticker_layer, GColorClear);
	text_layer_set_font(ticker_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	text_layer_set_text_alignment(ticker_layer, GTextAlignmentCenter);

	layer_add_child(window_layer, text_layer_get_layer(label_layer));
	layer_add_child(window_layer, text_layer_get_layer(token_layer));
	layer_add_child(window_layer, bitmap_layer_get_layer(ticker_gfx_layer));
	layer_add_child(window_layer, text_layer_get_layer(ticker_layer));

	tick_timer_service_subscribe(SECOND_UNIT, &handle_second_tick);
}

static void window_unload(Window *window) {
	tick_timer_service_unsubscribe();
	text_layer_destroy(label_layer);
	text_layer_destroy(token_layer);
	text_layer_destroy(ticker_layer);
	bitmap_layer_destroy(ticker_gfx_layer);
}

static void init(void) {
	token = persist_exists(KEY_TOKEN) ? persist_read_int(KEY_TOKEN) : 0;
	token_valid = false;

	window = window_create();
	window_set_click_config_provider(window, click_config_provider);
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload,
	});
	window_stack_push(window, true);
	window_set_background_color(window, GColorBlack);

	time_t t = time(NULL);
	handle_second_tick(gmtime(&t), SECOND_UNIT);
}

static void deinit(void) {
	persist_write_int(KEY_TOKEN, token);
	light_enable(false);
	window_destroy(window);
}

int main(void) {
	init();
	app_event_loop();
	deinit();
}
