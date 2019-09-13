#include <AccelStepper.h>
#include <Stepper.h>

#define HALFSTEP 8

// Motor pin definitions
// Arduino Mega
//#define motorPin1  2     // IN1 on the ULN2003 driver 1
//#define motorPin2  3     // IN2 on the ULN2003 driver 1
//#define motorPin3  4     // IN3 on the ULN2003 driver 1
//#define motorPin4  5     // IN4 on the ULN2003 driver 1
// ESP8266 
#define motorPin1  D5     // IN1 on the ULN2003 driver 1
#define motorPin2  D6     // IN2 on the ULN2003 driver 1
#define motorPin3  D7     // IN3 on the ULN2003 driver 1
#define motorPin4  D8     // IN4 on the ULN2003 driver 1

//#define ONE_TWENTY 1360
#define ONE_TWENTY 133
#define TICKS_TO_DEG(x) ((x) / 11)

// 150 for max speed seems to be a good speed without causing jams. 
int MAX_SPEED = 400; // Top speed. Used for the "move to" command.
int MAX_ACCEL = 1500; // Max acceleration. Used with the "move to" command. 
int SPEED = 100; // Speed used with the "move constant speed" command. Not used with "move to". 

// Initialize with pin sequence IN1-IN3-IN2-IN4 for using the AccelStepper with 28BYJ-48
//AccelStepper stepper1(HALFSTEP, motorPin1, motorPin3, motorPin2, motorPin4, false);
AccelStepper stepper1(HALFSTEP, motorPin1, motorPin2, motorPin3, motorPin4, false);
// Order: 1, 3, 2, 4
// Connection: Yellow A +, blue A-, green B +, Red B- 

int stepsPerRevolution = 200; 
// initialize the stepper library on pins 8 through 11:
Stepper stepper2(stepsPerRevolution, motorPin1, motorPin2, motorPin3, motorPin4);

unsigned long lastdb = 0, dbDelay = 50; 
int btn = D1;
int btnState = HIGH, lastState = HIGH; 

int stepperToUse = 1; 

void setup() {
  pinMode(btn, INPUT_PULLUP);

  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
  digitalWrite(motorPin3, LOW);
  digitalWrite(motorPin4, LOW);

  Serial.begin(9600);
  
  stepper1.setMaxSpeed(MAX_SPEED);
  stepper1.setAcceleration(MAX_ACCEL);
  //stepper1.setSpeed(520);
  //stepper1.setAcceleration(500); 
  //stepper1.setSpeed(200);
  //stepper1.moveTo(40000);

  Serial.println("Setup complete");
  Serial.println("Send 'g' to increment stepper: "); 

}//--(end setup )---

void loop() {

  int val = digitalRead(btn);
  
  char c = 0;

  if(Serial.available() > 0) {
    c = Serial.read();

    switch(c)
    {
      // GO FORWARD
      case 'g':
        Serial.print("Moving forward to ");
        Serial.println(TICKS_TO_DEG(stepper1.targetPosition() + ONE_TWENTY));
        moveNumThirds(1);
        break;
      case 'r': 
        Serial.print("Moving reverse to: " ); 
        Serial.println(TICKS_TO_DEG(stepper1.targetPosition() - ONE_TWENTY));
        moveNumThirds(-1); 
        break;
      // STOP
      case 's': 
        Serial.println("Stopping...");
        stepper1.stop(); 
        stepper1.disableOutputs(); 
        break;
        
      case '^':
        SPEED += 10;
        stepper1.setSpeed(SPEED); 
        Serial.print("Increasing speed to: "); 
        Serial.println(SPEED); 
        //movePlus120(); 
        break;

      case 'v': 
        SPEED -= 10;
        stepper1.setSpeed(SPEED); 
        Serial.print("Decreasing speed to: "); 
        Serial.println(SPEED); 
        //movePlus120(); 
        break;

      case 'm': 
        MAX_SPEED -= 10;
        stepper1.setMaxSpeed(MAX_SPEED); 
        Serial.print("Decreasing max speed to: "); 
        Serial.println(MAX_SPEED); 
        //movePlus120(); 
        break;

      case 'M': 
        MAX_SPEED += 10;
        stepper1.setMaxSpeed(MAX_SPEED); 
        Serial.print("Increasing max speed to: "); 
        Serial.println(MAX_SPEED); 
        //movePlus120(); 
        break;
      case 'a': 
        MAX_ACCEL -= 10;
        stepper1.setAcceleration(MAX_ACCEL); 
        Serial.print("Decreasing accel to: "); 
        Serial.println(MAX_ACCEL); 
        //movePlus120(); 
        break;

      case 'A': 
        MAX_ACCEL += 10;
        stepper1.setAcceleration(MAX_ACCEL); 
        Serial.print("Increasing accel to: "); 
        Serial.println(MAX_ACCEL); 
        //movePlus120(); 
        break;

      case '1': 
        Serial.println("Using AccelStepper"); 
        stepperToUse = 1; 
        break; 
      case '2': 
        Serial.println("Using basic Stepper.h"); 
        stepperToUse = 2; 
        break;
    }
  }
  
  if(val != lastState)
  {
    lastdb = millis(); 
//    Serial.print("State changed; millis: ");
//    Serial.println(lastdb);
  }

  if((millis() - lastdb) > dbDelay)
  {
    if(val != btnState){
      btnState = val; 

      //Serial.println(btnState);

      if(btnState == LOW) {
        Serial.println("Pressed");
        if(stepperToUse == 1)
          stepper1.enableOutputs();
        else
        {
        }
        movePlus120();
      }
    }
  }

  lastState = val;

  if(stepperToUse == 1)
  {
    if(stepper1.distanceToGo() == 0) 
    {
      //Serial.println("stopping");
      stepper1.stop();
      stepper1.disableOutputs();
    }
    else{
      stepper1.run();
    }
  }
  else
  {
  
  }

  
  //Change direction when the stepper reaches the target position
//  if (stepper1.distanceToGo() == 0) {
//    stepper1.moveTo(-stepper1.currentPosition());
//  }
//  stepper1.run();
}

void movePlus120() {
  if(stepperToUse == 1)
  {
    stepper1.enableOutputs(); 
    stepper1.moveTo(stepper1.targetPosition() + ONE_TWENTY);
  }
}

void moveNumThirds(int num){
  if(stepperToUse == 1)
  {
    stepper1.enableOutputs(); 
    stepper1.moveTo(stepper1.targetPosition() + (num * ONE_TWENTY)); 
  }
  else
  {

  }
  //stepper1.setSpeed(SPEED);
}
