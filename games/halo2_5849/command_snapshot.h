/* Private diagnostic output: shader words/constants can contain owned game
 * data. Capture only when stopped, never in the normal submission loop. */
#pragma once
#include <stdio.h>
#include "host_channel.h"
/* Returns zero on invalid arguments or stream failure. Does not flush/close
 * the caller's stream, execute methods, or read guest memory. */
int h2_command_snapshot(FILE *output, const h2_host_channel *channel);
