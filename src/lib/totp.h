#include <math.h>
#include <pebble.h>
#include "sha1.h"

//T0 is "Time zero"; typically this is actually zero but may be any time in the past, but not before unix epoch 0.
#define TOTP_T0 0
//Ti is time interval; typically this is 30 to denote token validity of 30 seconds.
#define TOTP_Ti 30
//LEN is length; typically this is 6 but may be as high as 8 (but no higher). Future revisions of totpebble will make this configurable.
#define TOTP_LEN 6

uint32_t get_token(time_t time_utc, unsigned char key[], uint32_t ksize);