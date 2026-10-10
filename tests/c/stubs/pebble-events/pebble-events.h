#pragma once
#include <pebble.h>

typedef void *EventHandle;

typedef struct EventAppMessageHandlers {
  AppMessageOutboxSent sent;
  AppMessageOutboxFailed failed;
  AppMessageInboxReceived received;
  AppMessageInboxDropped dropped;
} EventAppMessageHandlers;

void events_app_message_request_inbox_size(uint32_t size);
void events_app_message_request_outbox_size(uint32_t size);
EventHandle events_app_message_subscribe_handlers(EventAppMessageHandlers handlers, void *context);
void events_app_message_unsubscribe(EventHandle handle);
