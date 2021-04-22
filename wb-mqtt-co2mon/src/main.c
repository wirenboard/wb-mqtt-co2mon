/*
 * co2mon - programming interface to CO2 sensor.
 * Copyright (C) 2015  Oleg Bulatov <oleg@bulatov.me>

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "co2mon.h"
#include <mosquitto.h>


#define CODE_TEMP 0x42
#define CODE_CO2 0x50
#define CODE_HUMIDITY 0x41

uint16_t co2mon_data[256];

struct mosquitto *mosq = NULL;

static double decode_temperature(uint16_t w)
{
    return (double)w * 0.0625 - 273.15;
}

static void publish_mqtt_error(const char * control, const char * error) {
    static char buf[100];
    snprintf(buf, sizeof(buf), "/devices/co2mon/controls/%s/meta/error", control);
    mosquitto_publish(mosq, NULL, buf, error ? strlen(error) : 0, error, 2, true);
}


static void _set_control_error(const char * control, char * error_cache, const char * error) {
    if (error != NULL) {
        if (!!strcmp(error_cache, error)) {
            strncpy(error_cache, error, sizeof(error_cache)/sizeof(error_cache[0]));
            publish_mqtt_error(control, error);
        }
    } else {
        if (error_cache[0]) {
            error_cache[0] = 0;
            publish_mqtt_error(control, 0);
        }
    }
}

static void set_temp_error(const char * error) {
    static char error_cache[100] = "";
    _set_control_error("temperature" , error_cache, error);
}
static void set_co2_error(const char * error) {
    static char error_cache[100] = "";
    _set_control_error("co2", error_cache, error);
}

static void set_errors() {
    set_temp_error("r");
    set_co2_error("r");
}

static void device_loop(co2mon_device dev)
{
    co2mon_magic_table_t magic_table = {0};
    co2mon_data_t result;
	char buffer[15];


    if (!co2mon_send_magic_table(dev, magic_table)) {
        fprintf(stderr, "Unable to send magic table to CO2 device\n");
        set_errors();
        return;
    }

    printf("Sending values to MQTT...\n");

    while (1) {
        int r = co2mon_read_data(dev, magic_table, result);
        if (r == LIBUSB_ERROR_NO_DEVICE) {
            fprintf(stderr, "Device has been disconnected\n");
            set_errors();
            break;
        } else if (r <= 0) {
            set_errors();
            sleep(1);
            continue;
        }

        if (result[4] != 0x0d) {
            fprintf(stderr, "Unexpected data from device (data[4] = %02hhx, await 0x0d)\n", result[4]);
            set_errors();
            continue;
        }

        unsigned char r0, r1, r2, r3, checksum;
        r0 = result[0];
        r1 = result[1];
        r2 = result[2];
        r3 = result[3];
        checksum = r0 + r1 + r2;
        if (checksum != r3) {
            fprintf(stderr, "checksum error (%02hhx, await %02hhx)\n", checksum, r3);
            set_errors();
            continue;
        }

        uint16_t w = (result[1] << 8) + result[2];

        switch (r0) {
	        case CODE_TEMP:
				snprintf(buffer, 15, "%2.1f", decode_temperature(w));
	            mosquitto_publish(mosq, NULL, "/devices/co2mon/controls/temperature", strlen(buffer), buffer, 2, true);
                set_temp_error(0);
	            break;
	        case CODE_CO2:
                if ((unsigned)w > 3000) {
                    // Avoid reading spurious (uninitialized?) data
                    break;
                }
				snprintf(buffer, 15, "%d", w);
	            mosquitto_publish(mosq, NULL, "/devices/co2mon/controls/co2", strlen(buffer), buffer, 2, true);
                set_co2_error(0);
	            break;
	        //~ case CODE_HUMIDITY:
				//~ if (w != 0) {
					//~ snprintf(buffer, 15, "%2.2f", ((float) w) / 10000);
		            //~ mosquitto_publish(mosq, NULL, "/devices/co2mon/controls/humidity", strlen(buffer), buffer, 2, true);
				//~ }
	            //~ break;
        }
    }
}

void monitor_loop()
{
    bool show_no_device = true;
    while (1) {
        co2mon_device dev = co2mon_open_device();
        if (dev == NULL) {
            if (show_no_device) {
                fprintf(stderr, "Unable to open CO2 device\n");
                show_no_device = false;
            }
            set_temp_error("r");
            set_co2_error("r");
        } else {
            show_no_device = true;

            char path[16];
            if (co2mon_device_path(dev, path, 16)) {
                printf("Path: %s\n", path);
            } else {
                printf("Path: (error)\n");
            }

            device_loop(dev);

            co2mon_close_device(dev);
        }
        sleep(1);
    }
    return ;
}


void publish_mqtt_meta()
{
	char* str;

	str = "temperature";
	mosquitto_publish(mosq, NULL, "/devices/co2mon/controls/temperature/meta/type", strlen(str), str , 2, true);

	str = "concentration";
	mosquitto_publish(mosq, NULL, "/devices/co2mon/controls/co2/meta/type", strlen(str), str , 2, true);

	str = "CO2 Monitor";
	mosquitto_publish(mosq, NULL, "/devices/co2mon/meta/name", strlen(str), str , 2, true);
}




int main(int argc, char *argv[])
{
    int rc;
    int c;

	int port = 1883;
	int tmp;
	char * endptr = 0;
	char * host = "127.0.0.1";
    int decode_data = 1;

    while ( (c = getopt(argc, argv, "h:p:n")) != -1) {
        switch (c) {
        case 'p':
            tmp = strtol(optarg, &endptr, 0);
            if (*endptr == '\0') {
				port = tmp;
			} else {
				fprintf(stderr, "Warning: Cannot convert -p argument to integer, ignored\n");
			}

            break;
        case 'h':
            host = optarg;
            break;
        case 'n':
            decode_data = 0;
            break;
        case '?':
            break;
        default:
            printf ("?? getopt returned character code 0%o ??\n", c);
        }
    }


    rc = co2mon_init(decode_data);
    if (rc < 0) {
        return rc;
    }

	mosquitto_lib_init();

	mosq = mosquitto_new(NULL, true, NULL);
	if (!mosq) {
		switch (errno) {
			case ENOMEM:
				fprintf(stderr, "Error: Out of memory.\n");
				break;
			case EINVAL:
				fprintf(stderr, "Error: Invalid id and/or clean_session.\n");
				break;
		}

		mosquitto_lib_cleanup();
		return 1;
	}

	rc = mosquitto_connect(mosq, host, port, 5);
	if (rc) {
		fprintf(stderr, "Error: Cannot connect to MQTT broker\n");
		return rc;
	}


	publish_mqtt_meta();

	mosquitto_loop_start(mosq);
	monitor_loop();

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

    co2mon_exit();
    return 0;
}
