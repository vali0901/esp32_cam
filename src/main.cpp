#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <unordered_map>
#include <string>
#include "esp_camera.h"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Define the GPIO pin for the button
#define BUTTON_PIN 0

#define TOKEN_LENGTH 12

// WiFi AP credentials
const char *ap_ssid = "ESP32_AP";
const char *ap_password = "12345678";
const char* hostname = "esp32videocam";

// Flag for button press
volatile bool buttonPressed = false;

// Flag for flashlight
volatile bool isFlashlightOn = false;

// Flag for streaming
volatile bool isStreaming = false;

volatile bool CONFIG_MODE = false;
volatile bool DATA_MODE = false;

std::unordered_map<std::string, std::string> access_tokens;

// HTTPS server
AsyncWebServer config_server(8080);
AsyncWebServer data_server(80);


void read_wifi_credentials(std::string &ssid, std::string &password);
void save_wifi_credentials(std::string ssid, std::string password);
void generate_admin_token(std::string &new_token);
void start_config_mode();
void stop_config_mode();
void start_data_mode();
void stop_data_mode();
void read_access_tokens();
void save_access_tokens();
void serveWebFilesConfig();
void serveWebFilesData();
void init_camera();


// Interrupt service routine (ISR) for button press
void IRAM_ATTR handleButtonPress() {
  // digitalWrite(4, !digitalRead(4));
  buttonPressed = true;
}

void setup() {
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  // Initialize Serial communication
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Wait for Serial connection to stabilize
  }

  Serial.println("Initializing...");

  // Initialize SPIFFS
  if (!SPIFFS.begin(false, "/data", 20)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }

  Serial.println("SPIFFS mounted successfully");

  // Configure the button pin as input with an internal pull-up resistor
  pinMode(BUTTON_PIN, INPUT_PULLUP);


  // Read access tokens from SPIFFS
  read_access_tokens();

  // Initialize the camera
  init_camera();

  // start_config_mode();
  digitalWrite(4, HIGH);
  start_data_mode();
  digitalWrite(4, LOW);

  // Attach interrupt to the button pin (trigger on falling edge)
}

void loop() {
  if(buttonPressed) {
    buttonPressed = false;
    stop_data_mode();
    start_config_mode();
  }
}


// Function to serve web files
void serveWebFilesConfig() {
    config_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config/index.html", "text/html");
    });
    config_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config/style.css", "text/css");
    });
    config_server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config/script.js", "application/javascript");
    });
    config_server.on("/submit", HTTP_POST, [](AsyncWebServerRequest* request) {
        std::string ssid = request->getParam("ssid", true)->value().c_str();
        std::string password = request->getParam("password", true)->value().c_str();
        std::string admin_token = request->getParam("token", true)->value().c_str();

        if(access_tokens.find(admin_token) == access_tokens.end()) {
            request->send(403, "text/plain", "Invalid admin token");
            return;
        }

        save_wifi_credentials(ssid, password);

        Serial.printf("Received WiFi credentials: SSID=%s, Password=%s\n", ssid.c_str(), password.c_str());
        request->send(200, "text/plain", "WiFi credentials received!");
    });
    config_server.on("/quit", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Exiting config mode, you will be disconnected...");
        stop_config_mode();
        digitalWrite(4, HIGH);
        start_data_mode();
    });
    config_server.on("/token_mgmt/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config/token_mgmt/index.html", "text/html");
    });
    config_server.on("/token_mgmt/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config/token_mgmt/style.css", "text/css");
    });
    config_server.on("/token_mgmt/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/config/token_mgmt/script.js", "application/javascript");
    });
    config_server.on("/token_mgmt/submit", HTTP_POST, [](AsyncWebServerRequest* request) {
        std::string admin_token = request->getParam("token", true)->value().c_str();

        if(access_tokens.find(admin_token) == access_tokens.end()) {
            request->send(403, "text/plain", "Invalid admin token");
            return;
        }

        std::string action = request->getParam("action", true)->value().c_str();

        if(action == "add") {
          std::string new_token;
          generate_admin_token(new_token);

          access_tokens[new_token] = "";
          save_access_tokens();
          request->send(200, "text/plain", new_token.c_str());
        } else if(action == "remove") {
          if(access_tokens.size() == 1) {
            request->send(403, "text/plain", "Cannot remove token: no more tokens left!");
          }
          access_tokens.erase(admin_token);
          save_access_tokens();
          request->send(200, "text/plain", "Token removed");
        } else {
          request->send(400, "text/plain", "Invalid action");
        }
    });
    config_server.onNotFound([](AsyncWebServerRequest* request) {
      Serial.printf("Unhandled request: %s\n", request->url().c_str());
      request->send(404, "text/plain", "Not found");
    });
}

void serveWebFilesData() {
  data_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/video/index.html", "text/html");
  });
  data_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/video/style.css", "text/css");
  });
  data_server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/video/script.js", "application/javascript");
  });

  data_server.on("/stream/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/video/stream/index.html", "text/html");
  });

  data_server.on("/submit", HTTP_POST, [](AsyncWebServerRequest* request) {
        std::string admin_token = request->getParam("token", true)->value().c_str();

        if(access_tokens.find(admin_token) == access_tokens.end()) {
            request->send(403, "text/plain", "Invalid admin token");
            return;
        }

        request->send(200, "text/plain", "Access granted");

  });

  data_server.on("/stream/flashlight", HTTP_POST, [](AsyncWebServerRequest* request) {
    isFlashlightOn = !isFlashlightOn;
    digitalWrite(4, isFlashlightOn ? HIGH : LOW); // GPIO 4 controls the flashlight
    request->send(200, "application/json", String("{\"flashlight\":") + (isFlashlightOn ? "true" : "false") + "}");
  });

  // Handle stream toggle
  data_server.on("/stream/toggle_stream", HTTP_POST, [](AsyncWebServerRequest* request) {
    isStreaming = !isStreaming;
    request->send(200, "application/json", String("{\"streaming\":") + (isStreaming ? "true" : "false") + "}");
  });

 data_server.on("/stream/video_feed", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!isStreaming) {
        request->send(503, "text/plain", "Streaming is disabled");
        return;
    }

    AsyncResponseStream* response = request->beginResponseStream("multipart/x-mixed-replace; boundary=frame");
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Connection", "close");

    response->setCode(200); // Explicitly set the HTTP response code
    request->send(response); // Send headers and open the stream

    while (isStreaming) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera frame buffer failed");
            break;
        }

        // Send the frame data
        response->printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        response->write((const char*)fb->buf, fb->len);
        response->print("\r\n");

        esp_camera_fb_return(fb);

        // Limit the frame rate to ~10 FPS
        delay(100);
        yield(); // Prevent watchdog timer resets
    }
});


  data_server.onNotFound([](AsyncWebServerRequest* request) {
    Serial.printf("Unhandled request: %s\n", request->url().c_str());
    request->send(404, "text/plain", "Not found");
  });
}

void read_access_tokens() {
  // read access tokens from SPIFFS
  File file = SPIFFS.open("/passwd.json", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  // Parse the JSON file
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to read file, using default credentials");
    return;
  }

  // extract the tokens array from the JSON object
  JsonArray tokens = doc["tokens"].as<JsonArray>();
  for (JsonVariant token : tokens) {
    access_tokens[token.as<std::string>()] = "";
  }

  // Close the file
  file.close();
}

void save_access_tokens() {
  // Save access tokens to SPIFFS
  File file = SPIFFS.open("/passwd.json", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  // Create a JSON object
  JsonDocument doc;
  JsonArray tokens = doc["tokens"].to<JsonArray>();
  for (auto const& token : access_tokens) {
    tokens.add(token.first);
  }

  // Serialize the JSON object
  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write file");
  }

  // Close the file
  file.close();
}

void read_wifi_credentials(std::string &ssid, std::string &password) {
  // read wifi credentials from SPIFFS
  File file = SPIFFS.open("/wifi.json", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  // Parse the JSON file
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to read file, using default credentials");
    return;
  }

  // Extract the SSID and password
  ssid = doc["ssid"].as<std::string>();
  password = doc["password"].as<std::string>();

  // Close the file
  file.close();
}

void save_wifi_credentials(std::string ssid, std::string password) {
  // Save wifi credentials to SPIFFS
  File file = SPIFFS.open("/wifi.json", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  // Create a JSON object
  JsonDocument doc;
  doc["ssid"] = ssid;
  doc["password"] = password;

  // Serialize the JSON object
  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write file");
  }

  // Close the file
  file.close();
}

void generate_admin_token(std::string &new_token) {
  // Generate a random token
  char token[TOKEN_LENGTH + 1];
  for (int i = 0; i < TOKEN_LENGTH; i++) {
    token[i] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"[random(62)];
  }
  token[TOKEN_LENGTH] = '\0';
  new_token = token;
}

void start_config_mode() {
  // Start WiFi in AP mode
  // disable isr
  detachInterrupt(BUTTON_PIN);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("WiFi AP started");

  // Initialize HTTP server
  serveWebFilesConfig();
  config_server.begin();

  CONFIG_MODE = true;
}

void stop_config_mode() {
  // Stop the config server
  if(CONFIG_MODE) {
    CONFIG_MODE = true;
    config_server.end();
  }
  Serial.println("Config server stopped");

  // Stop WiFi AP
  WiFi.softAPdisconnect();
  Serial.println("WiFi AP stopped");

  // Re-enable the button ISR
  attachInterrupt(BUTTON_PIN, handleButtonPress, RISING);
}

void
start_data_mode() {
  // Stop the config server

  Serial.println("Config server stopped");

  // Start the data server
  std::string ssid, password;
  read_wifi_credentials(ssid, password);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");

  // Initialize HTTP server
  serveWebFilesData();
  data_server.begin();
  Serial.println("Data server started");


  // attachInterrupt(BUTTON_PIN, handleButtonPress, RISING);
  digitalWrite(4, LOW);


  DATA_MODE = true;
}

void stop_data_mode() {
  // Stop the data server
  if(DATA_MODE) {
    data_server.end();
    DATA_MODE = false;
  }

  Serial.println("Data server stopped");

  isFlashlightOn = false;
  digitalWrite(4, LOW);
  isStreaming = false;

  // disconnect from WiFi
  WiFi.disconnect();
  Serial.println("WiFi disconnected");
}

void init_camera() {
camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    // if(psramFound()) {
    //   config.frame_size = FRAMESIZE_UXGA;
    //   config.jpeg_quality = 10;
    //   config.fb_count = 2;
    // } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.jpeg_quality = 12;
      config.fb_count = 1;
    // }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        while (true);
    }
    Serial.println("Camera initialized successfully");
}

