#include <FastLED.h>
#include <EEPROM.h>

// Storage Address to persist the mode of the system
#define WORD_ADDRESS 0

// Configuration of the Distance sensors
#define TRIGPIN_1 12
#define ECHOPIN_1 13
#define TRIGPIN_2 10
#define ECHOPIN_2 11

#define SPEED_OF_SOUND 0.343
#define DISTANCE_FAR 1000
#define DISTANCE_CLOSE 250


// Configuration of the Prototype Board LED's and Button
#define INTERNAL_BUTTON_PIN 3
#define INTERNAL_LED1_PIN 5
#define INTERNAL_LED2_PIN 6

// Configuration of the LED Strip
#define LED_DATA_PIN 7
#define NUM_LEDS 16

// Configuration of the Audio Output
#define AUDIO_TONE 200

// List of Modes available
const int MODE_AUDIO_VISUAL = 0;
const int MODE_VISUAL_ONLY = 1;
const int MODE_AUDIO_ONLY = 2;
const int MODE_INTERNAL_ONLY = 3;

// The final distance struct containing all relevant data
struct distance {
  float distance_left;
  float distance_right;
  float approximation_left; // 0.0 is closest
  float approximation_right; // ..0 is closest
  float approximation; // 0.0 is closest
  float distance;
  bool valid;
};                         


// global variables
CRGB leds[NUM_LEDS];
int button_state;
int mode;


void setup() {
  Serial.begin(115200);

  FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed
  FastLED.setBrightness(10);

  // setup distance sensors
  pinMode(ECHOPIN_1, INPUT);
  pinMode(TRIGPIN_1, OUTPUT);
  pinMode(ECHOPIN_2, INPUT);
  pinMode(TRIGPIN_2, OUTPUT);

  // setup prototype board leds and button
  pinMode(INTERNAL_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INTERNAL_BUTTON_PIN), manage_mode, RISING);
  pinMode(INTERNAL_LED1_PIN, OUTPUT);
  pinMode(INTERNAL_LED2_PIN, OUTPUT);

  // program startup
  mode = EEPROM.read(WORD_ADDRESS);
  Serial.print("Loaded Mode from EEPROM: ");
  Serial.println(mode);
  visual_startup();
}

void audio_distance(distance &d, int audio_tone, int beep_duration=100) {
  // fucking buzzer does not work
}

void adaptive_delay(distance &d) {
  // delay loop based on distance, as closer the vehicle get the more often we read the distance.
  int long_delay = 2000;
  int short_delay = 200;
  if (d.approximation >= 1.0) {
    delay(long_delay);
  } else {
    int dynamic_delay = ((long_delay - short_delay) / 1.0 * d.approximation)+short_delay;
    delay(dynamic_delay);
  }
}

void visual_startup() {
  // just some fancy blinking
  int blink_delay = 50;
  for (int l = 0; l < NUM_LEDS; ++l) {
    leds[l] = CRGB::Blue;
    FastLED.show();
    delay(blink_delay);
    leds[l] = CRGB::Black;
  }
  for (int l = NUM_LEDS; l > 0; --l) {
    leds[l] = CRGB::Green;
    FastLED.show();
    delay(blink_delay);
    leds[l] = CRGB::Black;
  }
  FastLED.show();
}

void visual_distance(distance &d, CRGB color, bool internal_led = false, bool external_led = true) {
  if(internal_led) {
    if(d.approximation_right <= 0.0) {
      digitalWrite(INTERNAL_LED1_PIN, HIGH);
    } else {
      digitalWrite(INTERNAL_LED1_PIN, LOW);
    }
    
    if(d.approximation_left <= 0.0) {
      digitalWrite(INTERNAL_LED2_PIN, HIGH);
    } else {
      digitalWrite(INTERNAL_LED2_PIN, LOW);
    }
  }

  if(!external_led)
    return;

  // calculate the visual distance representation
  int left_leds = NUM_LEDS / 2;
  int right_leds = NUM_LEDS / 2;

  int left_active_led = (int)((float)left_leds / 1.0 * (1.0-d.approximation_left));
  int right_active_led = (int)((float)right_leds / 1.0 * (1.0-d.approximation_right));

  // set LED's for left
  for (int l = 0; l < left_leds; ++l) {
    if (l < left_active_led) {
        leds[l] = color;
    } else {
        leds[l] = CRGB::Black;
    }
  }

  // set LED's for right
  for (int r = 0; r < right_leds; ++r) {
    if (r < right_active_led) {
        leds[NUM_LEDS-r-1] = color; 
    } else {
        leds[NUM_LEDS-r-1] = CRGB::Black;
    }
  }

  // blink the LED when close  
  int led_from;
  int led_to;
  if (left_active_led == left_leds and right_active_led == right_leds) {
    led_from = 0;
    led_to = NUM_LEDS;
  } else if(left_active_led == left_leds){
    led_from = 0;
    led_to = left_leds;
  } else if(right_active_led == right_leds){
    led_from = right_leds;
    led_to = NUM_LEDS;
  } else {
    led_from = 0;
    led_to = 0;
  }

  if (led_from or led_to) {
    for (int x = led_from; x < led_to; ++x) {
      leds[x] = CRGB::Black;
    }
    FastLED.show();
    delay(100);
    for (int x = led_from; x < led_to; ++x) {
      leds[x] = color;
    }
  }
  
  FastLED.show();

}


float measure_distance(int echo_pin, int trigger_pin) {
  // actually measures the distance
  digitalWrite(trigger_pin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigger_pin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigger_pin, LOW);
  delayMicroseconds(20);

  float duration = pulseIn(echo_pin, HIGH);
  return (duration / 2) * SPEED_OF_SOUND;
}

distance get_distance(int echo_pin1, int trigger_pin1, int echo_pin2=0, int trigger_pin2=0) {
  // here happens all the magic to build the distance struct for any further processing
  distance d;

  // Get distances
  d.distance_left = measure_distance(echo_pin1, trigger_pin1);
  if (echo_pin2 and trigger_pin2) {
    d.distance_right = measure_distance(echo_pin2, trigger_pin2);
  } else {
    d.distance_right = d.distance_left;
  }

  // verify if the result is valid or not
  if (d.distance_left >= 6820.0 and d.distance_right >= 6820.0) {
    d.valid = false;
  } else {
    d.valid = true;
  }
  
  // CALCULATE LEFT APPROXIMATION
  if (d.distance_left >= DISTANCE_FAR) {
    d.approximation_left = 1.0;
  } else if (d.distance_left <= DISTANCE_CLOSE) {
    d.approximation_left = 0.0;
  } else {
    d.approximation_left = 1.0/(DISTANCE_FAR - DISTANCE_CLOSE)*(d.distance_left - DISTANCE_CLOSE);
  }

  // CALCULATE RIGHT APPROXIMATION
  if (d.distance_right >= DISTANCE_FAR) {
    d.approximation_right = 1.0;
  } else if (d.distance_right <= DISTANCE_CLOSE) {
    d.approximation_right = 0.0;
  } else {
    d.approximation_right = 1.0/(DISTANCE_FAR - DISTANCE_CLOSE)*(d.distance_right - DISTANCE_CLOSE);
  }

  // Calcualte average distance and approximation
  d.approximation = (d.approximation_left + d.approximation_right) / 2;
  d.distance = (d.distance_left + d.distance_right) / 2;
  return d;
}

void debug_distance(distance &d) {
  Serial.print("Mode: ");
  Serial.print(mode);
  Serial.print(" ");

  Serial.print("distance left: ");
  Serial.print(d.distance_left);
  Serial.print("mm ");

  Serial.print("distance right: ");
  Serial.print(d.distance_right);
  Serial.print("mm ");
  
  Serial.print("approximation left: ");
  Serial.print(d.approximation_left);

  Serial.print(" approximation right: ");
  Serial.print(d.approximation_right);

  Serial.print(" distance: ");
  Serial.print(d.distance);
  Serial.print("mm");

  Serial.print(" approximation: ");
  Serial.println(d.approximation);
}


void manage_mode() {
  // interrupt when mode button has been pressed. Changes the mode of the system.
  ++mode;
  if(mode > MODE_INTERNAL_ONLY or mode < 0)
    mode = 0;

  EEPROM.write(WORD_ADDRESS, mode);
  Serial.print("Systems mode set to: ");
  Serial.println(mode);

  switch(mode) {
      case MODE_AUDIO_ONLY:
        FastLED.clear();
        digitalWrite(INTERNAL_LED1_PIN, LOW);
        digitalWrite(INTERNAL_LED2_PIN, LOW);
        break;
      case MODE_INTERNAL_ONLY:
        FastLED.clear();
        break;
      default:
        break;
  }
}

void loop() {
  distance d = get_distance(ECHOPIN_1, TRIGPIN_1, ECHOPIN_2, TRIGPIN_2);
  
  if (d.valid) {
    debug_distance(d);
    
    switch(mode) {
      case MODE_AUDIO_VISUAL:
        visual_distance(d, CRGB::Red, true, true);
        audio_distance(d, AUDIO_TONE);
        break;
      case MODE_VISUAL_ONLY:
        visual_distance(d, CRGB::Red, true, true);
        break;
      case MODE_AUDIO_ONLY:
        audio_distance(d, AUDIO_TONE);
        break;
      default:
        visual_distance(d, CRGB::Red, true, false);
        break;
    }
    adaptive_delay(d);
  } else {
    delay(100);
  }
}
