#include <TFT_eSPI.h>
#include <SPI.h>
#include "audio_classifire.h"
#include <math.h>



#define SAMPLE_FREQ 16000                      
#define TOTAL_SAMPLES EI_CLASSIFIER_RAW_SAMPLE_COUNT // This should be 16000*3 = 48000
#define MIC_PIN 34 
TFT_eSPI tft = TFT_eSPI();

// Static buffer for audio data - using 16-bit integers (2 bytes per sample)
static int16_t* audioBuffer = nullptr;
static bool debug_nn = false;

void displayTextClear(String text, int x = 20, int y = 100, uint16_t textColor = TFT_WHITE, uint16_t bgColor = TFT_BLACK, uint8_t textSize = 2) {
  tft.fillScreen(bgColor);
  tft.setTextColor(textColor, bgColor);
  tft.setTextSize(textSize);
  tft.drawString(text, x, y);
  Serial.println(text);
  delay(1000);
}
// Function to generate dummy audio data
void generateDummyAudioData(int16_t* buffer, int samples, int signalType = 0) {
  for (int i = 0; i < samples; i++) {
    if (i%1000 == 0){
      displayTextClear("Generating sample " + String(i + 1) + "/" + String(samples), 10, 10, TFT_CYAN);
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
    
    // Generate dummy audio data
    generateDummyAudioData(audioBuffer, TOTAL_SAMPLES, signalType);
    
    displayTextClear("Running Classifier...", 10, 100, TFT_YELLOW);
    
    // Setup signal structure
    signal_t signal;
    signal.total_length = TOTAL_SAMPLES;
    signal.get_data = &audio_signal_get_data;
    
    // Run classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    
    if (r != EI_IMPULSE_OK) {
        displayTextClear("Classification Error: " + String(r), 10, 10, TFT_RED);
        return;
    }
    
    // Display timing information
    String timingInfo = "DSP: " + String(result.timing.dsp) + "ms";
    displayTextClear(timingInfo, 10, 150, TFT_YELLOW);
    
    String classifyTime = "Class: " + String(result.timing.classification) + "ms";
    displayTextClear(classifyTime, 10, 200, TFT_YELLOW);
    
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
    
    String confidenceText = "Confidence: " + String(maxConfidence * 100, 1) + "%";
    displayTextClear(confidenceText, 10, 100, TFT_WHITE);
    
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
#endif

    Serial.println("----------------------------------------");
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  displayTextClear("Audio Classifier Test", 10, 10, TFT_GREEN);
  
  displayTextClear("Free heap: " + String((int)ESP.getFreeHeap()), 10, 50);
  
  // Calculate required memory size (now 2 bytes per sample)
  size_t bufferSize = TOTAL_SAMPLES * sizeof(int16_t);
  displayTextClear("Needed: " + String(bufferSize) + " bytes", 10, 100);
  
  // Allocate buffer dynamically
  audioBuffer = (int16_t*)malloc(bufferSize);
  
  if (audioBuffer == NULL) {
    displayTextClear("Memory allocation failed!", 10, 10, TFT_RED);
    displayTextClear("Not enough heap memory", 10, 100, TFT_RED);
    displayTextClear("Available: " + String(ESP.getFreeHeap()), 10, 150, TFT_RED);
    while(1); // Stop execution
  }
  
  displayTextClear("Buffer allocated OK", 10, 150, TFT_GREEN);
  displayTextClear("Free heap after: " + String(ESP.getFreeHeap()), 10, 200);
  
  // Display model info
  displayTextClear("Sample Count: " + String(TOTAL_SAMPLES), 10, 100);
  
  String durationText = "Duration: " + String((float)TOTAL_SAMPLES / SAMPLE_FREQ, 1) + "s";
  displayTextClear(durationText, 10, 150);
  
  displayTextClear("Frame size: " + String(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE), 10, 200);
  
  String classCount = "Classes: " + String(sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));
  displayTextClear(classCount, 10, 50);

  displayTextClear("Starting classification...", 10, 200, TFT_WHITE);
}

void loop() {
  // Test different signal types
  for (int signalType = 0; signalType < 6; signalType++) {
    classifyAudioData(signalType);
    delay(2000); // Wait between tests
  }
  
  displayTextClear("Test cycle complete", 10, 200, TFT_GREEN);
  delay(3000);
}
