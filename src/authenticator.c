#include <pebble.h>
#include "configuration.h"
#include "sha1.h"

static Window      *window;
static TextLayer   *label_layer;
static TextLayer   *token_layer;
static BitmapLayer *ticker_gfx_layer;
static TextLayer   *ticker_layer;

static int          token;
static bool         token_valid = false;
static float        timezone    = DEFAULT_TIME_ZONE;
static struct tm   *curtime;

//Persistent storage
//KEY_TOKEN: Stores which token was last viewed.
enum { KEY_TOKEN };

//80ms pulse to alert the user that the token has expired
static void vibes_tiny_pulse() {
	vibes_enqueue_custom_pattern((VibePattern) {
		.durations = (uint32_t[]){ 80 },
		.num_segments = 1,
	});
}

//Make use of the unused centre button on the pebble; lights up the screen for 15 seconds.
static void deilluminate(void *ctx) {
	light_enable(false);
}
static void illuminate(void) {
	light_enable(true);
	app_timer_register(15000, deilluminate, NULL);
}

static uint32_t get_token(void) {
	sha1nfo s;
	uint8_t ofs;
	uint32_t otp;
	char sha1_time[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	// TOTP uses seconds since epoch in the upper half of an 8 byte payload
	// TOTP is HOTP with a time based payload
	// HOTP is HMAC with a truncation function to get a short decimal key
	uint32_t epoch = time(NULL);
	epoch -= (int)(3600*timezone);
	epoch /= 30;

	sha1_time[4] = (epoch >> 24) & 0xFF;
	sha1_time[5] = (epoch >> 16) & 0xFF;
	sha1_time[6] = (epoch >> 8 ) & 0xFF;
	sha1_time[7] =  epoch        & 0xFF;

	// First get the HMAC hash of the time payload with the shared key
	sha1_initHmac(&s, otp_keys[token], otp_sizes[token]);
	sha1_write(&s, sha1_time, 8);
	sha1_resultHmac(&s);

	// Then do the HOTP truncation. HOTP pulls its result from a 31-bit byte
	// aligned window in the HMAC result, then lops off digits to the left
	// over 6 digits.
	ofs = s.state.b[HASH_LENGTH-1] & 0xf;
	otp = 0;
	otp = ((s.state.b[ofs] & 0x7f) << 24) |
		((s.state.b[ofs + 1] & 0xff) << 16) |
		((s.state.b[ofs + 2] & 0xff) << 8 ) |
		( s.state.b[ofs + 3] & 0xff);
	otp %= 1000000;

	return otp;
}

// arc-drawing functions from https://raw.githubusercontent.com/Jnmattern/Arc_2.0/master/src/Arc_2.0.c
static void graphics_draw_arc(GContext *ctx, GPoint center, int radius, int thickness, int start_angle, int end_angle, GColor c) {
	int32_t xmin = 65535000, xmax = -65535000, ymin = 65535000, ymax = -65535000;
	int32_t cosStart, sinStart, cosEnd, sinEnd, r, t;
	int angle_90 = TRIG_MAX_ANGLE / 4;
	int angle_180 = TRIG_MAX_ANGLE / 2;
	int angle_270 = 3*TRIG_MAX_ANGLE/4;

	while (start_angle < 0) start_angle += TRIG_MAX_ANGLE;
	while (end_angle < 0) end_angle += TRIG_MAX_ANGLE;

	start_angle %= TRIG_MAX_ANGLE;
	end_angle %= TRIG_MAX_ANGLE;

	if (end_angle == 0) end_angle = TRIG_MAX_ANGLE;

	if (start_angle > end_angle) {
		graphics_draw_arc(ctx, center, radius, thickness, start_angle, TRIG_MAX_ANGLE, c);
		graphics_draw_arc(ctx, center, radius, thickness, 0, end_angle, c);
		return;
	}
	// Calculate bounding box for the arc to be drawn
	cosStart = cos_lookup(start_angle);
	sinStart = sin_lookup(start_angle);
	cosEnd = cos_lookup(end_angle);
	sinEnd = sin_lookup(end_angle);

	r = radius;
	// Point 1: radius & start_angle
	t = r * cosStart;
	if (t < xmin) xmin = t;
	if (t > xmax) xmax = t;
	t = r * sinStart;
	if (t < ymin) ymin = t;
	if (t > ymax) ymax = t;

	// Point 2: radius & end_angle
	t = r * cosEnd;
	if (t < xmin) xmin = t;
	if (t > xmax) xmax = t;
	t = r * sinEnd;
	if (t < ymin) ymin = t;
	if (t > ymax) ymax = t;

	r = radius - thickness;
	// Point 3: radius-thickness & start_angle
	t = r * cosStart;
	if (t < xmin) xmin = t;
	if (t > xmax) xmax = t;
	t = r * sinStart;
	if (t < ymin) ymin = t;
	if (t > ymax) ymax = t;

	// Point 4: radius-thickness & end_angle
	t = r * cosEnd;
	if (t < xmin) xmin = t;
	if (t > xmax) xmax = t;
	t = r * sinEnd;
	if (t < ymin) ymin = t;
	if (t > ymax) ymax = t;

	// Normalization
	xmin /= TRIG_MAX_RATIO;
	xmax /= TRIG_MAX_RATIO;
	ymin /= TRIG_MAX_RATIO;
	ymax /= TRIG_MAX_RATIO;

	// Corrections if arc crosses X or Y axis
	if ((start_angle < angle_90) && (end_angle > angle_90))
		ymax = radius;
	if ((start_angle < angle_180) && (end_angle > angle_180))
		xmin = -radius;
	if ((start_angle < angle_270) && (end_angle > angle_270))
		ymin = -radius;

	// Slopes for the two sides of the arc
	float sslope = (float)cosStart / (float)sinStart;
	float eslope = (float)cosEnd   / (float)sinEnd;

	if (end_angle == TRIG_MAX_ANGLE) eslope = -1000000;

	int ir2 = (radius - thickness) * (radius - thickness);
	int or2 = radius * radius;

	graphics_context_set_stroke_color(ctx, c);

	for (int x = xmin; x <= xmax; x++) { for (int y = ymin; y <= ymax; y++) {
		int x2 = x * x;
		int y2 = y * y;
		if ((x2 + y2 < or2 && x2 + y2 >= ir2) && (
				( (y >  0 && start_angle <  angle_180 && x <= y * sslope) ||
					(y <  0 && start_angle >  angle_180 && x >= y * sslope) ||
					(y <  0 && start_angle <= angle_180 )                   ||
					(y == 0 && start_angle <= angle_180 && x <  0)          ||
					(y == 0 && start_angle == 0         && x >  0))         &&
				( (y >  0 && end_angle   <  angle_180 && x >= y * eslope) ||
					(y <  0 && end_angle   >  angle_180 && x <= y * eslope) ||
					(y >  0 && end_angle   >= angle_180 )                   ||
					(y == 0 && end_angle   >= angle_180 && x <  0)          ||
					(y == 0 && start_angle == 0         && x >  0))
			))
			graphics_draw_pixel(ctx, GPoint(center.x+x, center.y+y));
	}}
}

static void render_ticker(Layer *layer, GContext *ctx) {
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_circle(ctx, GPoint(71,22), 19);
	graphics_context_set_fill_color(ctx, GColorBlack);
	graphics_fill_circle(ctx, GPoint(71,22), 17);
	int start = 3*TRIG_MAX_ANGLE/4;
	int end = ((30-(curtime->tm_sec%30))*(TRIG_MAX_ANGLE/30))-(TRIG_MAX_ANGLE/4);
	graphics_draw_arc(ctx, GPoint(71,22), 19, 4, start, end, GColorWhite);
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
	int validity = 30 - (tick_time->tm_sec % 30); curtime=tick_time;
	if (validity == 30) vibes_tiny_pulse();
	if (!token_valid || validity == 30) {
		token_valid = true;
		static char token_text[] = "000000";
		snprintf(token_text, sizeof(token_text), "%06lu", get_token());
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
			illuminate();
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
	layer_set_update_proc(bitmap_layer_get_layer(ticker_gfx_layer), render_ticker);

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
