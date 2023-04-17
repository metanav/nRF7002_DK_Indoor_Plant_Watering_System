#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/adc.h>
#include <hal/nrf_saadc.h>

#include "message_channel.h"

#define ADC_DEVICE_NAME DT_INST(0, nordic_nrf_saadc)
#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_CHANNEL_ID 0
#define ADC_CHANNEL_INPUT NRF_SAADC_INPUT_AIN0 // P0.04(A0) pin

#define BUFFER_SIZE 8
#define FORMAT_STRING "{\"soil_moisture\": %d}"

/* Register log module */
LOG_MODULE_REGISTER(sampler, CONFIG_MQTT_SAMPLE_SAMPLER_LOG_LEVEL);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(sampler, CONFIG_MQTT_SAMPLE_SAMPLER_MESSAGE_QUEUE_SIZE);

static int16_t m_sample_buffer[BUFFER_SIZE];
const struct device *adc_dev;

static const struct adc_channel_cfg m_channel_cfg = {
    .gain = ADC_GAIN,
    .reference = ADC_REFERENCE,
    .acquisition_time = ADC_ACQUISITION_TIME,
    .channel_id = ADC_CHANNEL_ID,
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
    .input_positive = ADC_CHANNEL_INPUT,
#endif
};

const struct adc_sequence_options sequence_opts = {
    .interval_us = 0,
    .callback = NULL,
    .user_data = NULL,
    .extra_samplings = BUFFER_SIZE-1,
};

static long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static void sample(void)
{
	struct payload payload = { 0 };
	int err, len;

        const struct adc_sequence sequence = {
            .options = &sequence_opts,
            .channels = BIT(ADC_CHANNEL_ID),
            .buffer = m_sample_buffer,
            .buffer_size = sizeof(m_sample_buffer),
            .resolution = ADC_RESOLUTION
        };

        if (!adc_dev) {
            return;
        }

        err = adc_read(adc_dev, &sequence);
 
        if (err) {
            LOG_ERR("Error in adc sampling: %d\n", err);
            return;
        }

        int sum_samples = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sum_samples += m_sample_buffer[i];
            LOG_INF("%d\n", m_sample_buffer[i]);
        }

        uint16_t avg_adc_val = (uint16_t) (sum_samples/BUFFER_SIZE);

        uint8_t per_soil_moisture = (uint8_t) map(avg_adc_val, 550, 400, 0, 100);

	len = snprintk(payload.string, sizeof(payload.string), FORMAT_STRING, per_soil_moisture);

	if ((len < 0) || (len >= sizeof(payload))) {
		LOG_ERR("Failed to construct message, error: %d", len);
		SEND_FATAL_ERROR();
		return;
	}

	err = zbus_chan_pub(&PAYLOAD_CHAN, &payload, K_SECONDS(1));
	if (err) {
		LOG_ERR("zbus_chan_pub, error:%d", err);
		SEND_FATAL_ERROR();
	}
}

static void sampler_task(void)
{
        adc_dev = DEVICE_DT_GET(ADC_DEVICE_NAME);

        if (!adc_dev) {
            LOG_ERR("device_get_binding ADC_0 failed\n");
            return;
        }

        int err;
        err = adc_channel_setup(adc_dev, &m_channel_cfg);

        if (err) {
            LOG_ERR("Error in adc setup: %d\n", err);
        }

        /* Trigger offset calibration
         * As this generates a _DONE and _RESULT event
         * the first result will be incorrect.
         */
        NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;

	const struct zbus_channel *chan;

	while (!zbus_sub_wait(&sampler, &chan, K_FOREVER)) {
		if (&TRIGGER_CHAN == chan) {
			sample();
		}
	}
}

K_THREAD_DEFINE(sampler_task_id,
		CONFIG_MQTT_SAMPLE_SAMPLER_THREAD_STACK_SIZE,
		sampler_task, NULL, NULL, NULL, 3, 0, 0);
