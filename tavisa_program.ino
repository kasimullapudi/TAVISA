#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <vector>
#include "Pulse.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Structure to hold Dosha values
struct DoshaValues {
  double vata;
  double pitta;
  double kapha;
};

//40:7F:F9:26:B9:A5
// RSSI threshold for filtering devices by distance
// const int RSSI_THRESHOLD = -60; // Adjust this value based on your requirements



// Global variables
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
MAX30105 particleSensor;
Pulse pulseIR;
Pulse pulseRed;

BLEServer* pServer = nullptr;
BLECharacteristic* pHeartRateCharacteristic = nullptr;
BLECharacteristic* pSpO2Characteristic = nullptr;
BLECharacteristic* pDoshaCharacteristic = nullptr;
BLECharacteristic* pUserDetailsCharacteristic = nullptr;
BLECharacteristic* pUnauthorisedDisconnection = nullptr;
bool deviceConnected = false;

const byte RATE_SIZE = 8;  // Increased buffer size for better averaging
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
double beatsPerMinute;
int beatAvg;
double filteredHR = 60.0;  // Initial heart rate estimate

int age;
String gender;
double weight;
double height;
double bmi;
String genderCategory;
String special_state;
String state_details;
bool dataReceived = false;
bool measurementComplete = false;

double lastHeartRate = 0;
double lastSpO2 = 0;
DoshaValues currentDoshas = { 0, 0, 0 };

// Updated threshold value for IR detection
const long IR_THRESHOLD = 120000;  // Adjusted for better finger detection
void displayCenteredText(const String& text, int yOffset = 0) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2 + yOffset);
  display.println(text);
}



// Function to parse user details from string
void parseUserDetails(const std::string& data) {
  String dataStr = String(data.c_str());
  int commaIndex1 = dataStr.indexOf(',');
  int commaIndex2 = dataStr.indexOf(',', commaIndex1 + 1);
  int commaIndex3 = dataStr.indexOf(',', commaIndex2 + 1);
  int commaIndex4 = dataStr.indexOf(',', commaIndex3 + 1);
  int commaIndex5 = commaIndex4 != -1 ? dataStr.indexOf(',', commaIndex4 + 1) : -1;

  if (commaIndex1 != -1 && commaIndex2 != -1 && commaIndex3 != -1) {
    age = dataStr.substring(0, commaIndex1).toInt();
    gender = dataStr.substring(commaIndex1 + 1, commaIndex2);
    gender.trim();

    if (commaIndex4 == -1 && commaIndex5 == -1) {
      // Male case (age,gender,weight,height)
      genderCategory = "man";
      weight = dataStr.substring(commaIndex2 + 1, commaIndex3).toDouble();
      height = dataStr.substring(commaIndex3 + 1).toDouble();
    } else if (commaIndex4 != -1 && commaIndex5 == -1) {
      // Woman without special state (age,gender,weight,height,special_state)
      genderCategory = "woman-no-special-state";
      weight = dataStr.substring(commaIndex2 + 1, commaIndex3).toDouble();
      height = dataStr.substring(commaIndex3 + 1, commaIndex4).toDouble();
      special_state = dataStr.substring(commaIndex4 + 1);
      special_state.trim();
    } else if (commaIndex4 != -1 && commaIndex5 != -1) {
      // Woman with special state (age,gender,weight,height,special_state,state_details)
      genderCategory = "woman-special-state";
      weight = dataStr.substring(commaIndex2 + 1, commaIndex3).toDouble();
      height = dataStr.substring(commaIndex3 + 1, commaIndex4).toDouble();
      special_state = dataStr.substring(commaIndex4 + 1, commaIndex5);
      special_state.trim();
      state_details = dataStr.substring(commaIndex5 + 1);
      state_details.trim();
    }

    // Convert weight and height to double
    // weight = w1.toDouble();
    // height = h1.toDouble();



    // Calculate BMI
    bmi = weight / ((height / 100) * (height / 100));
    dataReceived = true;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("User Details:");
    display.print("Age: ");
    display.println(age);
    display.print("Gender: ");
    display.println(gender);
    if (genderCategory.equals("woman-special-state")) {
      display.println("Health state:");
      display.println(special_state);
      if (special_state.equals("Pregnancy")) {
        display.print("Trimester: ");
        display.println(state_details);
      } else {
        display.print("Menopause: ");
        display.println(state_details);
      }
    }
    display.display();
    delay(3000);

    // --- STEP 2: Display weight, height and BMI for 3 seconds ---
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Weight: ");
    display.print(weight);
    display.println(" kg");
    display.print("Height: ");
    display.print(height);
    display.println(" cm");
    display.print("BMI: ");
    display.println(bmi, 1);
    display.display();
    delay(3000);

    // --- STEP 3: Finally, show "Place finger on sensor" ---
    display.clearDisplay();
    display.setTextSize(1.3);
    displayCenteredText("Place finger");
    displayCenteredText("on sensor", 10);
    display.display();
  }

  Serial.print("Weight: ");
  Serial.println(weight);
  Serial.print("Height: ");
  Serial.println(height);
}
//function to create count down for n seconds to detect fingerprint again

void ReScanFingerCountDown(unsigned long duration, long baseIR) {

  unsigned long startTime = millis();
  int remainingTime = duration / 1000;  // Convert to seconds

  while (millis() - startTime < duration) {
    long irValue = particleSensor.getIR();
    if (irValue > baseIR + 20000) {
      break;
    }
    display.clearDisplay();
    display.setTextSize(1.5);
    displayCenteredText("Detecting Finger... ");
    display.setTextSize(2);
    display.setCursor((SCREEN_WIDTH - 36) / 2, 40);
    display.print(remainingTime);
    display.print("s");
    display.display();
    delay(1000);  // Simulate blocking while allowing countdown
    remainingTime--;
  }
  // If no finger was detected, restart the main loop



  display.setTextSize(1);
}

// class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
//     void onResult(BLEAdvertisedDevice advertisedDevice) {
//         // Print the MAC address of the discovered device
//         int rssi = advertisedDevice.getRSSI();

//         // Check if the device is within the desired distance
//         if (rssi >= RSSI_THRESHOLD) {
//             Serial.print("Found nearby device: ");
//             Serial.print(advertisedDevice.getAddress().toString().c_str());


//             Serial.print(" (RSSI: ");
//             Serial.print(rssi);
//             Serial.println(" dBm)");

//         }
//     }
// };

uint16_t connId = 0;
// const char* allowedDev= "68:68:94:32:AA:B7"; //- tavisa 1
const char* allowedDev="79:84:1F:C6:0F:33"; //- tavisa 2
// const char* allowedDev = "4F:A5:92:15:23:0B";  //-tavisa 3
bool connectionAcknowledged = false;
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
    BLEDevice::stopAdvertising();
    // deviceConnected=true;
    // Extract and print MAC address
    uint8_t* mac = param->connect.remote_bda;
    connId = param->connect.conn_id;  // Store connection ID
    char macStr[18];
    snprintf(macStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("Connected Device MAC: ");
    Serial.println(macStr);

    if (strcasecmp(macStr, allowedDev) == 0 || 1==1 ){
      Serial.println("authorised device connected");
      deviceConnected = true;
    } else {
      Serial.println("device connected is not authorised!");

      Serial.println("disconnecting....");

      pServer->disconnect(connId);
      deviceConnected = false;
    }
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("Device disconnected. Waiting for reconnection...");

    deviceConnected = false;
    connectionAcknowledged = false;
    connId = 0;
    // Restart advertising to allow new connections
    BLEDevice::startAdvertising();
    unsigned long disconnectTime = millis();
    bool reconnected = false;
    // Wait for 15 seconds for reconnection
    while (millis() - disconnectTime < 15000) {
      if (deviceConnected) {  // If reconnected during wait time
        reconnected = true;
        Serial.println("Device reconnected within 15 seconds.");
        break;
      }
      delay(100);
    }

    if (!reconnected) {
      Serial.println("No reconnection. Restarting from 'Device Not Connected' state.");

      // Update OLED display
      display.clearDisplay();
      display.setTextSize(1);
      displayCenteredText("Device");
      displayCenteredText("Not connected", 10);
      display.display();
    }
  }
};


class UserDetailsCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    std::string value(pCharacteristic->getValue().c_str());
    if (value.length() > 0) {
      parseUserDetails(value);
    }
  }
};
// Improved SpO2 calculation
double SPO2, SPO2f;

// Improved SpO2 calculation
double calculateSpO2(long red, long ir) {
  // Basic signal validatio

  double R = (double)red / ir;

  SPO2f = -43.060 * R * R + 33.354 * R + 96.845;
  Serial.println(SPO2f);
  return SPO2f;
}

double calculateFinalSpO2(std::vector<double> spo2Readings) {
  if (spo2Readings.empty()) return 0;  // Safety check

  // Step 1: Sort values in ascending order
  std::sort(spo2Readings.begin(), spo2Readings.end());

  // Step 2: Remove outliers (values below 85)
  std::vector<double> filteredSpO2;
  for (double spo2 : spo2Readings) {
    if (spo2 >= 85) {
      filteredSpO2.push_back(spo2);
    }
  }

  if (filteredSpO2.empty()) return 0;  // No valid data

  // Step 3: Take the highest 5 values and average them
  int topCount = 5;
  if (filteredSpO2.size() < topCount) topCount = filteredSpO2.size();

  double sum = 0;
  for (int i = filteredSpO2.size() - topCount; i < filteredSpO2.size(); i++) {
    sum += filteredSpO2[i];
  }

  return sum / topCount;  // Return the final processed SpO₂
}
// Improved heart rate calculation with filtering

double calculateHeartRate(long delta) {
  
  // if (delta <= 0) return filteredHR;

  // Calculate instantaneous heart rate from the inter-beat interval (delta in ms)
  double instantHR = 60000.0 / delta;

  // Reject measurements outside the physiologically plausible range
  if (instantHR < 40 || instantHR > 180) {
    return filteredHR;
  }

  // Determine adaptive weight based on the deviation from the current filtered value.
  double diff = fabs(instantHR - filteredHR);
  double weight = 0.3;                     // default weight
  const double deviationThreshold = 10.0;  // if the new value is more than 10 BPM away, consider it an outlier

  if (diff > deviationThreshold) {
    // Use a smaller weight if the instantaneous reading deviates significantly
    weight = 0.1;
  }

  // Update filteredHR using the exponential moving average formula:
  filteredHR = (1 - weight) * filteredHR + weight * instantHR;

  return filteredHR;
}
double calculateFinalHeartRate(std::vector<double> heartRates) {
  if (heartRates.empty()) return 0;  // Safety check

  // Step 1: Sort values in ascending order
  std::sort(heartRates.begin(), heartRates.end());

  // Step 2: Find the minimum value
  double minHR = heartRates.front();
  double maxHR = heartRates.back();

  Serial.print("Min HR: ");
  Serial.println(minHR);
  Serial.print("Max HR: ");
  Serial.println(maxHR);

  // Step 3: If the lowest HR is within normal range (60-100 BPM), take the full average
  if (minHR >= 60) {
    double sum = 0;
    for (double hr : heartRates) sum += hr;
    return sum / heartRates.size();  // Average of all values
  }

  // Step 4: If minHR is below 60, average only the top N highest values
  int topCount = 3;                                                // Take the highest 3 values (tune as needed)
  if (heartRates.size() < topCount) topCount = heartRates.size();  // Avoid overflow

  double sumHigh = 0;
  for (int i = heartRates.size() - topCount; i < heartRates.size(); i++) {
    sumHigh += heartRates[i];  // Sum of highest values
  }

  return min(100.0,sumHigh / topCount);  // Return average of highest N values
}


class DoshaAnalyzer {
public:
  DoshaValues analyze(double bmi, int age, double heartRate, double spo2, const String& gender, const String& special_state = "General Wellness", const String& state_details = "NA") {
    DoshaValues values;

    if (gender.startsWith("M") || (gender.startsWith("F") && special_state.equals("General Wellness"))) {
      //user details -bmi,age
      values.vata = (bmi < 18.5 ? 60 : bmi < 25 ? 33
                                                : 15)
                      * 0.15
                    + (age < 35 ? 20 : age < 50 ? 30
                                                : 50)
                        * 0.15;
      values.pitta = (bmi < 18.5 ? 25 : bmi < 25 ? 34
                                                 : 25)
                       * 0.15
                     + (age < 35 ? 50 : age < 50 ? 40
                                                 : 20)
                         * 0.15;
      values.kapha = (bmi < 18.5 ? 15 : bmi < 25 ? 33
                                                 : 60)
                       * 0.15
                     + (age < 35 ? 30 : age < 50 ? 30
                                                 : 30)
                         * 0.15;
      //sensor details- hr,spo2
      values.vata += (heartRate > 80 ? 25 : heartRate > 70 ? 15
                                                           : 10)
                     + (spo2 >= 98 ? 10 : spo2 >= 95 ? 15
                                                     : 15);
      values.pitta += (heartRate > 80 ? 15 : heartRate > 70 ? 25
                                                            : 15)
                      + (spo2 >= 98 ? 15 : spo2 >= 95 ? 15
                                                      : 10);
      values.kapha += (heartRate > 80 ? 10 : heartRate > 70 ? 10
                                                            : 25)
                      + (spo2 >= 98 ? 15 : spo2 >= 95 ? 10
                                                      : 5);
    } else {
      if (special_state.equals("Pregnancy_Care")) {
        values.vata = (heartRate > 85 ? 20 : heartRate > 75 ? 15
                                                            : 10);
        values.pitta = (heartRate > 85 ? 20 : heartRate > 75 ? 20
                                                             : 15);
        values.kapha = (heartRate > 85 ? 10 : heartRate > 75 ? 15
                                                             : 25);

        if (state_details.equals("First_Trimester")) {
          values.vata *= 1.2;
          values.pitta *= 1.3;
          values.kapha *= 1.1;
        } else if (state_details.equals("Second_Trimester")) {
          values.vata *= 1.1;
          values.pitta *= 1.2;
          values.kapha *= 1.3;
        } else if (state_details.equals("Third_Trimester")) {
          values.vata *= 1.3;
          values.pitta *= 1.1;
          values.kapha *= 1.2;
        }
        //added values----
        /*values.vata = abs(std::max(0.0, values.vata + (spo2 >= 97 ? -10 : spo2 >= 95 ? 15 : 20)));
              values.pitta = abs(std::max(0.0, values.pitta + (spo2 >= 97 ? 20 : spo2 >= 95 ? 10 : -10)));
              values.kapha = abs(std::max(0.0, values.kapha + (spo2 >= 97 ? 15 : spo2 >= 95 ? -5 : -12)));*/
        if (spo2 >= 97) {
          values.pitta += 20;
          values.kapha += 15;
        } else if (spo2 >= 95) {
          values.vata += 15;
          values.pitta += 10;
        } else {
          values.vata += 20;
        }

      } else if (special_state.equals("Menopausal_Wellness")) {
        values.vata = (heartRate > 80 ? 25 : heartRate > 70 ? 20
                                                            : 15);
        values.pitta = (heartRate > 80 ? 20 : heartRate > 70 ? 25
                                                             : 20);
        values.kapha = (heartRate > 80 ? 5 : heartRate > 70 ? 5
                                                            : 15);

        if (state_details.equals("Early_Stage")) {
          values.vata *= 1.2;
          values.pitta *= 1.4;
          values.kapha *= 0.9;
        } else if (state_details.equals("Late_Stage")) {
          values.vata *= 1.4;
          values.pitta *= 1.1;
          values.kapha *= 0.9;
        }
      }
    }
    return values;
  }
};
std::vector<double> arrHR;
std::vector<double> arrSpo2;

void setup() {
  Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  displayCenteredText("TAVISA");
  display.display();
  delay(3000);

  display.setTextSize(1);
  display.clearDisplay();
  displayCenteredText("Device");
  displayCenteredText("Not connected", 10);
  display.display();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1);
  }

  // Optimized sensor settings for better readings
  particleSensor.setup(0x3C, 4, 2, 200, 411, 4096);
  particleSensor.enableDIETEMPRDY();

  // BLEDevice::init("TAVISA-1");
  BLEDevice::init("TAVISA-2");
  // BLEDevice::init("TAVISA-3");


  // BLEScan* pBLEScan = BLEDevice::getScan();
  // pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  // pBLEScan->setActiveScan(true); // Active scan uses more power but provides more information
  // pBLEScan->setInterval(100);   // Scan interval
  // pBLEScan->setWindow(99);      // Scan window

  // // Start scanning for 30 seconds
  // Serial.println("Scanning for BLE devices...");
  // BLEScanResults* foundDevices = pBLEScan->start(10);

  // // Print the number of devices found
  // Serial.print("Devices found: ");
  // Serial.println(foundDevices->getCount());

  // // Clear the results to free memory
  // pBLEScan->clearResults();



  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");

  pHeartRateCharacteristic = pService->createCharacteristic(
    "beb5483e-36e1-4688-b7f5-ea07361b26a8",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pHeartRateCharacteristic->addDescriptor(new BLE2902());

  pSpO2Characteristic = pService->createCharacteristic(
    "beb5483f-36e1-4688-b7f5-ea07361b26a8",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pSpO2Characteristic->addDescriptor(new BLE2902());

  pDoshaCharacteristic = pService->createCharacteristic(
    "beb54840-36e1-4688-b7f5-ea07361b26a8",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pDoshaCharacteristic->addDescriptor(new BLE2902());

  pUserDetailsCharacteristic = pService->createCharacteristic(
    "beb54841-36e1-4688-b7f5-ea07361b26a8",
    BLECharacteristic::PROPERTY_WRITE);
  pUserDetailsCharacteristic->setCallbacks(new UserDetailsCallbacks());

  pUnauthorisedDisconnection = pService->createCharacteristic(
    "eaa82ed5-c29c-4f62-a895-483e40255bc8",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic ::PROPERTY_NOTIFY);
  pUnauthorisedDisconnection->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("TAVISA Device Ready!");
}

void loop() {

  if (deviceConnected && !dataReceived && !connectionAcknowledged) {
    display.clearDisplay();
    display.setTextSize(1.3);
    displayCenteredText("Device connected");
    display.display();
    delay(1000);
    display.clearDisplay();
    display.setTextSize(1);
    displayCenteredText("Waiting for");
    displayCenteredText("user data...", 10);
    display.display();
    connectionAcknowledged = true;
  }
  if (!deviceConnected || !dataReceived) {
    return;
  }

  bool fingerDetected = false;
  unsigned long startFingerCheck = millis();
  long baseIR = particleSensor.getIR();  // Measure ambient IR before checki
                                         // Do not clear the display here; it already shows "Place finger on sensor"
  while (millis() - startFingerCheck < 10000) {
    long irValue = particleSensor.getIR();
    if (irValue > baseIR + 20000) {  // Dynamic threshold
      delay(500);
      if (particleSensor.getIR() > baseIR + 20000) {
        fingerDetected = true;
        break;
      }
    }
    delay(100);  // Polling delay without altering the display
  }

  if (fingerDetected) {
    display.clearDisplay();
    displayCenteredText("Finger detected!");
    displayCenteredText("Processing...", 10);
    display.display();
    delay(1000);
  }

  if (fingerDetected) {
    unsigned long startTime = millis();
    int validReadings = 0;
    double totalHeartRate = 0;
    double totalSpO2 = 0;
    int countdown = 60;

    while (millis() - startTime < 60000) {
      long irValue = particleSensor.getIR();
      long redValue = particleSensor.getRed();
      //check if finger removed too early

      if (irValue < baseIR + 20000) {
        delay(2000);
        irValue = particleSensor.getIR();
        if (irValue < baseIR + 20000) {
          delay(500);
          display.clearDisplay();
          display.setTextSize(1);
          displayCenteredText("Finger Removed");
          displayCenteredText("too early", 10);
          display.display();
          delay(1000);
          display.clearDisplay();
          delay(500);
          display.setTextSize(1.5);
          displayCenteredText("place finger again");
          displayCenteredText("within 10s", 10);
          display.display();
          delay(1000);
          ReScanFingerCountDown(10000, baseIR);
          if (particleSensor.getIR() < baseIR + 20000) {
            display.clearDisplay();
            display.setTextSize(1.3);
            displayCenteredText("No Finger Detected!");
            display.display();
            delay(2000);
            dataReceived = false;
            goto restart_detection;
            // Exits function, ensuring loop restarts properly
          }
        }
      }

      if (checkForBeat(irValue)) {
        unsigned long delta = millis() - lastBeat;
        Serial.printf("delta: %d ,,, lastbeat: %d\n\n",delta,lastBeat);
        lastBeat = millis();
        
        // Use improved heart rate calculation
        double currentHR = calculateHeartRate(delta);
        Serial.printf("Curent hr: %.2f\n",currentHR);
        if (currentHR > 0) {
          validReadings++;
          arrHR.push_back(currentHR);
          // totalHeartRate += currentHR;

          // Calculate SpO2 with improved formula
          double spo2 = calculateSpO2(redValue, irValue);
          if (spo2 > 0) {
            arrSpo2.push_back(spo2);
            Serial.printf("Curent spo2: %.2f\n",spo2);
            // totalSpO2 += spo2;
          }
        }
      }


      if (millis() % 1000 < 50) {
        display.clearDisplay();
        display.setTextSize(2);
        displayCenteredText("Reading");
        display.setTextSize(2);
        display.setCursor((SCREEN_WIDTH - 36) / 2, 40);
        display.print(countdown);
        display.print("s");
        display.display();
        countdown--;
      }

      delay(20);
    }
    display.clearDisplay();
    display.setTextSize(1.5);
    displayCenteredText("please remove");
    displayCenteredText("your finger", 10);
    display.display();
    delay(4000);
    display.clearDisplay();


    // Calculate final values with validation
    if (validReadings > 0) {
      Serial.print("heart rate: ");
      for (size_t i = 0; i < arrHR.size(); i++) {
        Serial.print(arrHR[i]);
        Serial.print(" ");
      }
      Serial.println();
      Serial.print("spo2: ");
      for (size_t i = 0; i < arrSpo2.size(); i++) {
        Serial.print(arrSpo2[i]);
        Serial.print(" ");
      }
      Serial.println();


      lastHeartRate = calculateFinalHeartRate(arrHR);
      lastSpO2 = calculateFinalSpO2(arrSpo2);
      Serial.println(lastHeartRate);


      std::vector<double>().swap(arrHR);
      std::vector<double>().swap(arrSpo2);
      // Analyze Doshas
      DoshaAnalyzer analyzer;
      currentDoshas = analyzer.analyze(bmi, age, lastHeartRate, lastSpO2, gender, special_state, state_details);

      // Display final results
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Final Results:");
      display.print("HR: ");
      display.print(lastHeartRate, 1);
      display.println(" bpm");
      display.print("SpO2: ");
      display.print(lastSpO2, 1);
      display.println("%");
      display.println("Dosha Values:");
      display.print("Vata: ");
      display.println(currentDoshas.vata, 1);
      display.print("Pitta: ");
      display.println(currentDoshas.pitta, 1);
      display.print("Kapha: ");
      display.println(currentDoshas.kapha, 1);
      display.display();

      // Send data to app
      String heartRateStr = String(lastHeartRate, 1);
      String spo2Str = String(lastSpO2, 1);
      String doshaStr = String(currentDoshas.vata, 1) + "," + String(currentDoshas.pitta, 1) + "," + String(currentDoshas.kapha, 1);

      pHeartRateCharacteristic->setValue(heartRateStr.c_str());
      pHeartRateCharacteristic->notify();
      pSpO2Characteristic->setValue(spo2Str.c_str());
      pSpO2Characteristic->notify();
      pDoshaCharacteristic->setValue(doshaStr.c_str());
      pDoshaCharacteristic->notify();

      // Display results for 7 seconds
      delay(7000);
      // pServer->disconnect(0);

    } else {
      display.clearDisplay();
      display.setTextSize(1.3);
      displayCenteredText("Measurement");
      displayCenteredText("failed", 10);
      displayCenteredText("Try again", 20);
      display.display();
      delay(3000);
    }

// Reset for next measurement
restart_detection:
    dataReceived = false;
    display.clearDisplay();
    display.setTextSize(1.3);
    displayCenteredText("Waiting for");
    displayCenteredText("user data...", 10);
    display.display();
  }
}
