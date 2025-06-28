#include <TFT_eSPI.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
// #include <NimBLE2902.h>
#include "audio_classifire.h"
#include <math.h>

#define SAMPLE_FREQ 16000                      
#define TOTAL_SAMPLES EI_CLASSIFIER_RAW_SAMPLE_COUNT // This should be 16000*3 = 48000
#define MIC_PIN 34 

// BLE UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

TFT_eSPI tft = TFT_eSPI();

// Static buffer for audio data - using 16-bit integers (2 bytes per sample)
static int16_t* audioBuffer = nullptr;
static bool debug_nn = false;

// BLE variables
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

int currentTestStep = 0;
int totalTestSteps = 6;
String lastClassification = "None";
float lastConfidence = 0.0;
unsigned long lastUpdateTime = 0;

void displayTextClear(String text, int x = 20, int y = 100, uint16_t textColor = TFT_WHITE, uint16_t bgColor = TFT_BLACK, uint8_t textSize = 2) {
  tft.fillScreen(bgColor);
  tft.setTextColor(textColor, bgColor);
  tft.setTextSize(textSize);
  tft.drawString(text, x, y);
  Serial.println(text);
  delay(1000);
}
class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        displayTextClear("BLE Client Connected", 10, 10, TFT_GREEN);
    };

    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
        displayTextClear("BLE Client Disconnected", 10, 10, TFT_RED);
    }
};



// Function to send BLE message
void sendBLEMessage(String message) {
  if (pCharacteristic != nullptr) {
    pCharacteristic->setValue(message.c_str());
    pCharacteristic->notify();
    displayTextClear("BLE Sent: " + message, 10, 220, TFT_BLUE, TFT_BLACK, 1);
  }
}

// Function to initialize BLE
void initBLE() {
  
  displayTextClear("Initializing BLE...", 10, 10, TFT_CYAN);
  
  NimBLEDevice::init("...");
  // NimBLEDevice::setDeviceName("Audio Classifier");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      NIMBLE_PROPERTY::READ |
                      NIMBLE_PROPERTY::WRITE |
                      NIMBLE_PROPERTY::NOTIFY
                    );

  pService->start();
  
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->enableScanResponse(true);

  // pAdvertising->setMinPreferred(0x0);
  // pAdvertising->setPreferredParams(minInterval, maxInterval);
  pAdvertising->setPreferredParams(0x18, 0x28); // ~30–50 ms

  // set value to 0x00 to not advertise this parameter
  NimBLEDevice::startAdvertising();
  
  displayTextClear("BLE Ready - Advertising", 10, 10, TFT_GREEN);
}

// Function to generate dummy audio data
void generateDummyAudioData(int16_t* buffer, int samples, int signalType = 0) {
  for (int i = 0; i < samples; i++) {
    if (i%1000 == 0){
      displayTextClear("Generating sample " + String(i + 1) + "/" + String(samples), 10, 10, TFT_CYAN);
      // Send progress over BLE
      sendBLEMessage("Progress: " + String(i + 1) + "/" + String(samples));
    }
    switch (signalType) {
      case 0: // Sine wave at 440Hz (A note)
        buffer[i] = (int16_t)(sin(2.0 * PI * 440.0 * i / SAMPLE_FREQ) * 16383); // 16383 = 32767 * 0.5
        break;
      case 1: // White noise
        buffer[i] = random(-9830, 9830); // ~30% of int16 range
        break;
      case 2: // Chirp signal (frequency sweep)
        {
          float t = (float)i / SAMPLE_FREQ;
          float duration = (float)samples / SAMPLE_FREQ;
          float freq = 200 + (2000 * t / duration);
          buffer[i] = (int16_t)(sin(2.0 * PI * freq * t) * 13107); // 13107 = 32767 * 0.4
        }
        break;
      case 3: // Square wave at 100Hz
        buffer[i] = (sin(2.0 * PI * 100.0 * i / SAMPLE_FREQ) > 0) ? 9830 : -9830;
        break;
      case 4: // Impulse train (clicks)
        buffer[i] = (i % (SAMPLE_FREQ / 10) == 0) ? 26214 : 0; // 10 clicks per second
        break;
      default: // Silence
        buffer[i] = 0;
        break;
    }
  }
}

// Callback function to provide audio data to the classifier
static int audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        if (offset + i < TOTAL_SAMPLES) {
            // Convert int16_t to float and normalize to [-1.0, 1.0] range
            out_ptr[i] = (float)audioBuffer[offset + i] / 32767.0f;
        } else {
            out_ptr[i] = 0.0f;
        }
    }
    return 0;
}

// Enhanced display functions
void drawProgressBar(int x, int y, int width, int height, int progress, int total, uint16_t fillColor = TFT_GREEN, uint16_t bgColor = TFT_DARKGREY) {
  // Draw border
  tft.drawRect(x, y, width, height, TFT_WHITE);
  
  // Fill background
  tft.fillRect(x + 1, y + 1, width - 2, height - 2, bgColor);
  
  // Calculate fill width
  int fillWidth = ((width - 2) * progress) / total;
  
  // Draw progress fill
  if (fillWidth > 0) {
    tft.fillRect(x + 1, y + 1, fillWidth, height - 2, fillColor);
  }
  
  // Draw percentage text
  String percentText = String((progress * 100) / total) + "%";
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  int textWidth = tft.textWidth(percentText);
  tft.drawString(percentText, x + (width - textWidth) / 2, y + height + 2);
}

void drawStepIndicator(int x, int y, int currentStep, int totalSteps) {
  int stepWidth = 30;
  int stepHeight = 20;
  int spacing = 5;
  
  for (int i = 0; i < totalSteps; i++) {
    int stepX = x + i * (stepWidth + spacing);
    
    // Choose colors based on step status
    uint16_t fillColor, borderColor;
    if (i < currentStep) {
      fillColor = TFT_GREEN;
      borderColor = TFT_GREEN;
    } else if (i == currentStep) {
      fillColor = TFT_YELLOW;
      borderColor = TFT_YELLOW;
    } else {
      fillColor = TFT_DARKGREY;
      borderColor = TFT_DARKGREY;
    }
    
    // Draw step box
    tft.fillRect(stepX, y, stepWidth, stepHeight, fillColor);
    tft.drawRect(stepX, y, stepWidth, stepHeight, borderColor);
    
    // Draw step number
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK);
    String stepNum = String(i + 1);
    int textWidth = tft.textWidth(stepNum);
    tft.drawString(stepNum, stepX + (stepWidth - textWidth) / 2, y + 6);
  }
}

void drawHeader() {
  // Clear header area
  tft.fillRect(0, 0, 320, 60, TFT_BLACK);
  
  // Draw title
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.drawString("Audio Classifier", 10, 5);
  
  // Draw last classification result
  if (lastClassification != "None") {
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN);
    String result = "Last: " + lastClassification + " (" + String(lastConfidence, 1) + "%)";
    tft.drawString(result, 10, 30);
  }
  
  // Draw connection status
  tft.setTextSize(1);
  if (deviceConnected) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("BLE Connected", 200, 5);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("BLE Disconnected", 200, 5);
  }
  
  // Draw time
  unsigned long currentTime = millis();
  String timeStr = "Time: " + String(currentTime / 1000) + "s";
  tft.setTextColor(TFT_WHITE);
  tft.drawString(timeStr, 200, 20);
  
  // Draw separator line
  tft.drawLine(0, 60, 320, 60, TFT_WHITE);
}

void drawMainDisplay(String mainText, String subText = "", uint16_t mainColor = TFT_WHITE, uint16_t subColor = TFT_DARKGREY) {
  // Clear main display area (below header and above footer)
  tft.fillRect(0, 61, 320, 120, TFT_BLACK);
  
  // Draw main text
  tft.setTextSize(2);
  tft.setTextColor(mainColor);
  int mainTextWidth = tft.textWidth(mainText);
  tft.drawString(mainText, (320 - mainTextWidth) / 2, 80);
  
  // Draw sub text if provided
  if (subText != "") {
    tft.setTextSize(1);
    tft.setTextColor(subColor);
    int subTextWidth = tft.textWidth(subText);
    tft.drawString(subText, (320 - subTextWidth) / 2, 110);
  }
}

void drawFooter() {
  // Clear footer area
  tft.fillRect(0, 181, 320, 60, TFT_BLACK);
  
  // Draw separator line
  tft.drawLine(0, 181, 320, 181, TFT_WHITE);
  
  // Draw step indicator
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Test Progress:", 10, 190);
  drawStepIndicator(10, 205, currentTestStep, totalTestSteps);
  
  // Draw memory info
  String memInfo = "Free: " + String(ESP.getFreeHeap()) + " bytes";
  tft.setTextColor(TFT_CYAN);
  tft.drawString(memInfo, 200, 190);
}

void updateDisplay(String mainText, String subText = "", uint16_t mainColor = TFT_WHITE, uint16_t subColor = TFT_DARKGREY) {
  drawHeader();
  drawMainDisplay(mainText, subText, mainColor, subColor);
  drawFooter();
}

// Enhanced function to generate dummy audio data with progress
void generateDummyAudioDataWithProgress(int16_t* buffer, int samples, int signalType = 0) {
  String signalName;
  switch (signalType) {
    case 0: signalName = "440Hz Sine"; break;
    case 1: signalName = "White Noise"; break;
    case 2: signalName = "Chirp Signal"; break;
    case 3: signalName = "Square Wave"; break;
    case 4: signalName = "Impulse Train"; break;
    default: signalName = "Silence"; break;
  }
  
  int progressUpdateInterval = samples / 20; // Update 20 times
  
  for (int i = 0; i < samples; i++) {
    if (i % progressUpdateInterval == 0) {
      int progress = (i * 100) / samples;
      
      // Clear progress area
      tft.fillRect(0, 140, 320, 40, TFT_BLACK);
      
      // Draw progress bar
      drawProgressBar(50, 145, 220, 15, i, samples, TFT_BLUE, TFT_DARKGREY);
      
      // Draw progress text
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE);
      String progressText = "Generating " + signalName + "...";
      int textWidth = tft.textWidth(progressText);
      tft.drawString(progressText, (320 - textWidth) / 2, 130);
      
      sendBLEMessage("Generating " + signalName + ": " + String(progress) + "%");
    }
    
    // Generate sample based on signal type (same as before)
    switch (signalType) {
      case 0: // Sine wave at 440Hz
        buffer[i] = (int16_t)(sin(2.0 * PI * 440.0 * i / SAMPLE_FREQ) * 16383);
        break;
      case 1: // White noise
        buffer[i] = random(-9830, 9830);
        break;
      case 2: // Chirp signal
        {
          float t = (float)i / SAMPLE_FREQ;
          float duration = (float)samples / SAMPLE_FREQ;
          float freq = 200 + (2000 * t / duration);
          buffer[i] = (int16_t)(sin(2.0 * PI * freq * t) * 13107);
        }
        break;
      case 3: // Square wave at 100Hz
        buffer[i] = (sin(2.0 * PI * 100.0 * i / SAMPLE_FREQ) > 0) ? 9830 : -9830;
        break;
      case 4: // Impulse train
        buffer[i] = (i % (SAMPLE_FREQ / 10) == 0) ? 26214 : 0;
        break;
      default: // Silence
        buffer[i] = 0;
        break;
    }
  }
}

// Enhanced classification function
void classifyAudioData(int signalType) {
    String signalName;
    switch (signalType) {
      case 0: signalName = "440Hz Sine Wave"; break;
      case 1: signalName = "White Noise"; break;
      case 2: signalName = "Chirp Signal"; break;
      case 3: signalName = "100Hz Square Wave"; break;
      case 4: signalName = "Impulse Train"; break;
      default: signalName = "Silence"; break;
    }
    
    currentTestStep = signalType;
    
    updateDisplay("Test " + String(signalType + 1) + "/" + String(totalTestSteps), 
                  "Preparing " + signalName, TFT_CYAN, TFT_WHITE);
    
    sendBLEMessage("Starting test " + String(signalType + 1) + ": " + signalName);
    delay(1000);
    
    // Generate dummy audio data with enhanced progress display
    generateDummyAudioDataWithProgress(audioBuffer, TOTAL_SAMPLES, signalType);
    
    updateDisplay("Classifying...", signalName, TFT_YELLOW, TFT_WHITE);
    sendBLEMessage("Running classifier for " + signalName);
    
    // Setup signal structure
    signal_t signal;
    signal.total_length = TOTAL_SAMPLES;
    signal.get_data = &audio_signal_get_data;
    
    // Run classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    
    if (r != EI_IMPULSE_OK) {
        updateDisplay("Error!", "Classification failed", TFT_RED, TFT_WHITE);
        sendBLEMessage("Error: Classification failed (" + String(r) + ")");
        delay(2000);
        return;
    }
    
    // Find the class with highest confidence
    float maxConfidence = 0.0;
    String bestClass = "Unknown";
    
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        float confidence = result.classification[ix].value;
        
        if (confidence > maxConfidence) {
            maxConfidence = confidence;
            bestClass = String(result.classification[ix].label);
        }
    }
    
    // Update global variables for header display
    lastClassification = bestClass;
    lastConfidence = maxConfidence * 100;
    
    // Display results
    String confidenceText = String(maxConfidence * 100, 1) + "% confidence";
    updateDisplay(bestClass, confidenceText, TFT_GREEN, TFT_WHITE);
    
    sendBLEMessage("Result: " + bestClass + " (" + String(maxConfidence * 100, 1) + "%)");
    
    // Show timing info briefly
    delay(1500);
    String timingInfo = "DSP: " + String(result.timing.dsp) + "ms, Class: " + String(result.timing.classification) + "ms";
    updateDisplay("Timing Info", timingInfo, TFT_YELLOW, TFT_CYAN);
    
    delay(2000);
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  updateDisplay("Audio Classifier", "Initializing...", TFT_GREEN, TFT_WHITE);
  
  // Initialize BLE
  initBLE();
  
  updateDisplay("Memory Check", "Free: " + String(ESP.getFreeHeap()) + " bytes", TFT_CYAN, TFT_WHITE);
  delay(1000);
  
  // Calculate required memory size
  size_t bufferSize = TOTAL_SAMPLES * sizeof(int16_t);
  updateDisplay("Allocating Memory", String(bufferSize) + " bytes needed", TFT_YELLOW, TFT_WHITE);
  
  // Allocate buffer dynamically
  audioBuffer = (int16_t*)malloc(bufferSize);
  
  if (audioBuffer == NULL) {
    updateDisplay("MEMORY ERROR!", "Not enough heap space", TFT_RED, TFT_WHITE);
    sendBLEMessage("ERROR: Memory allocation failed!");
    while(1); // Stop execution
  }
  
  updateDisplay("Memory OK!", "Buffer allocated successfully", TFT_GREEN, TFT_WHITE);
  sendBLEMessage("Memory allocated successfully");
  delay(1000);
  
  // Display model info
  String modelInfo = String(TOTAL_SAMPLES) + " samples, " + String((float)TOTAL_SAMPLES / SAMPLE_FREQ, 1) + "s";
  updateDisplay("Model Ready", modelInfo, TFT_CYAN, TFT_WHITE);
  sendBLEMessage("Model Info - " + modelInfo);
  delay(2000);

  updateDisplay("Starting Tests", "6 signal types to test", TFT_WHITE, TFT_CYAN);
  sendBLEMessage("System ready - Starting classification tests");
  delay(1000);
}

void loop() {
  // Handle BLE connection changes
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    NimBLEDevice::startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // Test different signal types
  for (int signalType = 0; signalType < 6; signalType++) {
    classifyAudioData(signalType);
    delay(1000);
  }
  
  currentTestStep = totalTestSteps;
  updateDisplay("Cycle Complete!", "All tests finished", TFT_GREEN, TFT_WHITE);
  sendBLEMessage("Test cycle completed - Restarting in 3 seconds...");
  delay(3000);
  
  // Reset for next cycle
  currentTestStep = 0;
  lastClassification = "None";
  lastConfidence = 0.0;
}
