/**
 * WiFiManager advanced demo with 3 custom string parameters (ESP32 + Preferences)
 * - Bafa UID
 * - Bafa Topic  
 * - BLE MAC Address  
 * - BLE Advertising Data
 * Optimized version with enhanced error handling, validation, and safety features
 */

#include <WiFi.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <Preferences.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <esp_mac.h>

// ********************* 需要修改的配置部分 **********************
//const char* ssid = "minke";        // 替换为你的Wi-Fi名称
//const char* pswd = "200182skl";        // 替换为你的Wi-Fi密码
//const char* uid  = "1f39e5ddf7234b46850eed5b92e9fd33";      // 替换为你的巴法云私钥
//const char* topic = "HD2enJ0ZM002";         // 替换为你在巴法云创建的主题名，例如 "light002"
// ************************************************************

// 引脚定义
#define TRIGGER_PIN 9
#define LED_PIN 12
#define BAFA_LED_PIN 13

// 配置常量
#define WDT_TIMEOUT_SECONDS 180
#define BUTTON_DEBOUNCE_MS 50
#define LONG_PRESS_MS 3000
#define CONFIG_PORTAL_TIMEOUT 120
#define BLE_ADVERTISING_DURATION 1000  // BLE广告持续时间1秒

// 定义设备名称
#define DEVICE_NAME "ESP32C3_BLE_Beacon"

// 全局可写缓冲区（用于存储默认值和已保存值）
char bafa_uid_buf[65] = "";
char bafa_topic_buf[33] = "";
char ble_mac_buf[19] = "";
char ble_data_buf[65] = "";

const char* DEFAULT_BAFA_UID = "98873b5ca43046cea88fa3b9ed51ef9b";

const char* DEFAULT_BAFA_TOPIC = "switch001";

const char* DEFAULT_BLE_MAC = "78:81:8c:05:0f:fa";

const char* DEFAULT_BLE_DATA = "0201061BFF53050100037E0566200001810917158C81780F00000000000000";

// BLE相关变量
BLEAdvertising *pAdvertising;
bool bleInitialized = false;
bool ledState = false;
unsigned long bleAdvertisingStart = 0;

// 自定义MAC地址 (最后三个字节可以更改)
uint8_t newMAC[6] = {0x78, 0x81, 0x8c, 0x06, 0x9a, 0xc4};


static uint8_t wake_adv_data[] = {
    // Flags (0x02, 0x01, 0x06)
    0x02, 0x01, 0x06,
    // Manufacturer Specific Data: length=27 (0x1B), type=0xFF
    0x1B, 0xFF,
    // Payload (26 bytes)
    0x53, 0x05, 0x01, 0x00, 0x03, 0x7e, 0x05, 0x66, 0x20, 0x00, 0x01, 0x81,
    // Host address (6 bytes, will be filled at runtime in reverse order)
    0x09, 0x17, 0x15, 0x8C, 0x81, 0x80,
    // Remaining bytes
    0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// 系统状态
enum SystemStatus {
  STATUS_BOOT,
  STATUS_CONFIG_MODE,
  STATUS_CONNECTING,
  STATUS_CONNECTED,
  STATUS_ERROR
};

SystemStatus current_status = STATUS_BOOT;
bool wm_nonblocking = false;
unsigned long last_led_toggle = 0;
bool led_state = false;

// 对象实例
WiFiManager wm;
Preferences prefs;

// 全局参数对象（必须全局）
WiFiManagerParameter param_bafa_uid;
WiFiManagerParameter param_bafa_topic;
WiFiManagerParameter param_ble_mac;
WiFiManagerParameter param_ble_data;

// 服务器地址和端口
const char* host = "bemfa.com";
const int port = 8344;

// 函数声明
void saveParamCallback();
String getParam(String name);
void loadSavedParams();
void checkButton();
void updateStatusLED();
void safeRestart(const char* reason);
bool validateBafaUID(const String& uid);
bool validateBafaTopic(const String& topic);
bool validateMACAddress(const String& mac);
bool validateHexData(const String& hex);
void printSystemInfo();
bool initializePreferences();
void setup_wifi();
void connect_server();
void send_heartbeat();
void initBLE();
void startBLEAdvertising();
void stopBLEAdvertising();
void handleBLEAdvertising();
std::string hexToBytes(const String& hex);

// 创建WiFi客户端对象
WiFiClient client;

void setup() {
  // 基本初始化
  esp_base_mac_addr_set(newMAC);
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n" + String("=").substring(0, 50));
  Serial.println("ESP32 WiFiManager with Enhanced Features");
  Serial.println("Version: 2.0 - Optimized");
  Serial.println(String("=").substring(0, 50));
  
  // GPIO 初始化
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BAFA_LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BAFA_LED_PIN, LOW);
  
  // 看门狗初始化
  esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
  esp_task_wdt_add(NULL);
  Serial.println("✅ Watchdog initialized");
  
  // 系统信息
  printSystemInfo();
  
  // 初始化 Preferences
  if (!initializePreferences()) {
    Serial.println("❌ Preferences initialization failed, using defaults");
  }
  
  // 从 Preferences 加载已保存的参数
  loadSavedParams();
  
  // WiFiManager 配置
  if (wm_nonblocking) {
    wm.setConfigPortalBlocking(false);
  }
  
  // 创建参数对象（使用已加载的 buffer 作为默认值）
  new (&param_bafa_uid) WiFiManagerParameter("bafa_uid", "Bafa User ID (64 chars max)", bafa_uid_buf, 64);
  new (&param_bafa_topic) WiFiManagerParameter("bafa_topic", "Bafa Topic (32 chars max)", bafa_topic_buf, 32);
  new (&param_ble_mac) WiFiManagerParameter("ble_mac", "BLE Device MAC (AA:BB:CC:DD:EE:FF format)", ble_mac_buf, 18);
  new (&param_ble_data) WiFiManagerParameter("ble_data", "BLE Adv Data (Hex format, even length)", ble_data_buf, 64);
  
  // 添加参数到 WiFiManager
  wm.addParameter(&param_bafa_uid);
  wm.addParameter(&param_bafa_topic);
  wm.addParameter(&param_ble_mac);
  wm.addParameter(&param_ble_data);
  
  // 设置回调
  wm.setSaveParamsCallback(saveParamCallback);
  
  // 自定义菜单和主题
  std::vector<const char*> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);
  wm.setClass("invert"); // 暗色主题
  wm.setConfigPortalTimeout(30); // 初始连接30秒超时
  
  // 设置自定义信息
  wm.setCustomHeadElement("<style>html{background:#1e1e1e;}</style>");
  
  current_status = STATUS_CONNECTING;
  Serial.println("🔄 Attempting WiFi connection...");
  
  // 尝试自动连接
  bool res = wm.autoConnect("ESP32-ConfigAP", "12345678");
  
  if (!res) {
    Serial.println("❌ Failed to connect or hit timeout");
    current_status = STATUS_ERROR;
  } else {
    Serial.println("✅ WiFi Connected!");
    Serial.print("📶 IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("📡 RSSI: ");
    Serial.println(WiFi.RSSI());
    current_status = STATUS_CONNECTED;
    
    // 连接巴法云服务器
    connect_server();
  }
  
  Serial.println("🚀 Setup completed, entering main loop");
}

void loop() {
  // 喂看门狗
  esp_task_wdt_reset();
  
  // WiFiManager 处理（非阻塞模式）
  if (wm_nonblocking) {
    wm.process();
  }
  
  // 按键检测
  checkButton();
  
  // LED状态指示
  updateStatusLED();
  
  // 连接状态监控
  if (current_status == STATUS_CONNECTED && WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi connection lost, attempting reconnection...");
    current_status = STATUS_CONNECTING;
  } else if (current_status == STATUS_CONNECTING && WiFi.status() == WL_CONNECTED) {
    current_status = STATUS_CONNECTED;
    Serial.println("✅ WiFi reconnected");
  }
  
  // 处理从服务器收到的消息
  if (client.available()) {
    String message = client.readStringUntil('\n');
    Serial.print("Received: ");
    Serial.println(message);

    // 解析消息：判断是否包含 "on" 或 "off"
    if (message.indexOf("on") != -1) {
      digitalWrite(BAFA_LED_PIN, HIGH); // 开灯
      ledState = true;
      Serial.println("LED turned ON");
      
      // 启动BLE广播1秒钟
      if (!bleInitialized) {
        initBLE();
      }
      startBLEAdvertising();
    } else if (message.indexOf("off") != -1) {
      digitalWrite(BAFA_LED_PIN, LOW); // 关灯
      ledState = false;
      Serial.println("LED turned OFF");
      
      // 停止BLE广播
      stopBLEAdvertising();
    }
  }

  // 处理BLE广告持续时间
  handleBLEAdvertising();

  // 每50秒发送一次心跳包（巴法云要求60秒内有通信）
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 50000) {
    send_heartbeat();
    lastHeartbeat = millis();
  }
  
  delay(100);
}

// 系统信息打印
void printSystemInfo() {
  Serial.println("📋 System Information:");
  Serial.println("   Chip Model: " + String(ESP.getChipModel()));
  Serial.println("   Chip Revision: " + String(ESP.getChipRevision()));
  Serial.println("   Flash Size: " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB");
  Serial.println("   Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("   SDK Version: " + String(ESP.getSdkVersion()));
}

// 安全重启
void safeRestart(const char* reason) {
  Serial.println("🔄 System restart requested: " + String(reason));
  Serial.println("   Saving current state...");
  
  // 确保数据已保存
  prefs.end();
  
  // 断开WiFi连接
  WiFi.disconnect(true);
  delay(1000);
  
  Serial.println("   Restarting in 3 seconds...");
  delay(3000);
  ESP.restart();
}

// 初始化 Preferences
bool initializePreferences() {
  if (!prefs.begin("config", true)) {
    return false;
  }
  
  // 测试读取以验证功能
  size_t freeEntries = prefs.freeEntries();
  prefs.end();
  
  Serial.println("✅ Preferences initialized (Free entries: " + String(freeEntries) + ")");
  return true;
}

// 参数验证函数
bool validateBafaUID(const String& uid) {
  if (uid.length() == 0 || uid.length() > 64) {
    Serial.println("❌ Bafa UID validation failed: invalid length");
    return false;
  }
  
  return true;
}

bool validateBafaTopic(const String& topic) {
  if (topic.length() == 0 || topic.length() > 32) {
    Serial.println("❌ Bafa topic validation failed: invalid length");
    return false;
  }
  
  return true;
}

bool validateMACAddress(const String& mac) {
  if (mac.length() != 17) {
    Serial.println("❌ MAC address validation failed: incorrect length");
    return false;
  }
  
  for (int i = 0; i < 17; i++) {
    if (i % 3 == 2) {
      if (mac[i] != ':') {
        Serial.println("❌ MAC address validation failed: missing ':' at position " + String(i));
        return false;
      }
    } else {
      if (!isxdigit(mac[i])) {
        Serial.println("❌ MAC address validation failed: invalid hex character at position " + String(i));
        return false;
      }
    }
  }
  
  return true;
}

bool validateHexData(const String& hex) {
  if (hex.length() == 0) {
    Serial.println("⚠️  Hex data is empty");
    return true; // 允许空数据
  }
  
  if (hex.length() % 2 != 0) {
    Serial.println("❌ Hex data validation failed: odd length");
    return false;
  }
  
  if (hex.length() > 64) {
    Serial.println("❌ Hex data validation failed: too long");
    return false;
  }
  
  for (unsigned int i = 0; i < hex.length(); i++) {
    if (!isxdigit(hex[i])) {
      Serial.println("❌ Hex data validation failed: invalid character at position " + String(i));
      return false;
    }
  }
  
  return true;
}

// 从 Web 服务器获取参数值
String getParam(String name) {
  if (wm.server && wm.server->hasArg(name)) {
    return wm.server->arg(name);
  }
  return String();
}

// 保存回调：用户点击"保存"时触发
void saveParamCallback() {
  Serial.println("\n📝 [CALLBACK] Parameter save triggered");
  
  String uid = getParam("bafa_uid");
  String topic = getParam("bafa_topic");
  String mac = getParam("ble_mac");  
  String data = getParam("ble_data");
  
  Serial.println("🔍 Validating parameters...");
  
  // 验证并修正参数
  if (!validateBafaUID(uid)) {
    Serial.println("   Using default Bafa UID");
    uid = DEFAULT_BAFA_UID;
  }
  
  if (!validateBafaTopic(topic)) {
    Serial.println("   Using default Bafa topic");
    topic = DEFAULT_BAFA_TOPIC;
  }
  
  if (!validateMACAddress(mac)) {
    Serial.println("   Using default MAC address");
    mac = DEFAULT_BLE_MAC;
  }
  
  if (!validateHexData(data)) {
    Serial.println("   Using default advertising data");
    data = DEFAULT_BLE_DATA;
  }
  
  Serial.println("✅ All parameters validated");
  Serial.println("   Bafa UID: " + uid);
  Serial.println("   Bafa Topic: " + topic);
  Serial.println("   BLE MAC: " + mac);
  Serial.println("   BLE Data: " + data);
  
  // 保存到 NVS
  if (!prefs.begin("config", false)) {
    Serial.println("❌ Failed to open preferences for writing");
    return;
  }
  
  bool success = true;
  success &= prefs.putString("bafa_uid", uid);
  success &= prefs.putString("bafa_topic", topic);
  success &= prefs.putString("ble_mac", mac);  
  success &= prefs.putString("ble_data", data);
  
  prefs.end();
  
  if (success) {
    // 更新全局 buffer
    strncpy(bafa_uid_buf, uid.c_str(), sizeof(bafa_uid_buf) - 1);
    bafa_uid_buf[sizeof(bafa_uid_buf) - 1] = '\0';
    
    strncpy(bafa_topic_buf, topic.c_str(), sizeof(bafa_topic_buf) - 1);
    bafa_topic_buf[sizeof(bafa_topic_buf) - 1] = '\0';
    
    strncpy(ble_mac_buf, mac.c_str(), sizeof(ble_mac_buf) - 1);
    ble_mac_buf[sizeof(ble_mac_buf) - 1] = '\0';
    
    strncpy(ble_data_buf, data.c_str(), sizeof(ble_data_buf) - 1);
    ble_data_buf[sizeof(ble_data_buf) - 1] = '\0';
    
    Serial.println("✅ Parameters saved successfully to flash memory");
  } else {
    Serial.println("❌ Failed to save parameters to flash memory");
  }
}

// 启动时加载已保存的参数
void loadSavedParams() {
  Serial.println("📖 Loading saved parameters...");
  
  if (!prefs.begin("config", true)) {
    Serial.println("❌ Failed to open preferences, using defaults");
    strcpy(bafa_uid_buf, DEFAULT_BAFA_UID);
    strcpy(bafa_topic_buf, DEFAULT_BAFA_TOPIC);
    strcpy(ble_mac_buf, DEFAULT_BLE_MAC);
    strcpy(ble_data_buf, DEFAULT_BLE_DATA);
    return;
  }
  
  String uid = prefs.getString("bafa_uid", DEFAULT_BAFA_UID);
  String topic = prefs.getString("bafa_topic", DEFAULT_BAFA_TOPIC);
  String mac = prefs.getString("ble_mac", DEFAULT_BLE_MAC);
  String data = prefs.getString("ble_data", DEFAULT_BLE_DATA);
  
  prefs.end();
  
  // 安全地复制到缓冲区
  strncpy(bafa_uid_buf, uid.c_str(), sizeof(bafa_uid_buf) - 1);
  bafa_uid_buf[sizeof(bafa_uid_buf) - 1] = '\0';
  
  strncpy(bafa_topic_buf, topic.c_str(), sizeof(bafa_topic_buf) - 1);  
  bafa_topic_buf[sizeof(bafa_topic_buf) - 1] = '\0';
  
  strncpy(ble_mac_buf, mac.c_str(), sizeof(ble_mac_buf) - 1);  
  ble_mac_buf[sizeof(ble_mac_buf) - 1] = '\0';
  
  strncpy(ble_data_buf, data.c_str(), sizeof(ble_data_buf) - 1);
  ble_data_buf[sizeof(ble_data_buf) - 1] = '\0';
  
  Serial.println("✅ Parameters loaded successfully:");
  Serial.println("   Bafa UID: " + String(bafa_uid_buf));
  Serial.println("   Bafa Topic: " + String(bafa_topic_buf));
  Serial.println("   BLE MAC: " + String(ble_mac_buf));
  Serial.println("   BLE Data: " + String(ble_data_buf));
}

// LED状态指示
void updateStatusLED() {
  unsigned long current_time = millis();
  unsigned long interval;
  
  // 根据状态设置闪烁间隔
  switch (current_status) {
    case STATUS_CONFIG_MODE:
      interval = 200; // 快闪 - 配置模式
      break;
    case STATUS_CONNECTING:
      interval = 500; // 中速闪 - 连接中
      break;
    case STATUS_CONNECTED:
      interval = 2000; // 慢闪 - 已连接
      break;
    case STATUS_ERROR:
      digitalWrite(LED_PIN, HIGH); // 常亮 - 错误状态
      return;
    default:
      interval = 1000; // 默认
      break;
  }
  
  if (current_time - last_led_toggle >= interval) {
    led_state = !led_state;
    digitalWrite(LED_PIN, led_state);
    last_led_toggle = current_time;
  }
}

// 按键检测（低电平触发，使用 INPUT_PULLUP）
void checkButton() {
  static unsigned long last_press = 0;
  static bool button_pressed = false;
  
  bool current_state = (digitalRead(TRIGGER_PIN) == LOW);
  
  if (current_state && !button_pressed) {
    // 按键刚被按下
    if (millis() - last_press > BUTTON_DEBOUNCE_MS) {
      button_pressed = true;
      last_press = millis();
      Serial.println("🔘 Button pressed");
    }
  } else if (!current_state && button_pressed) {
    // 按键释放
    unsigned long press_duration = millis() - last_press;
    button_pressed = false;
    
    if (press_duration > LONG_PRESS_MS) {
      // 长按：恢复出厂设置
      Serial.println("🔄 Long press detected (>3s): Factory reset initiated");
      Serial.println("   Clearing all saved configurations...");
      
      // 清除 Preferences
      if (prefs.begin("config", false)) {
        prefs.clear();
        prefs.end();
        Serial.println("   ✅ Preferences cleared");
      } else {
        Serial.println("   ❌ Failed to clear preferences");
      }
      
      // 清除 WiFi 配置
      wm.resetSettings();
      Serial.println("   ✅ WiFi settings cleared");
      
      // 重置全局缓冲区为默认值
      strcpy(bafa_uid_buf, DEFAULT_BAFA_UID);
      strcpy(bafa_topic_buf, DEFAULT_BAFA_TOPIC);
      strcpy(ble_mac_buf, DEFAULT_BLE_MAC);
      strcpy(ble_data_buf, DEFAULT_BLE_DATA);
      
      safeRestart("Factory reset completed");
      
    } else {
      // 短按：启动配置门户
      Serial.println("⚙️  Short press detected: Starting config portal");
      current_status = STATUS_CONFIG_MODE;
      
      wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
      
      if (!wm.startConfigPortal("ESP32-OnDemand", "12345678")) {
        Serial.println("❌ Config portal failed or timed out");
        current_status = (WiFi.status() == WL_CONNECTED) ? STATUS_CONNECTED : STATUS_ERROR;
      } else {
        Serial.println("✅ Config portal completed successfully");
        current_status = STATUS_CONNECTED;
        
        Serial.println("📶 Updated connection info:");
        Serial.println("   SSID: " + WiFi.SSID());
        Serial.println("   IP: " + WiFi.localIP().toString());
        Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
        
        // 连接巴法云服务器
        connect_server();
      }
    }
  }
}

// 连接巴法云服务器并订阅主题
void connect_server() {
  Serial.print("Connecting to Bemfa Cloud...");
  
  if (!client.connect(host, port)) {
    Serial.println(" connection failed!");
    delay(2000);
    return;
  }

  Serial.println(" connected!");

  // 发送订阅指令：cmd=1&uid=xxx&topic=xxx
  String subscribeCmd = "cmd=1&uid=" + String(bafa_uid_buf) + "&topic=" + String(bafa_topic_buf) + "\r\n";
  client.print(subscribeCmd);

  Serial.println("Subscribed to topic: " + String(bafa_topic_buf));
}

// 发送心跳包
void send_heartbeat() {
  String heartbeat = "cmd=0&msg=ping\r\n";
  client.print(heartbeat);
  Serial.println("Heartbeat sent.");
}

// 初始化BLE
void initBLE() {
  if (bleInitialized) return;
  
  Serial.println("Initializing BLE...");
  
  // 设置自定义MAC地址（如果提供）
  if (strlen(ble_mac_buf) > 0) {
    uint8_t customMAC[6];
    sscanf(ble_mac_buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &customMAC[0], &customMAC[1], &customMAC[2], 
           &customMAC[3], &customMAC[4], &customMAC[5]);
    customMAC[5]=customMAC[5]-2;
    if (esp_base_mac_addr_set(customMAC) == ESP_OK) {
      Serial.println("Custom MAC address set successfully");
    } else {
      Serial.println("Failed to set custom MAC address");
    }
  } else {
    // 使用默认MAC地址
    newMAC[5] = newMAC[5]-2;
    if (esp_base_mac_addr_set(newMAC) == ESP_OK) {
      Serial.println("Custom MAC address set successfully");
    } else {
      Serial.println("Failed to set custom MAC address");
    }
  }
  
  // 打印使用的MAC地址
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  Serial.print("BLE MAC Address: ");
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // 初始化BLE设备
  BLEDevice::init(DEVICE_NAME);

  // 获取广告对象
  pAdvertising = BLEDevice::getAdvertising();

  // 设置广播参数
  pAdvertising->setMinInterval(0x0020);  // 最小广播间隔
  pAdvertising->setMaxInterval(0x0040);  // 最大广播间隔
  pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND); // 非连接广播

  bleInitialized = true;
  Serial.println("BLE initialized");
}

// 开始BLE广播
void startBLEAdvertising() {
  if (!bleInitialized) {
    initBLE();
  }
  
  Serial.println("Starting BLE advertising for 1 second...");
  
  // 打印将要使用的MAC地址
  Serial.print("Using MAC address: ");
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", 
                newMAC[0], newMAC[1], newMAC[2], newMAC[3], newMAC[4], newMAC[5]);
  
  // 停止当前广播（如果正在运行）
  pAdvertising->stop();
  
  // 创建广播数据
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  
  // 检查是否有配置的广播数据，如果有则使用配置的数据，否则使用默认的wake_adv_data
  if (strlen(ble_data_buf) > 0) {
    // 使用配置的十六进制字符串数据
    std::string hexData = hexToBytes(String(ble_data_buf));
    oAdvertisementData.addData(hexData);
    Serial.println("BLE Beacon started with configured data: " + String(ble_data_buf));
  } else {
    // 使用预定义的wake_adv_data数组作为原始广播数据
    std::string advertDataString(reinterpret_cast<char*>(wake_adv_data), sizeof(wake_adv_data));
    oAdvertisementData.addData(advertDataString);
    Serial.println("BLE Beacon started with predefined data...");
  }

  // 设置广播数据
  pAdvertising->setAdvertisementData(oAdvertisementData);

  // 启动广播
  pAdvertising->start();
  
  // 记录广播开始时间
  bleAdvertisingStart = millis();
}

// 停止BLE广播
void stopBLEAdvertising() {
  if (bleInitialized) {
    pAdvertising->stop();
    bleAdvertisingStart = 0;  // 重置时间
    Serial.println("BLE advertising stopped");
  }
}

// 处理BLE广告持续时间
void handleBLEAdvertising() {
  // 如果正在广播且已达到持续时间，则停止广播
  if (bleAdvertisingStart > 0 && (millis() - bleAdvertisingStart >= BLE_ADVERTISING_DURATION)) {
    stopBLEAdvertising();
  }
}

// 将十六进制字符串转换为字节数组
std::string hexToBytes(const String& hex) {
  std::string result;
  for (unsigned int i = 0; i < hex.length(); i += 2) {
    std::string byte = hex.substring(i, i+2).c_str();
    result.push_back((char) strtol(byte.c_str(), NULL, 16));
  }
  return result;
}