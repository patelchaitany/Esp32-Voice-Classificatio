#include <TFT_eSPI.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
// #include <NimBLE2902.h>
#include "audio_classifire.h"
#include <driver/i2s.h>
#include <math.h>

#define SAMPLE_FREQ 16000                      
#define TOTAL_SAMPLES EI_CLASSIFIER_RAW_SAMPLE_COUNT // This should be 16000*3 = 48000
// I2S Configuration for INMP441
#define I2S_WS 15    // Word Select (LRCL)
#define I2S_SCK 13   // Bit Clock (BCLK) 
#define I2S_SD 32 

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

void configureI2S() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_FREQ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (result != ESP_OK) {
    updateDisplay("I2S Error!", "Driver install failed", TFT_RED, TFT_WHITE);
    return;
  }

  result = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (result != ESP_OK) {
    updateDisplay("I2S Error!", "Pin config failed", TFT_RED, TFT_WHITE);
    return;
  }

  result = i2s_zero_dma_buffer(I2S_NUM_0);
  if (result != ESP_OK) {
    updateDisplay("I2S Error!", "DMA clear failed", TFT_RED, TFT_WHITE);
    return;
  }

  updateDisplay("I2S Ready", "Microphone configured", TFT_GREEN, TFT_WHITE);
  sendBLEMessage("I2S microphone configured successfully");
}

// Function to generate dummy audio data
void sampleAudioData(int16_t* buffer, int samples) {
  updateDisplay("Recording...", "Sampling audio from mic", TFT_YELLOW, TFT_WHITE);
  sendBLEMessage("Starting audio recording...");
  
  int progressUpdateInterval = samples / 20; // Update 20 times
  int32_t i2s_buffer[1024]; // Temporary buffer for I2S data
  size_t bytes_read = 0;
  int samplesRead = 0;
  
  // Start I2S
  i2s_start(I2S_NUM_0);
  
  while (samplesRead < samples) {
    // Calculate how many samples to read this iteration
    int samplesToRead = min(1024, samples - samplesRead);
    
    // Read from I2S
    esp_err_t result = i2s_read(I2S_NUM_0, i2s_buffer, samplesToRead * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    
    if (result != ESP_OK) {
      updateDisplay("I2S Error!", "Read failed", TFT_RED, TFT_WHITE);
      sendBLEMessage("ERROR: I2S read failed");
      break;
    }
    
    int actualSamples = bytes_read / sizeof(int32_t);
    
    // Convert 32-bit I2S data to 16-bit and store in buffer
    for (int i = 0; i < actualSamples && samplesRead < samples; i++) {
      // INMP441 data is in the upper 24 bits of the 32-bit word
      // Shift right by 16 to get 16-bit data
      buffer[samplesRead] = (int16_t)(i2s_buffer[i] >> 16);
      Serial.printf("Sample %d: %d\n", samplesRead, buffer[samplesRead]);
      samplesRead++;
    }
    
    // Update progress display
    if (samplesRead % progressUpdateInterval == 0 || samplesRead >= samples) {
      int progress = (samplesRead * 100) / samples;
      
      // Clear progress area
      tft.fillRect(0, 140, 320, 40, TFT_BLACK);
      
      // Draw progress bar
      drawProgressBar(50, 145, 220, 15, samplesRead, samples, TFT_BLUE, TFT_DARKGREY);
      
      // Draw progress text
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE);
      String progressText = "Recording audio...";
      int textWidth = tft.textWidth(progressText);
      tft.drawString(progressText, (320 - textWidth) / 2, 130);
      
      sendBLEMessage("Recording: " + String(progress) + "% (" + String(samplesRead) + "/" + String(samples) + ")");
    }
  }
  
  // Stop I2S
  i2s_stop(I2S_NUM_0);
  
  updateDisplay("Recording Complete", String(samplesRead) + " samples captured", TFT_GREEN, TFT_WHITE);
  sendBLEMessage("Audio recording completed - " + String(samplesRead) + " samples");
  delay(1000);
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


// Enhanced function to generate dummy audio data with progress
// Enhanced classification function
void classifyRealAudio() {
    currentTestStep = 0;
    
    updateDisplay("Prepare to Record", "Audio will be captured for 3 seconds", TFT_CYAN, TFT_WHITE);
    sendBLEMessage("Preparing to record audio - 3 second duration");
    delay(2000);
    
    // Sample real audio data
    sampleAudioData(audioBuffer, TOTAL_SAMPLES);
    
    updateDisplay("Classifying...", "Processing recorded audio", TFT_YELLOW, TFT_WHITE);
    sendBLEMessage("Running classifier on recorded audio");
    
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
    
    // Show all classification results
    delay(2000);
    updateDisplay("All Results:", "", TFT_CYAN, TFT_WHITE);
    delay(1000);
    
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        String className = String(result.classification[ix].label);
        float confidence = result.classification[ix].value * 100;
        
        updateDisplay(className, String(confidence, 1) + "%", 
                     confidence > 50 ? TFT_GREEN : TFT_WHITE, TFT_DARKGREY);
        sendBLEMessage(className + ": " + String(confidence, 1) + "%");
        delay(1500);
    }
    
    // Show timing info
    String timingInfo = "DSP: " + String(result.timing.dsp) + "ms, Class: " + String(result.timing.classification) + "ms";
    updateDisplay("Timing Info", timingInfo, TFT_YELLOW, TFT_CYAN);
    sendBLEMessage("Timing - " + timingInfo);
    
    delay(3000);
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
  

   // Configure I2S for INMP441 microphone
  configureI2S();
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
    classifyRealAudio();
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
