#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <HX711_ADC.h>
#include <ESP32Servo.h>

// Constants for NeoPixel
#define NEOPIXEL_PIN 5
#define NUMPIXELS 2

// NeoPixel object
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Constants for OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

// TwoWire objects for two displays
TwoWire I2Cone = TwoWire(0);
TwoWire I2Ctwo = TwoWire(1);
Adafruit_SSD1306 LoadDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cone, OLED_RESET);
Adafruit_SSD1306 TargetDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Ctwo, OLED_RESET);

// Pins for Potentiometers and Buttons
const int motorPotPin = 15;
const int targetPotPin = 4;
const int TareButtonPin = 25;
const int ManualButtonPin = 26;
const int StartButtonPin = 27;

// Pins for Load Cell
const int LOADCELL_DT_PIN = 33;
const int LOADCELL_SCK_PIN = 32;
HX711_ADC LoadCell(LOADCELL_DT_PIN, LOADCELL_SCK_PIN);

// Servo setup
Servo myservo;
const int servoPin = 13;
int motorPotValue = 0;
int prevMotorPotValue = 0;
const int servoThreshold = 3;
bool valveClosed = false;
int weight;
float currentWeight = 0;
int servoClose = 180;
int servoOpen = 90;
int servoClosingStep1= 135;
// int servoClosingStep2 = 160;
// int servoClosingStep3 = 160;

// Variables for program control
int startButtonCounter = 0;
int programState = 0;
int targetWeight;
bool autoPouring = false;

void setup() {
  Serial.begin(9600);

  // Initialize OLED displays
  I2Cone.begin(22, 18); // For LoadDisplay
  I2Ctwo.begin(23, 19); // For TargetDisplay

  if (!LoadDisplay.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("LoadDisplay SSD1306 allocation failed");
    for (;;);
  }
  LoadDisplay.setTextSize(2);

  if (!TargetDisplay.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("TargetDisplay SSD1306 allocation failed");
    for (;;);
  }
  TargetDisplay.setTextSize(2);

  // Initialize pins and components
  pinMode(motorPotPin, INPUT);
  pinMode(targetPotPin, INPUT);
  pinMode(TareButtonPin, INPUT_PULLUP);
  pinMode(ManualButtonPin, INPUT_PULLUP);
  pinMode(StartButtonPin, INPUT_PULLUP);

  // Initialize NeoPixel
  pixels.begin();
  pixels.setPixelColor(1, pixels.Color(255, 165, 0)); // Orange (manual mode off)

  // Initialize Load Cell
  LoadCell.begin();
  LoadCell.start(2000);
  LoadCell.setCalFactor(1871.0);
  LoadCell.tare();

  // Initialize Servo
  myservo.attach(servoPin);

  // If in manual mode, start with the valve closed
  if (digitalRead(ManualButtonPin) == HIGH) {
    myservo.write(180); // Close the valve
    valveClosed = true;
  }
}

void loop() {
  // Read the state of the start/stop button
  int startButtonState = digitalRead(StartButtonPin);

  // Increment startButtonCounter if the button is pressed
  if (startButtonState == LOW) {
    startButtonCounter++;
    delay(200);
    
  }

  // Check and control program state
  controlProgramState();

  if (programState == 1 && digitalRead(ManualButtonPin) == HIGH) {
    autoControlServo();
  }


  // Read other sensors and update variables
  targetWeight = map(analogRead(targetPotPin), 0, 4095, 50, 750);

  // Check manual mode
  if (digitalRead(ManualButtonPin) == LOW) {
    manualControlServo();
    pixels.setPixelColor(1, pixels.Color(0, 0, 255)); // Blue (manual mode on)
    valveClosed = false;
    programState = 0;
    startButtonCounter = 0;
  } else {
    
    pixels.setPixelColor(1, pixels.Color(255, 165, 0)); // Orange (manual mode off)
   
   
  }

  // Check for tare button press
  if (digitalRead(TareButtonPin) == LOW) {
    LoadCell.tare();
  }

  // Update load cell data and display
  updateLoadCellAndDisplay();

  // Show NeoPixel status
  pixels.show();
}

// Function to control the start/stop of the program
void controlProgramState() {
  if(startButtonCounter == 0){
    programState = 0;
  }
  if (startButtonCounter % 2 == 1 && programState == 0) {
    Serial.println("Program started");
    myservo.write(servoOpen); // Open the valve
    valveClosed = false;
    programState = 1;
  } else if (startButtonCounter % 2 == 0 && programState == 1) {
    Serial.println("Program stopped");
    myservo.write(servoClose); // Close the valve
    valveClosed = true;
    programState = 0;
  }
}

// Function to manually control the servo
void manualControlServo() {
  motorPotValue = analogRead(motorPotPin);
  motorPotValue = map(motorPotValue, 0, 4095, servoClose, servoOpen);
  if (abs(motorPotValue - prevMotorPotValue) > servoThreshold) {
    myservo.write(motorPotValue);
    prevMotorPotValue = motorPotValue;
  }
}

void autoControlServo() {
  updateLoadCellAndDisplay();

  float weightStep1 = targetWeight * 0.80; // 20% below target
  float minAcceptanceLimit = targetWeight * 0.98; // 2% below target


  if (currentWeight < weightStep1) {
    myservo.write(servoOpen); // Open the valve if weight is below target - 20%
    Serial.println("Servo: Open");
  }
  
  else if (currentWeight < minAcceptanceLimit) {
    myservo.write(servoClosingStep1); // first step in closing process. Lid closes halfway when currentWeight reaches targetWeight-20%
    
  }
  else {
    myservo.write(servoClose); // Close the valve if weight exceeds target
    valveClosed = true; // Close valve and keep it closed
    programState = 0;
    startButtonCounter = 0;
    Serial.println("Servo: Close");
     Serial.print("Current Weight: ");
    Serial.println(currentWeight);
  }
  Serial.print("Current Weight: ");
  Serial.println(currentWeight);
}




// Function to update load cell data and display
void updateLoadCellAndDisplay() {
  LoadCell.update();
  currentWeight = LoadCell.getData();
  displayWeight(&LoadDisplay, currentWeight);
  displayWeight(&TargetDisplay, targetWeight);
  updateStatusPixel(currentWeight, targetWeight);
}

// Function to update NeoPixel status
void updateStatusPixel(float currentWeight, int targetWeight) {
  if (currentWeight < targetWeight) {
    pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Yellow
  } else if (currentWeight <= targetWeight + 5 && currentWeight >= targetWeight - 5) {
    pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green
  } else {
    pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red
  }
}

// Function to display weight on OLED
void displayWeight(Adafruit_SSD1306* display, float weight) {
  display->clearDisplay();
  display->setTextColor(SSD1306_WHITE);
  display->setCursor(0, 0);
  display->print(weight);
  display->println("g");
  display->display();
}

