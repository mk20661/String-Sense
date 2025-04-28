#include <PDM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <String_Sense_inferencing.h>

// OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Button configuration
#define BUTTON_PIN 2

// Inference structure
typedef struct {
    int16_t *buffer;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static signed short sampleBuffer[2048];
static bool debug_nn = false; // Set to true to debug features

void setup()
{
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Edge Impulse Inference with OLED and Button");

    // Initialize OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED initialization failed");
        while (1);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Press button to start");
    display.display();

    // Initialize button
    pinMode(BUTTON_PIN, INPUT);

    // Print inference settings
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: %.2f ms.\n", (float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNumber of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    // Initialize microphone
    if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false) {
        ei_printf("ERR: Could not allocate audio buffer\n");
        return;
    }
}

void loop()
{
    // Check if the button is pressed
    if (digitalRead(BUTTON_PIN) == HIGH) {
        Serial.println("Button pressed, starting inference...");

        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.println("Recording...");
        display.display();

        if (!microphone_inference_record()) {
            ei_printf("ERR: Failed to record audio\n");
            return;
        }

        signal_t signal;
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        signal.get_data = &microphone_audio_signal_get_data;

        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
        if (r != EI_IMPULSE_OK) {
            ei_printf("ERR: Failed to run classifier (%d)\n", r);
            return;
        }

        // Find the label with the highest probability
        float max_score = 0.0;
        const char* max_label = "";
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (result.classification[ix].value > max_score) {
                max_score = result.classification[ix].value;
                max_label = result.classification[ix].label;
            }
        }

        // Output to serial monitor
        Serial.print("Detected: ");
        Serial.print(max_label);
        Serial.print(" (");
        Serial.print(max_score * 100, 2);
        Serial.println("%)");

        // Display on OLED
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.println("Detected:");
        display.setTextSize(2);
        display.setCursor(0, 20);
        display.println(max_label);
        display.display();

        delay(1000); // Simple debounce delay
    }
}

// Microphone sampling callback
static void pdm_data_ready_inference_callback(void)
{
    int bytesAvailable = PDM.available();
    int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);

    if (inference.buf_ready == 0) {
        for (int i = 0; i < bytesRead >> 1; i++) {
            inference.buffer[inference.buf_count++] = sampleBuffer[i];
            if (inference.buf_count >= inference.n_samples) {
                inference.buf_count = 0;
                inference.buf_ready = 1;
                break;
            }
        }
    }
}

// Initialize microphone and inference buffer
static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (inference.buffer == NULL) return false;

    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;

    PDM.onReceive(&pdm_data_ready_inference_callback);
    PDM.setBufferSize(4096);
    if (!PDM.begin(1, EI_CLASSIFIER_FREQUENCY)) {
        ei_printf("Failed to start PDM!");
        microphone_inference_end();
        return false;
    }

    PDM.setGain(127);
    return true;
}

// Wait for microphone data to be ready
static bool microphone_inference_record(void)
{
    inference.buf_ready = 0;
    inference.buf_count = 0;
    while (inference.buf_ready == 0) {
        delay(10);
    }
    return true;
}

// Provide sampled audio data to classifier
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);
    return 0;
}

// Stop microphone and free buffer
static void microphone_inference_end(void)
{
    PDM.end();
    free(inference.buffer);
}

// Ensure correct sensor model
#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif
