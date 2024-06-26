/*
 * These parameters are things you can change
 */

// options for the CSV output to serial
bool include_time_in_data = false;

// define some limits for the motor 
// the motor output is a PWM value between 0 and 255, this parameter sets a new upper limit in case it is spinning too fast
const int minimum_control_action = 0;
const int maximum_control_action = 250;

// How fast the motor should spin when the program starts
const int initialMotorSetpoint = 20;

// Some parameters for the controller
// Integrator windup protection - stops the accumulation of error from increasing too much
float min_accumulated_error = 0; // Minimum allowed value for the integral term
float max_accumulated_error = 500;  // Maximum allowed value for the integral term



// BANG-BANG controller
// this function runs every 50ms (operating at 20Hz)
int CalculateControlAction(int &currentCount, float &setpoint, float &error_sum) {

  // bang-bang controller
  if (currentCount < setpoint)
    return maximum_control_action;
  else
    return minimum_control_action;
}



/*
 * These variables do the work for the controller
 */

// define the pins of the Arduino that are used for different tasks
const int encoderInPin = 8;  // we read the encoder state (the output of the op amp comparator) on this pin
const int motorPWMPin = 6; // we output the desired speed of the motor (as PWM) through this pin

/*
 * You do not need to change anything below this line
 */


// define some variables that hold the current values 
volatile int encoderCurrentValue = 0;
volatile unsigned int encoderCounts = 0;
volatile bool lastStateHigh = false;
volatile unsigned int latestReading = 0;

// Variables for the controller state
float input = 0, output = 0, setpoint = 0;
float accumulated_error = 0;


// Timer 1 is configured to read at 10000Hz (10kHz)
// it is important that this code does not take long to execute as
// otherwise the next timer would expire before the previous one finishes
// This function is called an Interrupt Service Routine (ISR)
ISR(TIMER1_COMPA_vect) {

  encoderCurrentValue = digitalRead(encoderInPin); // Read the encoder current state

  // the following code checks whether the encoder current state has changed
  // since the previous time we checked, and if so increment the count
  if (lastStateHigh && encoderCurrentValue == 0) {
    encoderCounts++;
    lastStateHigh = false;
  } else if (!lastStateHigh && encoderCurrentValue == 1) {
    lastStateHigh = true;
    encoderCounts++;
  }
}


void setup() {
  Serial.begin(1000000);  // open up the serial port at 115200 baud rate

  pinMode(encoderInPin, INPUT); // read from the encoder input

  // Initialise the setpoint, this is the desired speed for the motor on startup
  setpoint = initialMotorSetpoint;

  // initialise the timers, once this is executed the motor controller will begin
  setupTimer1();
  setupTimer2();
}


// Inside the loop, the program reads from the serial port, allowing you to type a new set point from the serial monitor/plotter
void loop() {

  // Use the following code if you want to read the setpoint from analog input A0
  setpoint = map(analogRead(A0), 0, 1023, 0, 50);
  delay(250);

  // use the following code if you want to read the setpoint from the serial port
  /*
  // Check if there is new serial data available
  if (Serial.available() > 0) { 
    String inputString = Serial.readStringUntil('\n'); // Read the incoming data until a newline character is encountered
    int parsedInt = inputString.toInt(); // Convert the string to an integer

    // Ensure the parsed integer is within the desired range (0-50)
    if (parsedInt >= 0 && parsedInt <= 90) {
      // change the setpoint of the controller to the value received from the serial port
      setpoint = parsedInt;
    }
  }
  */
}


// timer is at 100hz (cannot be 20hz), so need to divide by 5 to get 20hz
unsigned int divide_frequency_counter = 0;

ISR(TIMER2_COMPA_vect) {

  if (divide_frequency_counter >= 5)
  {
    divide_frequency_counter = 0;

    /* 
     *  This is where the control is done
     */

    int currentCount = encoderCounts;
    encoderCounts = 0;  // need to reset the count so that the count begins for the next period

    // perform this operation at 10Hz
    int control_action = CalculateControlAction(currentCount, setpoint, accumulated_error);

    // Limit the output to the PWM range (don't want it to go max speed)
    if (control_action < minimum_control_action)
      control_action = minimum_control_action;
    else if (control_action > maximum_control_action)
      control_action = maximum_control_action;

    // Write the output to the motor
    analogWrite(motorPWMPin, (int)control_action);
    float timeInSeconds = (float)micros() / (float)1e6;

    // print time if required
    if (include_time_in_data) {
      Serial.print(timeInSeconds, 4);
      Serial.print(",");
    }

    // print the current count from the encoder (in 0.05 seconds)
    Serial.print(currentCount);
    Serial.print(".0");

    // print the current set point
    Serial.print(",");
    Serial.print(setpoint);

    // print the accumulated error
    //Serial.print(",");
    //Serial.print(accumulated_error);

    // the output value is scaled so that it is roughly the same magnitude as the setpoint
    // this is for graphing and visualisation only
    Serial.print(",");
    Serial.println(control_action / 10.0); 
  }

  divide_frequency_counter++;
}


// Configure Timer1 to run at 10 kHz 
// the Service routine is called at this frequency
void setupTimer1() {
 
  cli(); // Disable global interrupts

  // Reset Timer1 control registers
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // Calculate the value for the Output Compare Register (OCR1A)
  // Formula: OCR1A = (F_CPU / (Prescaler * Desired_Frequency)) - 1
  // For 1 kHz: OCR1A = (16,000,000 / (1 * 1,000)) - 1 = 15999

  // currently testing at 10kHz instead
  OCR1A = 1562;

  // Enable Timer1 compare interrupt
  TIMSK1 |= (1 << OCIE1A);

  // Set Timer1 to CTC mode and select a prescaler of 1
  TCCR1B |= (1 << WGM12) | (1 << CS10);

  sei(); // Enable global interrupts
}


// Configure Timer2 to run at 100 Hz 
// the Service routine is called at this frequency
// we want the motor control loop at 10Hz, but this is too slow for the timer
// so in the control loop we execute only 1 in every 10 iterations.
void setupTimer2() {
 noInterrupts();
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;
  OCR2A = 156; // 16MHz / 64 (prescaler) / 1000 (desired frequency) - 1
  TCCR2A |= (1 << WGM21);
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);
  TIMSK2 |= (1 << OCIE2A);
  interrupts();
}
