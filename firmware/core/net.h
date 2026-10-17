#pragma once
// The MQTT broker connection, as the core and the pages see it. The desktop preview has no
// MQTT client: there it is never connected and a publish goes to the log instead.
#include "esphome.h"
#include <string>

namespace cyd_net {

#ifdef USE_MQTT
inline bool connected() {
  return esphome::mqtt::global_mqtt_client != nullptr && esphome::mqtt::global_mqtt_client->is_connected();
}
inline void publish_json(const std::string &topic, const esphome::json::json_build_t &f, uint8_t qos = 0,
                         bool retain = false) {
  if (connected()) esphome::mqtt::global_mqtt_client->publish_json(topic, f, qos, retain);
}
#else
inline bool connected() { return false; }
inline void publish_json(const std::string &topic, const esphome::json::json_build_t &f, uint8_t qos = 0,
                         bool retain = false) {
  ESP_LOGD("cyd", "PUBLISH (no broker) %s %s", topic.c_str(), esphome::json::build_json(f).c_str());
}
#endif

}  // namespace cyd_net
