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

// TFT_eSPI tft = TFT_eSPI();

// Static buffer for audio data - using 16-bit integers (2 bytes per sample)
static int16_t* audioBuffer = nullptr;
static bool debug_nn = false;

// BLE variables
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

void displayTextClear(String text, int x = 20, int y = 100, uint16_t textColor = TFT_WHITE, uint16_t bgColor = TFT_BLACK, uint8_t textSize = 2) {
  // tft.fillScreen(bgColor);
  // tft.setTextColor(textColor, bgColor);
  // tft.setTextSize(textSize);
  // tft.drawString(text, x, y);
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

// Function to run classification on dummy audio data
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
    
    displayTextClear("Generating: " + signalName, 10, 50, TFT_CYAN);
    sendBLEMessage("Generating: " + signalName);
    
    // Generate dummy audio data
    generateDummyAudioData(audioBuffer, TOTAL_SAMPLES, signalType);
    
    displayTextClear("Running Classifier...", 10, 100, TFT_YELLOW);
    sendBLEMessage("Running Classifier for " + signalName);
    
    // Setup signal structure
    signal_t signal;
    signal.total_length = TOTAL_SAMPLES;
    signal.get_data = &audio_signal_get_data;
    
    // Run classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    
    if (r != EI_IMPULSE_OK) {
        displayTextClear("Classification Error: " + String(r), 10, 10, TFT_RED);
        sendBLEMessage("Error: Classification failed (" + String(r) + ")");
        return;
    }
    
    // Display timing information
    String timingInfo = "DSP: " + String(result.timing.dsp) + "ms";
    displayTextClear(timingInfo, 10, 150, TFT_YELLOW);
    sendBLEMessage(timingInfo);
    
    String classifyTime = "Class: " + String(result.timing.classification) + "ms";
    displayTextClear(classifyTime, 10, 200, TFT_YELLOW);
    sendBLEMessage(classifyTime);
    
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
    
    // Display best prediction
    String predictionText = "Predicted: " + bestClass;
    displayTextClear(predictionText, 10, 50, TFT_GREEN);
    sendBLEMessage(predictionText);
    
    String confidenceText = "Confidence: " + String(maxConfidence * 100, 1) + "%";
    displayTextClear(confidenceText, 10, 100, TFT_WHITE);
    sendBLEMessage(confidenceText);
    
    // Send detailed results over BLE
    String detailedResults = "Results for " + signalName + ": ";
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        detailedResults += String(result.classification[ix].label) + ":" + String(result.classification[ix].value * 100, 1) + "% ";
    }
    sendBLEMessage(detailedResults);
    
    // Display all predictions on serial for debugging
    Serial.printf("\nPredictions for %s:\n", signalName.c_str());
    Serial.printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)\n",
        result.timing.dsp, result.timing.classification, result.timing.anomaly);
    
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        Serial.printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
    }
    
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    Serial.printf("    anomaly score: %.5f\n", result.anomaly);
    String anomalyText = "Anomaly: " + String(result.anomaly, 3);
    displayTextClear(anomalyText, 10, 150, TFT_CYAN);
    sendBLEMessage(anomalyText);
#endif
  Serial.println("----------------------------------------");
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  // tft.init();
  // tft.setRotation(1);
  // tft.fillScreen(TFT_BLACK);

  displayTextClear("Audio Classifier Test", 10, 10, TFT_GREEN);
  
  // Initialize BLE
  initBLE();
  
  displayTextClear("Free heap: " + String((int)ESP.getFreeHeap()), 10, 50);
  
  // Calculate required memory size (now 2 bytes per sample)
  size_t bufferSize = TOTAL_SAMPLES * sizeof(int16_t);
  displayTextClear("Needed: " + String(bufferSize) + " bytes", 10, 100);
  
  // Allocate buffer dynamically
  audioBuffer = (int16_t*)malloc(bufferSize);
  
  if (audioBuffer == NULL) {
    displayTextClear("Memory allocation failed!", 10, 10, TFT_RED);
    sendBLEMessage("ERROR: Memory allocation failed!");
    displayTextClear("Not enough heap memory", 10, 100, TFT_RED);
    displayTextClear("Available: " + String(ESP.getFreeHeap()), 10, 150, TFT_RED);
    while(1); // Stop execution
  }
  
  displayTextClear("Buffer allocated OK", 10, 150, TFT_GREEN);
  sendBLEMessage("Memory allocated successfully");
  displayTextClear("Free heap after: " + String(ESP.getFreeHeap()), 10, 200);
  
  // Display model info
  displayTextClear("Sample Count: " + String(TOTAL_SAMPLES), 10, 100);
  sendBLEMessage("Model Info - Samples: " + String(TOTAL_SAMPLES));
  
  String durationText = "Duration: " + String((float)TOTAL_SAMPLES / SAMPLE_FREQ, 1) + "s";
  displayTextClear(durationText, 10, 150);
  sendBLEMessage(durationText);
  
  displayTextClear("Frame size: " + String(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE), 10, 200);
  
  String classCount = "Classes: " + String(sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));
  displayTextClear(classCount, 10, 50);
  sendBLEMessage(classCount);

  displayTextClear("Starting classification...", 10, 200, TFT_WHITE);
  sendBLEMessage("System ready - Starting classification tests");
}

void loop() {
  // Handle BLE connection changes
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    NimBLEDevice::startAdvertising(); // restart advertising
    displayTextClear("Start advertising", 10, 10, TFT_YELLOW);
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // Test different signal types
  for (int signalType = 0; signalType < 6; signalType++) {
    classifyAudioData(signalType);
    delay(2000); // Wait between tests
  }
  
  displayTextClear("Test cycle complete", 10, 200, TFT_GREEN);
  sendBLEMessage("Test cycle completed - Restarting...");
  delay(3000);
}
