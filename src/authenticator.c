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
	 *
	 * At its core, a TOTP token can be defined via the following:
	 * K is predefined as a series of arbitrary bytes forming a secret
	 * C = ( TIME() - T0 ) / Ti, given that TIME() returns the current unix epoch
	 * HS = HMAC(K,C)
	 * D = Truncate(HS)
	 *
	 * It starts off with HS = HMAC(K,C).
	 * We first have K, our authentication key, which is a bytestring typically stored as base32 text
	 * The configure script converts this back to bytes as part of generating the header
	 * We then have T0 and Ti (epoch and validity interval). These are almost always defined as T0 = 0, Ti = 30
	 * We can then derive C, where C = ( TIME() - T0 ) / Ti
	 * From HMAC(K,C) we then derive HS, which is the resulting 160-bit message digest..
	 * We then compute the HOTP, which takes HS and applies dynamic truncation.
	 * Dynamic truncation involves calculating first the offset, defined as O.
	 * We calculate O as being the lower four bits of the last byte in HS.
	 * Thus, O = HS[DIGEST_LENGTH - 1] & 0x0F
	 * From this, we truncate HS to four bytes beginning at byte O, which may be anywhere from 0 to F (15), to obtain P.
	 * Thus, P = HS[O+0..3]
	 * After truncating, we strip the most significant bit in order to prevent P from being interpreted as signed.
	 * Thus, P = P & 0x7FFFFFFF
	 * Finally, in order to derive D as a number below 1000000, we modulo P against this.
	 * Thus, D = P % 1000000.
	 * With D now known, we have our TOTP token.
	 *
	 * This formula makes the assumption that the desired token length is 6 digits, however this is a fairly safe assumption to make.
	 */

	// Get the current epoch time and store it in a reasonable way for sha1 operations
	long epoch = time(NULL)/30;
	uint8_t sha1time[8];
	for(int i=8;i--;epoch >>= 8) sha1time[i] = epoch;
	
	sha1nfo s;
	// We first get HMAC(K,C) where K is our secret and C is our message (the time)
	sha1_initHmac(&s, otp_keys[token], otp_sizes[token]);
	sha1_write(&s, (char*)sha1time, 8);
	sha1_resultHmac(&s);
	
	// Now that we have the message digest (HS), we can move on to HOTP.
	// HOTP is HMAC with dynamic truncation. An explanation of dynamic truncation with HMAC is below:
	// Since HS (the result of HMAC(K,C)) is a 160-bit (20-byte) hash, we take lower four bits of the last byte (HS[DIGEST_LENGTH-1] & 0x0F)
	// And use that as the beginning of our truncated bytes. We truncate to four bytes, so this method allows for truncation anywhere in HS
	// While guaranteeing that there will be three bytes to the right of the offset.
	// We first obtain the offset, O.
	uint8_t offset = s.state.b[HASH_LENGTH-1] & 0x0F;
	uint32_t otp = 0;
	// We then truncate to the four bytes beginning at O, to obtain P.
	otp = s.state.b[offset] << 24 | s.state.b[offset + 1] << 16 | s.state.b[offset + 2] << 8 | s.state.b[offset + 3];
	// Then strip the topmost bit to prevent it being handled as a signed integer.
	otp &= 0x7FFFFFFF;
	// To obtain D as something we can display as a six-digit integer, modulo by 1000000
	otp %= 1000000;
	// Return the result.
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
