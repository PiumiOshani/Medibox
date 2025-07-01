//libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <WiFi.h>
#include <DHTesp.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>


#define BUZZER 18
#define LED_1 15
#define LED_2 2
#define CANCEL 34
#define UP 35
#define DOWN 36
#define OK 33
#define DHT 12
#define LDR_PIN 32         
#define SERVO_PIN 13       

#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET_DST 0

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

//MQTT settings
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "medibox_esp32"

//object declarations
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;
Servo shadeServo;

WiFiClient espClient;
PubSubClient mqttClient(espClient);


//variables
int UTC_OFFSET = 19800;
int n_notes = 8;
int C = 262;
int D = 294;
int E = 330;
int F = 349;
int G = 392;
int A = 440;
int B = 494;
int C_H = 523;
int notes[] = {C, D, E, F, G, A, B, C_H};

int hours = 0;
int minutes = 0;

bool alarm_enables = false;
int n_alarms =2;
int alarm_hours[] = {0, 0};
int alarm_minutes[] = {0, 0};
bool alarm_triggered[] = {false, false};

int current_mode = 0;
int max_modes = 4;
String option[] = {"Set Alarm 1" ,"Set Alarm 2", "Set Time", "Remove Alarm" };

//for temperature and humidity
float tem = 0 ;   
float hum = 0 ;

//for LDR Sensor
float lightIntensity = 0.0;  
int ldrValue = 0;            
int ldrMin = 0;            
int ldrMax = 4095; 
int sampleCount = 0;         
float lightSum = 0.0;           

// Sampling and sending interval variables
unsigned long samplingInterval = 5000;  // Default 5 seconds 
unsigned long sendingInterval = 12000; // Default 2 minutes 
unsigned long lastSamplingTime = 0;
unsigned long lastSendingTime = 0;

// Servo control variables
float theta_offset = 30.0;   // Default minimum angle (degrees)
float gamma_factor = 0.75;          // Default controlling factor
float T_med = 30.0;          // Default ideal medicine storage temperature (°C)
float servoAngle = 30.0;

void Display_centered_text(String text, int y, int size);
void get_time();
void set_time();
void update_time_with_check_alarm();
int  wait_for_button_press();
void display_menu();
void go_to_menu();
void run_mode(int mode);
void set_alarm(int alarm);
void ring_alarm(int alarm_index);
void remove_alarm();
void check_temp();
void setupMQTT();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void readLightSensor();
void updateServoPosition();
void sendDataToMQTT();

static const unsigned char PROGMEM icon_alarm[] = {
  0x18, 0x24, 0x42, 0x81, 0x99, 0xA5, 0x42, 0x3C
};

// Function to display a centered string
void Display_centered_text(String text, int y, int size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

// Function to wait for button press
int wait_for_button_press() {
  while (true) {
    if (digitalRead(UP) == LOW) {
      delay(200);
      return UP;
    }
    else if (digitalRead(DOWN) == LOW) {
      delay(200);
      return DOWN;
    }
    else if (digitalRead(CANCEL) == LOW) {
      delay(200);
      return CANCEL;
    }
    else if (digitalRead(OK) == LOW) {
      delay(200);
      return OK;
    }
  }
}

// function to automatically update the current time while checking for alarms 
void update_time_with_check_alarm() { 
  // update time 
  display.clearDisplay(); 
  get_time(); 


  // check for alarms 
  if (alarm_enables) { 
    // iterating through all alarms 
    for (int i = 0; i < n_alarms; i++) { 
      if (alarm_triggered[i] == false && hours == alarm_hours[i] && minutes == alarm_minutes[i]) { 
        ring_alarm(i); // call the ringing function 
         
      } 
    }  
  }
}  

//function to update the current time  and display
void get_time() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Failed to obtain time");
    display.display();
    return;
  }

  // Clear display
  display.clearDisplay();
  
  // Display time
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  Display_centered_text(timeStr, 5, 2);

  char hour_str[8];
  char min_str[8];

  strftime(hour_str, 8, "%H", &timeinfo);
  strftime(min_str, 8, "%M", &timeinfo);

  hours = atoi(hour_str);
  minutes = atoi(min_str);
  
  // Display date
  char dateStr[20];
  const char* weekDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  sprintf(dateStr, "%s, %s %02d, %d", 
          weekDays[timeinfo.tm_wday], 
          months[timeinfo.tm_mon],
          timeinfo.tm_mday,
          timeinfo.tm_year + 1900);
  Display_centered_text(dateStr, 30, 1);

  //display temperature and Humidity
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("Tem:"+ String(tem)+"C");
  display.setCursor(70, 50);
  display.print("Hum:"+ String(hum)+"%");
  
  display.display();
  delay(1000);
}

// Function to display the main menu
void display_menu() {
  // Clear display and set up menu header
  display.clearDisplay();
  
  // Draw menu title
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(40, 2);
  display.print("MAIN MENU");
  display.setTextColor(SSD1306_WHITE);
  
  // Display menu options
  for (int i = 0; i < max_modes; i++) {
    // Calculate vertical position for each menu item
    int y_pos = 15 + (i * 12);
    
    // Highlight the currently selected option
    if (i == current_mode) {
      // Draw selection indicator
      display.fillRect(0, y_pos - 2, SCREEN_WIDTH, 12, SSD1306_INVERSE);
      display.setTextColor(SSD1306_BLACK);
    } 
    else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    
    // Display option text
    display.setCursor(10, y_pos);
    display.print(option[i]);
    
    // Reset text color
    display.setTextColor(SSD1306_WHITE);
  }
  
  
  display.display();
}

//function to navigate through the menu
void go_to_menu() {
  display.clearDisplay();
  while (digitalRead(CANCEL) == HIGH) {

    // Display the menu with current selection
    display_menu();
  
    //Get the button inputs
    int pressed = wait_for_button_press();
    
    if (pressed == UP) {
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_modes - 1;
      }
      delay(150);
    }

    else if (pressed == DOWN) {
      current_mode += 1;
      current_mode %= max_modes;
      delay(150);
    }
    
    else if (pressed == OK) {
      run_mode(current_mode);
      delay(150);
    }
  }
}

// Function to run the selected mode
void run_mode(int mode) {

  //Alarm setting
  if (mode == 0 || mode == 1) {
    set_alarm(mode);
  }

  //Time setting
  else if (mode == 2){
    set_time();
  }

  //Alarm removing
  else if (mode == 3) {
    remove_alarm();
  }
}

//function to set the Alarm 
void set_alarm(int alarm) {
  
  // Setting the hour
  int temp_hour = alarm_hours[alarm];
  bool setting_active = true;

  while (setting_active) {
    display.clearDisplay();
    
    // Draw title
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(30, 2);
    display.print("SET ALARM " + String(alarm+1));
    display.setTextColor(SSD1306_WHITE);
    
    // Draw alarm icon
    display.drawBitmap(10, 20, icon_alarm, 8, 8, SSD1306_WHITE);
    
    // Display time setting
    display.setTextSize(2);
    display.fillRect(38, 18, 25, 17, SSD1306_INVERSE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(40, 20);
    if (temp_hour < 10) display.print("0");
    display.print(temp_hour);
    
    // Display minutes
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(65, 20);
    display.print(":");
    if (alarm_minutes[alarm] < 10) display.print("0");
    display.print(alarm_minutes[alarm]);
    
    // Instructions
    display.setTextSize(1);
    display.setCursor(0, 45);
    display.print("UP/DOWN: Change hour");
    display.setCursor(0, 55);
    display.print("OK: Next  CANCEL: Exit");
    
    display.display();


    //get the button inputs
    int pressed = wait_for_button_press();

    if (pressed == UP) {
      temp_hour = (temp_hour + 1) % 24;
      delay(150);
    }
    else if (pressed == DOWN) {
      temp_hour = (temp_hour - 1 + 24) % 24;
      delay(150);
    }
    else if (pressed == OK) {
      alarm_hours[alarm] = temp_hour;
      setting_active = false;
    }
    else if (pressed == CANCEL) {
      return;
    }
  }

  // Setting the minute
  int temp_minute = alarm_minutes[alarm];
  setting_active = true;

  while (setting_active) {
    display.clearDisplay();
    
    // Draw title
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(30, 2);
    display.print("SET ALARM " + String(alarm + 1));
    display.setTextColor(SSD1306_WHITE);
    
    // Draw alarm icon
    display.drawBitmap(10, 20, icon_alarm, 8, 8, SSD1306_WHITE);
    
    // Display time setting
    display.setTextSize(2);
    display.setCursor(40, 20);
    if (alarm_hours[alarm] < 10) display.print("0");
    display.print(alarm_hours[alarm]);
    
    display.print(":");
    
    // Display minutes 
    display.fillRect(73, 18, 25, 17, SSD1306_INVERSE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(75, 20);
    if (temp_minute < 10) display.print("0");
    display.print(temp_minute);
    display.setTextColor(SSD1306_WHITE);
    
    // Instructions
    display.setTextSize(1);
    display.setCursor(0, 45);
    display.print("UP/DOWN: Change minute");
    display.setCursor(0, 55);
    display.print("OK: Save  CANCEL: Exit");
    
    display.display();

    //get the button inputs
    int pressed = wait_for_button_press();

    if (pressed == UP) {
      temp_minute = (temp_minute + 1) % 60;
      delay(150);
    }
    else if (pressed == DOWN) {
      temp_minute = (temp_minute - 1 + 60) % 60;
      delay(150);
    }
    else if (pressed == OK) {
      alarm_minutes[alarm] = temp_minute;
      alarm_triggered[alarm] = false;  // Reset trigger status
      alarm_enables = true;
      setting_active = false;
      
      // Show confirmation
      display.clearDisplay();
      Display_centered_text("Alarm " + String(alarm + 1) + " Set!", 20, 1);
      
      // Format time string with leading zeros
      String timeStr = "";
      if (alarm_hours[alarm] < 10) timeStr += "0";
      timeStr += String(alarm_hours[alarm]) + ":";
      if (alarm_minutes[alarm] < 10) timeStr += "0";
      timeStr += String(alarm_minutes[alarm]);
      
      Display_centered_text(timeStr, 35, 2);
      display.display();
      delay(1500); // Show confirmation 
    }
    else if (pressed == CANCEL) {
      return;
    }
  }
}

//Ring the alarm
void ring_alarm(int alarm_index) {
  display.clearDisplay();
  
  String message = "ALARM " + String(alarm_index + 1);
  
  // Flash the message 
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();
    if (i % 2 == 0) {
      Display_centered_text(message, 0, 1);
      Display_centered_text("Medicine  Time!", 10, 2);
    }
    display.display();
    delay(300);
  }
  
  // Light up LED 1
  digitalWrite(LED_1, HIGH);

  bool stoped = false;
  bool snoozed = false;

  // Ring the buzzer
  while ((stoped == false) && (snoozed == false)) {
    for (int i = 0; i < n_notes; i++) {

      //stop the alarm
      if (digitalRead(CANCEL) == LOW) {
        stoped = true;
        delay(200);
        break;
      }

      //snooze the alarm
      if (digitalRead(OK) == LOW) {
        alarm_minutes[alarm_index] += 5 ;
        snoozed = true;
        break;
      }
      
      // Update display during alarm
      display.clearDisplay();
      Display_centered_text(message, 0, 1);
      Display_centered_text("Medicine  Time!", 10, 2);
      Display_centered_text("CANCEL:stop", 40, 1);
      Display_centered_text("OK:snooze", 50, 1);
      
      display.display();
      
      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
      delay(2);
    }
  }
  
  delay(200);
  digitalWrite(LED_1, LOW);
  
  // Confirmation message
  //If stoped
  if (stoped == true){
    display.clearDisplay();
    Display_centered_text("Alarm     Stopped", 20, 2);
    display.display();
    delay(1000);
    alarm_triggered[alarm_index] = true;
  }
  //If snoozed
  if (snoozed == true){
    display.clearDisplay();
    Display_centered_text("Alarm     Snoozed", 20, 2);
    display.display();
    delay(1000);
  }
}

//Remove the alarms
void remove_alarm(){
  int selected = 0;
  
  while (true) {
    String alarms[] = {"Alarm 1","Alarm 2","All Alarms"};

    display.clearDisplay();
    display.clearDisplay();
    display.setTextSize(1);
    Display_centered_text("Remove Alarms", 0, 1);
    display.display();

    // Display alarm options
    for (int i = 0; i < 3; i++) {
      // Calculate vertical position for each item
      int y_pos = 15 + (i * 12);
      
      // Highlight the currently selected option
      if (i == selected) {
        // Draw selection indicator
        display.fillRect(0, y_pos - 2, SCREEN_WIDTH, 12, SSD1306_INVERSE);
        display.setTextColor(SSD1306_BLACK);
      } 
      else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      
      // Display option text
      display.setCursor(10, y_pos);
      display.print(alarms[i]);
      
      // Reset text color
      display.setTextColor(SSD1306_WHITE);
    }
    
    display.display();

    int pressed = wait_for_button_press();

    if (pressed == UP) {
      selected -= 1;
      if (selected < 0) {
        selected = 2;
      }
      delay(150);
    }

    else if (pressed == DOWN) {
      selected += 1;
      selected %= 3;
      delay(150);
    }
    
    
    // Romove the selected alarm
    else if (pressed == OK) {
        
      if (selected != 3){
        alarm_triggered[selected] = false;
        alarm_hours[selected] = 0;
        alarm_minutes[selected] = 0;
      }

      if (selected == 3){
        alarm_enables = false;
        for (int i = 0; i < n_alarms; i++) {
          alarm_triggered[i] = false;
          alarm_hours[i] = 0;
          alarm_minutes[i] = 0;
        }
      }
      display.clearDisplay();
      
      // Show confirmation
      for (int i = 0; i < 3; i++) {
        display.clearDisplay();
        if (i % 2 == 0) {
          Display_centered_text("Alarm Removed!", 25, 1);
        }
        display.display();
        delay(300);
      }
      
      display.clearDisplay();
      Display_centered_text("Alarm Removed!", 25, 1);
      display.display();
      delay(1000);
      break;
    }
    else if (pressed == CANCEL) {
      display.clearDisplay();
      Display_centered_text("Operation Cancelled", 25, 1);
      display.display();
      delay(1000);
      break;
    }
  }

}

//Set the Time according to given UTC offset
void set_time() {
  int temp_offset = UTC_OFFSET;
  bool setting_active = true;
  
  while (setting_active) {
    display.clearDisplay();
    
    // Draw title
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(35, 2);
    display.print("SET UTC OFFSET");
    display.setTextColor(SSD1306_WHITE);
    
    // Display current offset in hours and minutes
    int hours = temp_offset / 3600;
    int minutes = (temp_offset % 3600) / 60;
    char offsetStr[20];
    sprintf(offsetStr, "UTC %+d:%02d", hours, abs(minutes));
    
    display.setTextSize(2);
    display.fillRect((SCREEN_WIDTH - strlen(offsetStr) * 12) / 2 - 5, 25, strlen(offsetStr) * 12 + 10, 20, SSD1306_INVERSE);
    display.setTextColor(SSD1306_BLACK);
    Display_centered_text(offsetStr, 28, 2);
    display.setTextColor(SSD1306_WHITE);
    
    // Instructions
    display.setTextSize(1);
    display.setCursor(0, 45);
    display.print("UP/DOWN: Change offset");
    display.setCursor(0, 55);
    display.print("OK: Save  CANCEL: Exit");
    
    display.display();
    
    int pressed = wait_for_button_press();
    
    if (pressed == UP) {
      // Increment by 30 minutes (1800 seconds)
      temp_offset += 1800;
      delay(150);
    }
    else if (pressed == DOWN) {
      // Decrement by 30 minutes (1800 seconds)
      temp_offset -= 1800;
      delay(150);
    }
    else if (pressed == OK) {
      UTC_OFFSET = temp_offset;
      
      // Reconfigure time with new offset
      configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
      
      // Show confirmation
      display.clearDisplay();
      Display_centered_text("Time Zone Set!", 20, 2);
      
      // Format offset string
      int hours = UTC_OFFSET / 3600;
      int minutes = (UTC_OFFSET % 3600) / 60;
      char confirmStr[20];
      sprintf(confirmStr, "UTC %+d:%02d", hours, abs(minutes));
      Display_centered_text(confirmStr, 45, 1);
      
      display.display();
      delay(1500);
      setting_active = false;
    }
    else if (pressed == CANCEL) {
      return;
    }
  }
}

//function to check the temperature and humidity
void check_temp() { 
  TempAndHumidity data = dhtSensor.getTempAndHumidity(); 

  tem = data.temperature;
  hum = data.humidity;

  bool all_good = true; 
  String tem_status = "" ;
  String hum_status = "" ;

  if (tem >= 32) { 
    all_good = false; 
    digitalWrite(LED_2, HIGH); 
    tem_status = "TEMP HIGH"; 
  } 

  else if (tem <= 24) { 
    all_good = false; 
    digitalWrite(LED_2, HIGH); 
    tem_status = "TEMP LOW"; 
  } 

  if (hum >= 80) { 
    all_good = false; 
    digitalWrite(LED_2, HIGH); 
    hum_status = "HUMD HIGH";
  } 

  else if (hum <= 65) { 
    all_good = false; 
    digitalWrite(LED_2, HIGH); 
    hum_status = "HUMD LOW" ;
  } 

  if (all_good) { 
    digitalWrite(LED_2, LOW); 
  }
  
  else if(!all_good){
    display.clearDisplay();
    Display_centered_text(tem_status, 5, 2);
    Display_centered_text(hum_status, 40, 2);
    display.display();
    delay(2000);
  }
}

// Function to read LDR sensor and calculate light intensity
void readLightSensor() {
  
  ldrValue = analogRead(LDR_PIN);

  lightIntensity = 1.0 - map(ldrValue, ldrMin, ldrMax, 0, 1000) / 1000.0;
  Serial.println(lightIntensity);

  lightSum += lightIntensity;
  sampleCount++;
  
}

// Function to calculate and update servo position
void updateServoPosition() {
  
  // Calculate servo angle using the provided equation
  // θ = θ_offset + (180 - θ_offset) × I × γ × ln(t_s/t_u) × (T/T_med)
  
  // Calculate each component
  float intensityFactor = lightIntensity;
  float intervalFactor = log((float)samplingInterval / (float)sendingInterval);
  float tempFactor = tem / T_med;
  
  // Calculate final angle
  servoAngle = theta_offset + (180 - theta_offset) * intensityFactor * gamma_factor * intervalFactor * tempFactor;
  
  // Constrain angle to valid range
  servoAngle = abs(servoAngle);
  
  // Move servo to calculated angle
  shadeServo.write(servoAngle);
  
  Serial.print("Servo Angle: ");
  Serial.println(servoAngle);
}

// Setup MQTT connection and callbacks
void setupMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

// Reconnect to MQTT if connection is lost
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");
    
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println("Connected to MQTT broker");
      
      // Subscribe to topics for dashboard control
      mqttClient.subscribe("MOTOR_PARAM_OFFSET");
      mqttClient.subscribe("MOTOR_PARAM_gamma");
      mqttClient.subscribe("MOTOR_PARAM_TMED");
      mqttClient.subscribe("SAMPLING_INTERVAL");
      mqttClient.subscribe("SENDING_INTERVAL");

      //Publish default values for dashboard control
      mqttClient.publish("MOTOR_PARAM_OFFSET_DEFAULT", "30.00");
      mqttClient.publish("MOTOR_PARAM_gamma_DEFAULT", "0.75");
      mqttClient.publish("MOTOR_PARAM_TMED_DEFAULT", "30");
      mqttClient.publish("SAMPLING_INTERVAL_DEFAULT", "5");
      mqttClient.publish("SENDING_INTERVAL_DEFAULT", "120");

    } else {
      Serial.print("Failed to connect to MQTT, rc=");
      Serial.println(mqttClient.state());
      Serial.println(" Retrying in 5 seconds");
      delay(5000);
    }
  }
}

// MQTT message callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {

  char message[length];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  
  float value = atof(message);
  
  // Handle different topics
  if (strcmp(topic, "MOTOR_PARAM_OFFSET") == 0) {
    // Update theta_offset (minimum angle)
    theta_offset = value;
    Serial.print("New theta_offset: ");
    Serial.println(theta_offset);
  }
  else if (strcmp(topic, "MOTOR_PARAM_gamma") == 0) {
    // Update gamma_factor (controlling factor)
    gamma_factor = value;
    Serial.print("New gamma: ");
    Serial.println(gamma_factor);
  }
  else if (strcmp(topic, "MOTOR_PARAM_TMED") == 0) {
    // Update T_med (ideal temperature)
    T_med = value;
    Serial.print("New T_med: ");
    Serial.println(T_med);
  }
  else if (strcmp(topic, "SAMPLING_INTERVAL") == 0) {
    // Update sampling interval (convert seconds to milliseconds)
    samplingInterval = value*1000;
    Serial.print("New sampling interval: ");
    Serial.println(samplingInterval);
  }
  else if (strcmp(topic, "SENDING_INTERVAL") == 0) {
    // Update sending interval (convert seconds to milliseconds)
    sendingInterval = value*1000;
    Serial.print("New sending interval: ");
    Serial.println(sendingInterval);
  }
  
  // Update servo position with new parameters
  updateServoPosition();
}

// Function to sent data to MQTT
void sendDataToMQTT() {
  if (sampleCount > 0) {
    // Calculate average light intensity
    float avgLightIntensity = lightSum / sampleCount;
    
    // Convert each value to char array
    char lightAr[10];
    char tempAr[10];
    char humAr[10];
    char angleAr[10];
    
    // Format values as strings
    dtostrf(avgLightIntensity, 4, 2, lightAr);  
    dtostrf(tem, 4, 2, tempAr);                 
    dtostrf(hum, 4, 2, humAr);                  
    sprintf(angleAr, "%d", (int)servoAngle);         
    
    // Publish each data separately
    mqttClient.publish("LIGHT_INTENSITY", lightAr);
    mqttClient.publish("TEMPERATURE", tempAr);
    mqttClient.publish("HUMIDITY", humAr);
    mqttClient.publish("SERVO_ANGLE", angleAr);
    
    // Reset counters
    lightSum = 0.0;
    sampleCount = 0;
    
    // Print confirmation
    Serial.println("Sent MQTT data:");
    Serial.print("Light: "); Serial.println(lightAr);
    Serial.print("Temperature: "); Serial.println(tempAr);
    Serial.print("Humidity: "); Serial.println(humAr);
    Serial.print("Servo angle: "); Serial.println(angleAr);
  }
}

void setup() {
  Serial.begin(9600); 

  pinMode (BUZZER, OUTPUT); 
  pinMode(LED_1, OUTPUT); 
  pinMode(LED_2, OUTPUT); 
  pinMode(CANCEL, INPUT); 
  pinMode (UP, INPUT); 
  pinMode (DOWN, INPUT); 
  pinMode (OK, INPUT); 
  pinMode(LDR_PIN, INPUT);

  dhtSensor.setup(DHT, DHTesp:: DHT22); 

  // Initialize servo
  shadeServo.attach(SERVO_PIN);
  shadeServo.write(theta_offset);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { 
    Serial.println(F("SSD1306 allocation failed")); 
    for (;;); 
  } 

  // Show splash screen
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("MediBox");
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println("Medicine Reminder");
  display.display();
  delay(2000); 

  WiFi.begin("Wokwi-GUEST", "", 6); 

  while (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Connecting to WiFi");
    
    
    display.setCursor(0, 20);
    for (int i = 0; i < 3; i++) {
      display.print(".");
      display.display();
      delay(50);
    }
    
  }


  // Display Connected 
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connected to WiFi");
  display.display();
  delay(200);

  setupMQTT();

  
}

void loop() {
  configTime (UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);

  // Handle MQTT connection
  if (!mqttClient.connected()) {
      reconnectMQTT();
  }
  mqttClient.loop();

  update_time_with_check_alarm();

  if (digitalRead(OK) == LOW) {
    go_to_menu();
  }

  check_temp();

  // Check if it's time to sample light intensity
  unsigned long currentMillis = millis();
  if (currentMillis - lastSamplingTime >= samplingInterval) {
      lastSamplingTime = currentMillis;
      readLightSensor();
      updateServoPosition();
    }
    
  // Check if it's time to send data to MQTT
  if (currentMillis - lastSendingTime >= sendingInterval) {
      lastSendingTime = currentMillis;
      sendDataToMQTT();
  }
  
}