#define BLYNK_USE_DIRECT_CONNECT

// Imports
#include <Wire.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <BlynkSimpleSerialBLE.h>
#include "./TinyGPS.h"                 // Use local version of this library
#include "./CoolerDefinitions.h"
#include <DFRobot_QMC5883.h>

// GPS
TinyGPS gps;

// Master Enable
bool enabled = false;

//WidgetTerminal terminal(V3);

// Serial components
// SoftwareSerial bluetoothSerial(BLUETOOTH_TX_PIN, BLUETOOTH_RX_PIN); [Using 18(RX) and 19(TX) pin, serial1.--- instead of bluetoothSerial.---]
// SoftwareSerial nss(GPS_TX_PIN, 255);            // TXD to digital pin 6 [Using 16(RX) and 17(Tx)pin, serial2.--- instead of nss.---]

/* Compass */
DFRobot_QMC5883 compass;

GeoLoc checkGPS() {
  Serial.println("Reading onboard GPS: ");
  bool newdata = false;
  unsigned long start = millis();
  while (millis() - start < GPS_UPDATE_INTERVAL) {
    if (feedgps())
      newdata = true;
  }
  if (newdata) {
    return gpsdump(gps);
  }

  GeoLoc coolerLoc;
  coolerLoc.lat = 0.0;
  coolerLoc.lon = 0.0;
  
  return coolerLoc;
}

// Get and process GPS data
GeoLoc gpsdump(TinyGPS &gps) {
  float flat, flon;
  unsigned long age;
  
  gps.f_get_position(&flat, &flon, &age);

  GeoLoc coolerLoc;
  coolerLoc.lat = flat;
  coolerLoc.lon = flon;

  Serial.print(coolerLoc.lat, 7); Serial.print(", "); Serial.println(coolerLoc.lon, 7);

  return coolerLoc;
}

// Feed data as it becomes available 
bool feedgps() {
  while (serial2.available()) {
    if (gps.encode(serial2.read()))
      return true;
  }
  return false;
}

// Killswitch Hook
BLYNK_WRITE(V1) {
  enabled = !enabled;
  
  //Stop the wheels
  stop();
}

// GPS Streaming Hook
BLYNK_WRITE(V2) {
  GpsParam gps(param);
  
  Serial.println("Received remote GPS: ");
  
  // Print 7 decimal places for Lat
  Serial.print(gps.getLat(), 7); Serial.print(", "); Serial.println(gps.getLon(), 7);

  GeoLoc phoneLoc;
  phoneLoc.lat = gps.getLat();
  phoneLoc.lon = gps.getLon();

  driveTo(phoneLoc, GPS_STREAM_TIMEOUT);
}

// Terminal Hook
BLYNK_WRITE(V3) {
  Serial.print("Received Text: ");
  Serial.println(param.asStr());

  String rawInput(param.asStr());
  int colonIndex;
  int commaIndex;
  
  do {
    commaIndex = rawInput.indexOf(',');
    colonIndex = rawInput.indexOf(':');
    
    if (commaIndex != -1) {
      String latStr = rawInput.substring(0, commaIndex);
      String lonStr = rawInput.substring(commaIndex+1);

      if (colonIndex != -1) {
         lonStr = rawInput.substring(commaIndex+1, colonIndex);
      }
    
      float lat = latStr.toFloat();
      float lon = lonStr.toFloat();
    
      if (lat != 0 && lon != 0) {
        GeoLoc waypoint;
        waypoint.lat = lat;
        waypoint.lon = lon;
    
        Serial.print("Waypoint found: "); Serial.print(lat); Serial.println(lon);
        driveTo(waypoint, GPS_WAYPOINT_TIMEOUT);
      }
    }
    
    rawInput = rawInput.substring(colonIndex + 1);
    
  } while (colonIndex != -1);
}

#ifndef DEGTORAD
#define DEGTORAD 0.0174532925199432957f
#define RADTODEG 57.295779513082320876f
#endif

float geoBearing(struct GeoLoc &a, struct GeoLoc &b) {
  float y = sin(b.lon-a.lon) * cos(b.lat);
  float x = cos(a.lat)*sin(b.lat) - sin(a.lat)*cos(b.lat)*cos(b.lon-a.lon);
  return atan2(y, x) * RADTODEG;
}

float geoDistance(struct GeoLoc &a, struct GeoLoc &b) {
  const float R = 6371000; // km
  float p1 = a.lat * DEGTORAD;
  float p2 = b.lat * DEGTORAD;
  float dp = (b.lat-a.lat) * DEGTORAD;
  float dl = (b.lon-a.lon) * DEGTORAD;

  float x = sin(dp/2) * sin(dp/2) + cos(p1) * cos(p2) * sin(dl/2) * sin(dl/2);
  float y = 2 * atan2(sqrt(x), sqrt(1-x));

  return R * y;
}

void setSpeedMotorA(int speed) {
  digitalWrite(MOTOR_A_IN_1_PIN, LOW);
  digitalWrite(MOTOR_A_IN_2_PIN, HIGH);
  
  // set speed to 200 out of possible range 0~255
  analogWrite(MOTOR_A_EN_PIN, speed + MOTOR_A_OFFSET);
}

void setSpeedMotorB(int speed) {
  digitalWrite(MOTOR_B_IN_1_PIN, LOW);
  digitalWrite(MOTOR_B_IN_2_PIN, HIGH);
  
  // set speed to 200 out of possible range 0~255
  analogWrite(MOTOR_B_EN_PIN, speed + MOTOR_B_OFFSET);
}

void setSpeed(int speed)
{
  // this function will run the motors in both directions at a fixed speed
  // turn on motor A
  setSpeedMotorA(speed);

  // turn on motor B
  setSpeedMotorB(speed);
}

void stop() {
  // now turn off motors
  digitalWrite(MOTOR_A_IN_1_PIN, LOW);
  digitalWrite(MOTOR_A_IN_2_PIN, LOW);  
  digitalWrite(MOTOR_B_IN_1_PIN, LOW);
  digitalWrite(MOTOR_B_IN_2_PIN, LOW);
}

void drive(int distance, float turn) {
  int fullSpeed = 230;
  int stopSpeed = 0;

  // drive to location
  int s = fullSpeed;
  if ( distance < 8 ) {
    int wouldBeSpeed = s - stopSpeed;
    wouldBeSpeed *= distance / 8.0f;
    s = stopSpeed + wouldBeSpeed;
  }
  
  int autoThrottle = constrain(s, stopSpeed, fullSpeed);
  autoThrottle = 230;

  float t = turn;
  while (t < -180) t += 360;
  while (t >  180) t -= 360;
  
  Serial.print("turn: ");
  Serial.println(t);
  Serial.print("original: ");
  Serial.println(turn);
  
  float t_modifier = (180.0 - abs(t)) / 180.0;
  float autoSteerA = 1;
  float autoSteerB = 1;

  if (t < 0) {
    autoSteerB = t_modifier;
  } else if (t > 0){
    autoSteerA = t_modifier;
  }

  Serial.print("steerA: "); Serial.println(autoSteerA);
  Serial.print("steerB: "); Serial.println(autoSteerB);

  int speedA = (int) (((float) autoThrottle) * autoSteerA);
  int speedB = (int) (((float) autoThrottle) * autoSteerB);
  
  setSpeedMotorA(speedA);
  setSpeedMotorB(speedB);
}

void driveTo(struct GeoLoc &loc, int timeout) {
  serial.listen();
  GeoLoc coolerLoc = checkGPS();
  serial1.listen();

  if (coolerLoc.lat != 0 && coolerLoc.lon != 0 && enabled) {
    float d = 0;
    //Start move loop here
    do {
      serial2.listen();
      coolerLoc = checkGPS();
      serial1.listen();
      
      d = geoDistance(coolerLoc, loc);
      float t = geoBearing(coolerLoc, loc) - geoHeading();
      
      Serial.print("Distance: ");
      Serial.println(geoDistance(coolerLoc, loc));
    
      Serial.print("Bearing: ");
      Serial.println(geoBearing(coolerLoc, loc));

      Serial.print("heading: ");
      Serial.println("");
      
      drive(d, t);
      timeout -= 1;
    } while (d > 3.0 && enabled && timeout>0);

    stop();
  }
}

void setupCompass() {
   /* Initialise the compass */
  while(!compass.begin())
  {
    /* There was a problem detecting the HMC5883 ... check your connections */
    Serial.println("Ooops, no QMC5883 detected ... Check your wiring!");
    delay(500);
  }
  if(compass.isHMC() ){
    Serial.println("Initialize HMC5883");
    //compass.setRange(HMC5883L_RANGE_1_3GA);
    //compass.setMeasurementMode(HMC5883L_CONTINOUS);
    //compass.setDataRate(HMC5883L_DATARATE_15HZ);
    //compass.setSamples(HMC5883L_SAMPLES_8);
  }else if(compass.isQMC()){
    Serial.println("Initialize QMC5883");
    //compass.setRange(QMC5883_RANGE_2GA);
    //compass.setMeasurementMode(QMC5883_CONTINOUS); 
    //compass.setDataRate(QMC5883_DATARATE_50HZ);
    //compass.setSamples(QMC5883_SAMPLES_8);
  }else if(compass.isVCM()){
    Serial.println("Initialize VCM5883L");
    //compass.setMeasurementMode(VCM5883L_CONTINOUS); 
    //compass.setDataRate(VCM5883L_DATARATE_200HZ);
  }
  delay(1000);
}

void setup()
{
  // Compass
  setupCompass();

  // Motor pins
  pinMode(MOTOR_A_EN_PIN, OUTPUT);
  pinMode(MOTOR_B_EN_PIN, OUTPUT);
  pinMode(MOTOR_A_IN_1_PIN, OUTPUT);
  pinMode(MOTOR_A_IN_2_PIN, OUTPUT);
  pinMode(MOTOR_B_IN_1_PIN, OUTPUT);
  pinMode(MOTOR_B_IN_2_PIN, OUTPUT);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  //Debugging via serial
  Serial.begin(4800);

  //GPS
  serial2.begin(9600);

  //Bluetooth
  serial1.begin(9600);
  Blynk.begin(serial1, auth);
}

  float geoHeading()
{
  // Set declination angle on your location and fix heading
  // You can find your declination on: http://magnetic-declination.com/
  // (+) Positive or (-) for negative
  // For Chandigarh/ India declination angle is 1'49E (positive)
  // Formula: (deg + (min / 60.0)) / (180 / PI);
  float declinationAngle = (1.0 + (49.0 / 60.0)) / (180 / PI);
  compass.setDeclinationAngle(declinationAngle);
  Vector mag = compass.readRaw();
  compass.getHeadingDegrees();
  Serial.print("X:");
  Serial.print(mag.XAxis);
  Serial.print(" Y:");
  Serial.print(mag.YAxis);
  Serial.print(" Z:");
  Serial.println(mag.ZAxis);
  Serial.print("Degress = ");
  Serial.println(mag.HeadingDegress);
  delay(100);
  return mag.HeadingDegress;
}

void testDriveNorth() {
  float heading = geoHeading();
  int testDist = 10;
  Serial.println(heading);
  
  while(!(heading < 5 && heading > -5)) {
    drive(testDist, heading);
    heading = geoHeading();
    Serial.println(heading);
    delay(500);
  }
  
  stop();
}

void loop()
{
  Blynk.run();
  geoHeading();
}
