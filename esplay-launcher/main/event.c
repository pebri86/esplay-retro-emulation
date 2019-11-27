#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <event.h>
#include <gamepad.h>

#define REPEAT_HOLDOFF (250 / portTICK_PERIOD_MS)
#define REPEAT_RATE (80 / portTICK_PERIOD_MS)

static QueueHandle_t event_queue;

static void keypad_task(void *arg)
{
	event_t event;
	uint16_t changes;

	TickType_t down_ticks[4];
	uint16_t repeat_count[4];

	while (true) {
		vTaskDelay(10 / portTICK_PERIOD_MS);
		event.type = EVENT_TYPE_KEYPAD;
		event.keypad.state = keypad_debounce(keypad_sample(), &changes);
		event.keypad.pressed = event.keypad.state & changes;
		event.keypad.released = ~event.keypad.state & changes;

		TickType_t now = xTaskGetTickCount();
		for (int i = 0; i < 4; i++) {
			if ((event.keypad.pressed >> i) & 1) {
				down_ticks[i] = now;
				repeat_count[i] = UINT16_MAX;
			} else if ((event.keypad.state >> i) & 1) {
				if (now - down_ticks[i] >= REPEAT_HOLDOFF) {
					uint16_t n = (now - down_ticks[i] - REPEAT_HOLDOFF) / REPEAT_RATE;
					if (repeat_count[i] != n) {
						repeat_count[i] = n;
						event.keypad.pressed |= (1 << i);
					}
				}
			}
		}

		if (event.keypad.pressed || event.keypad.released) {
			push_event(&event);
		}
	}
}

void event_init(void)
{
	event_queue = xQueueCreate(10, sizeof(event_t));
	xTaskCreate(keypad_task, "keypad", 4096, NULL, 5, NULL);
}

int wait_event(event_t *event)
{
	int got_event = xQueueReceive(event_queue, event, portMAX_DELAY);
	if (got_event != pdTRUE) {
		return -1;
	}
	return 1;
}

int push_event(event_t *event) { return xQueueSend(event_queue, event, 10 / portTICK_PERIOD_MS); }
