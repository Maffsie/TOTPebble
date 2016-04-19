#include <math.h>
#include <pebble.h>
#include "sha1.h"

#define TOTP_T0 0
#define TOTP_Ti 30
#define TOTP_LEN 6

uint32_t get_token(time_t time_utc, unsigned char key[], uint32_t ksize);