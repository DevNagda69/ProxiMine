#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//                PIN CONFIGURATION (ESP32 Dev Module)

#define TRIG_PIN    33
#define ECHO_PIN    32

#define GREEN_LED   25
#define YELLOW_LED  26
#define RED_LED     27

#define OLED_SDA    21
#define OLED_SCL    22


//                    OLED


#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);


//               DEMO CONFIGURATION


// DEMO SCALE:
// 1 cm measured by HC-SR04 = 1 meter simulated
const float SCALE = 1.0;

// Maximum valid HC-SR04 distance for this prototype
const float MAX_SENSOR_CM = 400.0;
const float MIN_SENSOR_CM = 2.0;

// Speed below this value is treated as "not approaching"
const float SPEED_DEADBAND = 0.15;   // m/s

// TTC thresholds
const float SAFE_TTC    = 5.0;       // > 5 sec = SAFE
const float CAUTION_TTC = 2.0;       // 2-5 sec = CAUTION
                                      // < 2 sec = DANGER

// If an object is extremely close, treat it as danger
const float EMERGENCY_DISTANCE = 5.0; // simulated meters


//                  FILTER SETTINGS


// Exponential moving average
// Higher = faster response, lower = smoother

const float FILTER_ALPHA = 0.55;

float filteredDistance = -1;
float previousDistance = -1;

unsigned long previousTime = 0;


//                     RISK STATES


enum RiskLevel
{
  SAFE,
  CAUTION,
  DANGER,
  NO_OBJECT
};


//            READ HC-SR04 DISTANCE


float readUltrasonicCM()
{
  // Trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  // Wait for echo
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  // No echo received
  if (duration == 0)
  {
    return -1;
  }

  // Speed of sound ~= 0.0343 cm/us
  float distance = (duration * 0.0343) / 2.0;

  // Reject impossible readings
  if (distance < MIN_SENSOR_CM || distance > MAX_SENSOR_CM)
  {
    return -1;
  }

  return distance;
}


//               FILTER DISTANCE


float filterDistance(float newDistance)
{
  if (filteredDistance < 0)
  {
    filteredDistance = newDistance;
  }
  else
  {
    filteredDistance =
      FILTER_ALPHA * newDistance +
      (1.0 - FILTER_ALPHA) * filteredDistance;
  }

  return filteredDistance;
}


//             CALCULATE CLOSING SPEED


float calculateClosingSpeed(float currentDistance,
                            float oldDistance,
                            float deltaTime)
{
  if (oldDistance < 0 || deltaTime <= 0)
  {
    return 0;
  }

  /*
     Demo scale:
     1 cm sensor = 1 meter simulated

     Example:
     Previous = 80 cm  -> 80 m
     Current  = 75 cm  -> 75 m

     Distance reduced by 5 cm
     In demo = 5 meters

     speed = 5 m / time
  */

  float distanceChange =
    (oldDistance - currentDistance) * SCALE;

  float closingSpeed =
    distanceChange / deltaTime;

  // Negative means object is moving AWAY
  if (closingSpeed < SPEED_DEADBAND)
  {
    return 0;
  }

  return closingSpeed;
}


//                 CALCULATE TTC


float calculateTTC(float distanceMeters,
                   float closingSpeed)
{
  if (closingSpeed <= 0)
  {
    return -1;
  }

  return distanceMeters / closingSpeed;
}


//                 DETERMINE RISK


RiskLevel determineRisk(float distanceMeters,
                        float closingSpeed,
                        float ttc)
{
  // No approaching object
  if (closingSpeed <= 0)
  {
    // Still warn if extremely close
    if (distanceMeters <= EMERGENCY_DISTANCE)
    {
      return DANGER;
    }

    return SAFE;
  }

  // Very close object
  if (distanceMeters <= EMERGENCY_DISTANCE)
  {
    return DANGER;
  }

  // TTC based decision
  if (ttc < CAUTION_TTC)
  {
    return DANGER;
  }

  if (ttc < SAFE_TTC)
  {
    return CAUTION;
  }

  return SAFE;
}


//                 CONTROL LEDs


void updateLEDs(RiskLevel risk)
{
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);

  switch (risk)
  {
    case SAFE:
      digitalWrite(GREEN_LED, HIGH);
      break;

    case CAUTION:
      digitalWrite(YELLOW_LED, HIGH);
      break;

    case DANGER:
      digitalWrite(RED_LED, HIGH);
      break;

    case NO_OBJECT:
      digitalWrite(GREEN_LED, HIGH);
      break;
  }
}


//                   OLED DISPLAY


void showOLED(float distanceMeters,
              float closingSpeed,
              float ttc,
              RiskLevel risk)
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // -------- TITLE --------
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("PROXIMINE");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // -------- DISTANCE --------
  display.setTextSize(1);
  display.setCursor(0, 14);

  if (distanceMeters < 0)
  {
    display.println("DIST: --");
  }
  else
  {
    display.print("DIST: ");
    display.print(distanceMeters, 1);
    display.println(" m");
  }

  // -------- SPEED --------
  display.setCursor(0, 27);

  display.print("REL SPD: ");
  display.print(closingSpeed, 1);
  display.println(" m/s");

  // -------- TTC --------
  display.setCursor(0, 40);

  display.print("TTC: ");

  if (ttc < 0)
  {
    display.println("--");
  }
  else
  {
    display.print(ttc, 1);
    display.println(" s");
  }

  // -------- STATUS --------
  display.setTextSize(1);
  display.setCursor(0, 54);

  switch (risk)
  {
    case SAFE:
      display.print("STATUS: SAFE");
      break;

    case CAUTION:
      display.print("STATUS: CAUTION");
      break;

    case DANGER:
      display.print("STATUS: DANGER!");
      break;

    case NO_OBJECT:
      display.print("STATUS: SAFE");
      break;
  }

  display.display();
}


//                    SERIAL OUTPUT


void printSerial(float distance,
                 float speed,
                 float ttc,
                 RiskLevel risk)
{
  Serial.println("------------------------------");

  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" m");

  Serial.print("Relative speed: ");
  Serial.print(speed);
  Serial.println(" m/s");

  Serial.print("TTC: ");

  if (ttc < 0)
  {
    Serial.println("N/A");
  }
  else
  {
    Serial.print(ttc);
    Serial.println(" sec");
  }

  Serial.print("Risk: ");

  switch (risk)
  {
    case SAFE:
      Serial.println("SAFE");
      break;

    case CAUTION:
      Serial.println("CAUTION");
      break;

    case DANGER:
      Serial.println("DANGER");
      break;

    case NO_OBJECT:
      Serial.println("NO OBJECT");
      break;
  }
}


//                      SETUP


void setup()
{
  Serial.begin(115200);

  // Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // LEDs
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);

  // OLED - ESP32 supports custom SDA/SCL pins via Wire.begin(sda, scl)

  
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS))
  {
    Serial.println("OLED initialization failed!");

    while (true)
    {
      // Stop here
    }
  }

  // Startup screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(10, 15);
  display.println("PROXIMINE");

  display.setTextSize(1);
  display.setCursor(20, 40);
  display.println("SYSTEM READY");

  display.display();

  delay(2000);

  previousTime = millis();
}


//                       LOOP


void loop()
{
  // Read sensor
  float rawDistance = readUltrasonicCM();

 
  // NO VALID OBJECT
 

  if (rawDistance < 0)
  {
    updateLEDs(NO_OBJECT);

    showOLED(-1, 0, -1, NO_OBJECT);

    Serial.println("No valid object detected.");

    delay(100);
    return;
  }

 
  // FILTER DISTANCE
 

  float currentDistance = filterDistance(rawDistance);

 
  // CALCULATE TIME
 

  unsigned long currentTime = millis();

  float deltaTime =
    (currentTime - previousTime) / 1000.0;

  // Prevent division errors
  if (deltaTime <= 0)
  {
    deltaTime = 0.1;
  }

  previousTime = currentTime;

 
  // CALCULATE RELATIVE CLOSING SPEED
 

  float closingSpeed =
    calculateClosingSpeed(
      currentDistance,
      previousDistance,
      deltaTime
    );

  // Save distance for next cycle
  previousDistance = currentDistance;

 
  // CONVERT TO DEMO METERS


  float distanceMeters =
    currentDistance * SCALE;

  // CALCULATE TTC
  

  float ttc =
    calculateTTC(
      distanceMeters,
      closingSpeed
    );

  // DETERMINE RISK
 

  RiskLevel risk =
    determineRisk(
      distanceMeters,
      closingSpeed,
      ttc
    );

// UPDATE OUTPUTS
  updateLEDs(risk);

  showOLED(
    distanceMeters,
    closingSpeed,
    ttc,
    risk
  );

  printSerial(
    distanceMeters,
    closingSpeed,
    ttc,
    risk
  );

  delay(100);
}
