#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

const char *ssid = "";
const char *password = "";

// Піни для датчика HC-SR04
const int trigPin = 14; // D5
const int echoPin = 12; // D6

float distance = 0.0;
unsigned long last_sensor_time = 0;

// --- СТРУКТУРА КОНСТРУКТОРА ПРАВИЛ (Rule Engine) ---
#define MAX_RULES 5

struct Rule
{
  String sensor; // Наприклад, "distance"
  String op;     // Оператор: "<", ">", "=="
  float val;     // Порогове значення
  String action; // Дія
  bool active;   // Чи увімкнене правило
};

Rule rules[MAX_RULES];
int ruleCount = 0;

String lastTriggeredAction = "Немає спрацювань правил";

enum States
{
  CONNECTING,
  SUCCESS,
  FAILED
};

States cur_state = CONNECTING;

unsigned long last_time_connected = 0;
unsigned long start_attempt_time = 0;
unsigned long last_time_failed = 0;

ESP8266WebServer server(80);

// Неблокуюче вимірювання відстані
void measureDistance()
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0)
  {
    distance = -1.0;
  }
  else
  {
    distance = duration * 0.034 / 2.0;
  }
}

// --- ОЦІНКА ПРАВИЛ (Evaluation Engine) ---
void evaluateRules()
{
  for (int i = 0; i < ruleCount; i++)
  {
    if (!rules[i].active)
      continue;

    bool conditionMet = false;

    if (rules[i].sensor == "distance" && distance >= 0)
    {
      if (rules[i].op == "<")
      {
        conditionMet = (distance < rules[i].val);
      }
      else if (rules[i].op == ">")
      {
        conditionMet = (distance > rules[i].val);
      }
      else if (rules[i].op == "==")
      {
        conditionMet = (abs(distance - rules[i].val) < 1.0);
      }
    }

    if (conditionMet)
    {
      lastTriggeredAction = "Правило #" + String(i + 1) + " спрацювало! (Відстань " + rules[i].op + " " + String(rules[i].val) + " см)";
      Serial.println(lastTriggeredAction);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\nBooting Automation Gateway...");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  if (!LittleFS.begin())
  {
    Serial.println("Помилка монтування LittleFS!");
    return;
  }
  Serial.println("LittleFS замонтовано успішно.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  start_attempt_time = millis();
}

void loop()
{
  unsigned long cur_time = millis();

  switch (cur_state)
  {
  case CONNECTING:
    if (WiFi.status() == WL_CONNECTED)
    {
      cur_state = SUCCESS;
      Serial.println("\nWiFi connected success!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      // 1. Головна сторінка SPA
      server.on("/", HTTP_GET, []()
                {
        if (LittleFS.exists("/index.html")) {
          File file = LittleFS.open("/index.html", "r");
          server.streamFile(file, "text/html");
          file.close();
        } else {
          server.send(404, "text/plain", "Error: index.html not found!");
        } });

      // 2. GET API: Телеметрія
      server.on("/api/data", HTTP_GET, []()
                {
        JsonDocument doc;
        doc["uptime"] = millis() / 1000;
        doc["distance"] = distance;
        doc["last_action"] = lastTriggeredAction;
        doc["status"] = "OK";
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json); });

      // 3. GET API: Отримання списку правил
      server.on("/api/rules", HTTP_GET, []()
                {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < ruleCount; i++) {
          JsonObject r = arr.add<JsonObject>();
          r["id"] = i;
          r["sensor"] = rules[i].sensor;
          r["op"] = rules[i].op;
          r["val"] = rules[i].val;
          r["action"] = rules[i].action;
          r["active"] = rules[i].active;
        }

        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json); });

      // 4. POST API: Збереження правил
      server.on("/api/rules", HTTP_POST, []()
                {
        if (!server.hasArg("plain")) {
          server.send(400, "application/json", "{\"status\":\"error\"}");
          return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
          server.send(400, "application/json", "{\"status\":\"invalid json\"}");
          return;
        }

        ruleCount = 0;
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject r : arr) {
          if (ruleCount >= MAX_RULES) break;
          rules[ruleCount].sensor = r["sensor"].as<String>();
          rules[ruleCount].op = r["op"].as<String>();
          rules[ruleCount].val = r["val"] | 0.0;
          rules[ruleCount].action = r["action"].as<String>();
          rules[ruleCount].active = r["active"] | true;
          ruleCount++;
        }

        Serial.printf("Оновлено правил через конструктор: %d\n", ruleCount);
        server.send(200, "application/json", "{\"status\":\"success\"}"); });

      server.serveStatic("/style.css", LittleFS, "/style.css");
      server.serveStatic("/app.js", LittleFS, "/app.js");

      server.onNotFound([]()
                        {
        File file = LittleFS.open("/index.html", "r");
        if (file) {
          server.streamFile(file, "text/html");
          file.close();
        } else {
          server.send(404, "text/plain", "404: Not Found");
        } });

      server.begin();
      Serial.println("HTTP server started.");
    }
    else
    {
      if (cur_time - last_time_connected >= 300)
      {
        last_time_connected = cur_time;
        Serial.print(".");
      }
      if (cur_time - start_attempt_time >= 30000)
      {
        cur_state = FAILED;
        last_time_failed = cur_time;
      }
    }
    break;

  case SUCCESS:
    server.handleClient();

    if (cur_time - last_sensor_time >= 1000)
    {
      last_sensor_time = cur_time;
      measureDistance();
      evaluateRules();
    }
    break;

  case FAILED:
    if (cur_time - last_time_failed >= 5000)
    {
      ESP.restart();
    }
    break;
  default:
    break;
  }
}