#include "jack.h"

static struct jack_output_data {
  jack_client_t *client;
  jack_options_t options;
  jack_status_t status;
  jack_port_t* port;

  receiver_format_t receiver_format;
  int latency;
  char *port_name;
} jo_data;

int jack_process(jack_nframes_t nframes, void* arg){
  return 0;
}

int pulse_output_init(int latency, char *port_name)
{
  int error;

  // set application icon
  setenv("PULSE_PROP_application.icon_name", "audio-card", 0);

  // Start with base default format, rate and channels. Will switch to actual format later
  jo_data.ss.format = PA_SAMPLE_S16LE;
  jo_data.ss.rate = 44100;
  jo_data.ss.channels = 2;

  // init receiver format to track changes
  jo_data.receiver_format.sample_rate = 0;
  jo_data.receiver_format.sample_size = 0;
  jo_data.receiver_format.channels = 2;
  jo_data.receiver_format.channel_map = 0x0003;

  jo_data.latency = latency;
  jo_data.port_name = port_name;

  // set buffer size for requested latency
  jo_data.buffer_attr.maxlength = (uint32_t)-1;
  jo_data.buffer_attr.tlength = pa_usec_to_bytes((pa_usec_t)jo_data.latency * 1000u, &jo_data.ss);
  jo_data.buffer_attr.prebuf = (uint32_t)-1;
  jo_data.buffer_attr.minreq = (uint32_t)-1;
  jo_data.buffer_attr.fragsize = (uint32_t)-1;

  jo_data.client = jack_client_open("Scream",
    jo_data.options,
    jo_data.status
  );
  if (!jo_data.client) {
    fprintf(stderr, "Unable to connect to PulseAudio. %s\n", pa_strerror(error));
    return 1;
  }

  jo_data.port = jack_port_register (jo_data.client,
    jo_data.port_name,
    JACK_DEFAULT_AUDIO_TYPE,
    JackPortIsOutput,
    0
  );
  if (!jo_data.port) {
    fprintf(stderr, "Unable to register output port with JACK.\n");
  }

  return 0;
}

void jack_output_destroy()
{
  if (jo_data.client)
    jack_client_close(jo_data.client);
}

int jack_output_send(receiver_data_t *data)
{
  int error;

  receiver_format_t *rf = &data->format;

  if (memcmp(&jo_data.receiver_format, rf, sizeof(receiver_format_t))) {
    // audio format changed, reconfigure
    memcpy(&jo_data.receiver_format, rf, sizeof(receiver_format_t));

    jo_data.ss.channels = rf->channels;
    jo_data.ss.rate = ((rf->sample_rate >= 128) ? 44100 : 48000) * (rf->sample_rate % 128);
    switch (rf->sample_size) {
      case 16: jo_data.ss.format = PA_SAMPLE_S16LE; break;
      case 24: jo_data.ss.format = PA_SAMPLE_S24LE; break;
      case 32: jo_data.ss.format = PA_SAMPLE_S32LE; break;
      default:
        printf("Unsupported sample size %hhu, not playing until next format switch.\n", rf->sample_size);
        jo_data.ss.rate = 0;
    }

    if (rf->channels == 1) {
      pa_channel_map_init_mono(&jo_data.channel_map);
    }
    else if (rf->channels == 2) {
      pa_channel_map_init_stereo(&jo_data.channel_map);
    }
    else {
      pa_channel_map_init(&jo_data.channel_map);
      jo_data.channel_map.channels = rf->channels;
      // k is the key to map a windows SPEAKER_* position to a PA_CHANNEL_POSITION_*
      // it goes from 0 (SPEAKER_FRONT_LEFT) up to 10 (SPEAKER_SIDE_RIGHT) following the order in ksmedia.h
      // the SPEAKER_TOP_* values are not used
      int k = -1;
      for (int i=0; i<rf->channels; i++) {
        for (int j = k+1; j<=10; j++) {// check the channel map bit by bit from lsb to msb, starting from were we left on the previous step
          if ((rf->channel_map >> j) & 0x01) {// if the bit in j position is set then we have the key for this channel
            k = j;
            break;
          }
        }
        // map the key value to a pulseaudio channel position
        switch (k) {
          case  0: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_LEFT; break;
          case  1: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_RIGHT; break;
          case  2: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_CENTER; break;
          case  3: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_LFE; break;
          case  4: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_REAR_LEFT; break;
          case  5: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_REAR_RIGHT; break;
          case  6: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER; break;
          case  7: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER; break;
          case  8: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_REAR_CENTER; break;
          case  9: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_SIDE_LEFT; break;
          case 10: jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_SIDE_RIGHT; break;
          default:
            // center is a safe default, at least it's balanced. This shouldn't happen, but it's better to have a fallback
            printf("Channel %i could not be mapped. Falling back to 'center'.\n", i);
            jo_data.channel_map.map[i] = PA_CHANNEL_POSITION_CENTER;
        }
        const char *channel_name;
        switch (k) {
          case  0: channel_name = "Front Left"; break;
          case  1: channel_name = "Front Right"; break;
          case  2: channel_name = "Front Center"; break;
          case  3: channel_name = "LFE / Subwoofer"; break;
          case  4: channel_name = "Rear Left"; break;
          case  5: channel_name = "Rear Right"; break;
          case  6: channel_name = "Front-Left Center"; break;
          case  7: channel_name = "Front-Right Center"; break;
          case  8: channel_name = "Rear Center"; break;
          case  9: channel_name = "Side Left"; break;
          case 10: channel_name = "Side Right"; break;
          default:
            channel_name = "Unknown. Set to Center.";
        }
        printf("Channel %i mapped to %s\n", i, channel_name);
      }
    }
    // this is for extra safety
    if (!pa_channel_map_valid(&jo_data.channel_map)) {
      printf("Invalid channel mapping, falling back to CHANNEL_MAP_WAVEEX.\n");
      pa_channel_map_init_extend(&jo_data.channel_map, rf->channels, PA_CHANNEL_MAP_WAVEEX);
    }
    if (!pa_channel_map_compatible(&jo_data.channel_map, &jo_data.ss)){
      printf("Incompatible channel mapping.\n");
      jo_data.ss.rate = 0;
    }

    if (jo_data.ss.rate > 0) {
      // sample spec has changed, so the playback buffer size for the requested latency must be recalculated as well
      jo_data.buffer_attr.tlength = pa_usec_to_bytes((pa_usec_t)jo_data.latency * 1000, &jo_data.ss);

      if (jo_data.s) pa_simple_free(jo_data.s);
      jo_data.s = pa_simple_new(NULL,
        "Scream",
        PA_STREAM_PLAYBACK,
        NULL,
        jo_data.port_name,
        &jo_data.ss,
        &jo_data.channel_map,
        &jo_data.buffer_attr,
        NULL
      );
      if (jo_data.s) {
        printf("Switched format to sample rate %u, sample size %hhu and %u channels.\n", jo_data.ss.rate, rf->sample_size, rf->channels);
      }
      else {
        printf("Unable to open PulseAudio with sample rate %u, sample size %hhu and %u channels, not playing until next format switch.\n", jo_data.ss.rate, rf->sample_size, rf->channels);
        jo_data.ss.rate = 0;
      }
    }
  }

  if (!jo_data.ss.rate) return 0;
  if (pa_simple_write(jo_data.s, data->audio, data->audio_size, &error) < 0) {
    fprintf(stderr, "pa_simple_write() failed: %s\n", pa_strerror(error));
    pulse_output_destroy();
    return 1;
  }
  return 0;
}
