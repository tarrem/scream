#ifndef JACK_H
#define JACK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jack/jack.h>

#include "scream.h"

int jack_output_init(int latency, char *stream_name);
int jack_output_send(receiver_data_t *data);

#endif
