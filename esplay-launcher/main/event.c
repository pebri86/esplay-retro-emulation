#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <event.h>
#include <gamepad.h>
#include <string.h>

static QueueHandle_t event_queue;
static bool event_running = false;

static void keypad_task(void *arg)
{
	event_running = true;
	event_t event;
	input_gamepad_state prevKey;
	gamepad_read(&prevKey);
	while (event_running) {
		input_gamepad_state key;
		gamepad_read(&key);
		vTaskDelay(10 / portTICK_PERIOD_MS);
		event.type = EVENT_TYPE_KEYPAD;
		event.keypad.state = key;
		event.keypad.last_state = prevKey;

		if (memcmp(prevKey.values, key.values, sizeof(prevKey)) != 0) {
			push_event(&event);
		}
		prevKey = key;
	}

  vTaskDelete(NULL);
	vQueueDelete(event_queue);
}

void event_init(void)
{
	event_queue = xQueueCreate(10, sizeof(event_t));
	xTaskCreate(keypad_task, "keypad", 4096, NULL, 5, NULL);
}

void event_deinit(void)
{
	event_running = false;
	printf("Shutdown event..\n");
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
