// Executes ESPHome's installed URL matching and text-sensor GET/update methods.
// The network and JSON writer are narrow seams; no firmware or TCP emulation.
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#define USE_DEVICES
#define ESP_LOGW(...)
constexpr int OBJECT_ID_MAX_LEN = 64;
constexpr int HTTP_GET = 0;
using StringRef = std::string_view;
struct Device {
  std::string name;
  const char *get_name() const { return name.c_str(); }
};
struct EntityBase {
  std::string name;
  Device *device = nullptr;
  bool internal = true;
  const char *get_name() const { return name.c_str(); }
  Device *get_device() const { return device; }
  bool is_internal() const { return internal; }
  StringRef get_object_id_to(char *) const { return "deprecated_unused"; }
};
struct EntityMatchResult { bool matched; bool action_is_empty; };
struct UrlMatch {
  bool valid = false;
  StringRef domain, id, method, device_name;
  EntityMatchResult match_entity(EntityBase *entity) const;
};
namespace text_sensor {
struct TextSensor : EntityBase { std::string state; };
}
struct Application {
  std::vector<text_sensor::TextSensor *> sensors;
  auto &get_text_sensors() { return sensors; }
} App;
struct AsyncWebServerRequest {
  int status = 0;
  std::string body;
  int method() const { return HTTP_GET; }
  void send(int code, const char * = nullptr, const char *data = "") { status = code; body = data; }
};
int get_request_detail(AsyncWebServerRequest *) { return 0; }
struct WebServer {
  bool include_internal_ = false;
  struct Events {
    int count = 0;
    template<class... Args> void deferrable_send_state(Args...) { ++count; }
  } events_;
  static void text_sensor_state_json_generator() {}
  std::string text_sensor_json_(text_sensor::TextSensor *, const std::string &state, int) { return state; }
  void on_text_sensor_update(text_sensor::TextSensor *obj);
  void handle_text_sensor_request(AsyncWebServerRequest *request, const UrlMatch &match);
};
// INSERT_ESPHOME_METHODS
int main(int argc, char **argv) {
  assert(argc == 3);
  Device device{"Synthetic Meter"};
  text_sensor::TextSensor main, sub;
  main.name = sub.name = "Full FDR Archive";
  main.state = "archive-main";
  sub.state = "archive-sub";
  sub.device = &device;
  App.sensors = {&main, &sub};
  WebServer server;
  for (int i = 1; i <= 2; ++i) {
    const auto match = match_url(argv[i], std::strlen(argv[i]), false);
    assert(match.valid);
    AsyncWebServerRequest request;
    server.handle_text_sensor_request(&request, match);
    assert(request.status == 200);
    assert(request.body == (i == 1 ? main.state : sub.state));
  }
  server.on_text_sensor_update(&main);
  assert(server.events_.count == 0); // Internal state is not broadcast to SSE.
  server.include_internal_ = true;
  server.on_text_sensor_update(&main);
  assert(server.events_.count == 1);
  const char *missing = "/text_sensor/Other Meter/Full FDR Archive";
  AsyncWebServerRequest request;
  server.handle_text_sensor_request(&request, match_url(missing, std::strlen(missing), false));
  assert(request.status == 404);
}
