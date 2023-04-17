#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/gpio.h>

#include "message_channel.h"


/* Register log module */
LOG_MODULE_REGISTER(water_switch);

#define WATER_PUMP_PIN  10 // P1.10 (D8)
const static struct device *digital_pin_dev;

static bool configured = false;

void configure() 
{
        digital_pin_dev  = DEVICE_DT_GET(DT_NODELABEL(gpio1));
        if (!digital_pin_dev) {
            LOG_ERR("device gpio1 not found\n");
            return;
        }

        if (!device_is_ready(digital_pin_dev)) {
            LOG_ERR("GPIO controller not ready");
            return;
        }

        gpio_pin_configure(digital_pin_dev, WATER_PUMP_PIN, GPIO_OUTPUT); 
}

void water_switch_callback(const struct zbus_channel *chan)
{
	int err = 0;
	const enum water_switch_status *status;

	if (&WATER_SWITCH_CHAN == chan) {
                if (!configured) {
                    configure();
                    configured = true;
	            LOG_INF("GPIO pin configured");
                }

		/* Get network status from channel. */
		status = zbus_chan_const_msg(chan);

	        LOG_INF("status =  %d", *status);

		switch (*status) {
		case SWITCH_OFF:
                        err = gpio_pin_set(digital_pin_dev, WATER_PUMP_PIN, 0);
			if (err) {
				LOG_ERR("digital pin, error: %d", err);
			} else {
	                    LOG_INF("SWITCH_OFF:  %d", err);
                        }
			break;
		case SWITCH_ON:
                        err = gpio_pin_set(digital_pin_dev, WATER_PUMP_PIN, 1);
			if (err) {
				LOG_ERR("digital pin, error: %d", err);
			} else {
	                    LOG_INF("SWITCH_ON:  %d", err);
                        }
			break;
		default:
			LOG_ERR("Unknown event: %d", *status);
			break;
		}
	}
}

/* Register listener - water_switch_callback will be called everytime a channel that the module listens on
 * receives a new message.
 */
ZBUS_LISTENER_DEFINE(water_switch, water_switch_callback);
