#define ESP
//#define DEBUG

// ESP-NOW dynamic traffic light simulator
#ifdef ESP
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN     5
#define GERMAN_PIN 14
#define SLOW_PIN   13
#define FAST_PIN   12

uint8_t broadcastMAC[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(3, LED_PIN, NEO_RGB + NEO_KHZ800);
#endif

volatile uint8_t currentIndex = 255;
volatile uint8_t length = 0;

// TODO: cleanup and put in array
const int BLINK_TIME = 650;
const int GREEN_TIME = 4000;
const int AMBER_TIME = 2500;
const int PREPARE_GREEN_TIME = 1500;

// NOTE: take PREPARE_GREEN_TIME into account
const int RED_TIME_SLOW = 15000;
const int RED_TIME = 5000;
const int RED_TIME_FAST = 2000;

const int RELEASE_INTERSECTION_TIME = 500; // Initial time before continuity is checked and adjusted for
const int CHECK_CONTINUITY_TIME = 500;     // Recurring time before continuity is checked and adjusted for
const int WAIT_TIMEOUT_TIME = 30000;       // Make sure it's above the highest pause we have

typedef enum {
  BLINK_ON,
  BLINK_OFF,
  AMBER,
  RED,
  GREEN,
  PREPARE_GREEN,
  RELEASE_INTERSECTION,
  CHECK_CONTINUITY,
  LENGTH,
  WAIT_TIMEOUT,
  REMOVE_INDEX,
} State;

// message format
typedef struct struct_message {
  State state;
  int8_t index;
} Message;

State nextState = BLINK_ON;
long int nextTick = 0;

void printState(State _state, uint8_t _index)
{
#ifdef DEBUG
  switch (_state)
  {
    case BLINK_ON:
      Serial.print("BLINK_ON");
      break;

    case BLINK_OFF:
      Serial.print("BLINK_OFF");
      break;

    case AMBER:
      Serial.print("AMBER");
      break;

    case RED:
      Serial.print("RED");
      break;

    case GREEN:
      Serial.print("GREEN");
      break;

    case PREPARE_GREEN:
      Serial.print("PREPARE_GREEN");
      break;

    case RELEASE_INTERSECTION:
      Serial.print("RELEASE_INTERSECTION");
      break;

    case CHECK_CONTINUITY:
      Serial.print("CHECK_CONTINUITY");
      break;

    case LENGTH:
      Serial.print("LENGTH");
      break;

    case WAIT_TIMEOUT:
      Serial.print("WAIT_TIMEOUT");
      break;

    case REMOVE_INDEX:
      Serial.print("REMOVE_INDEX");
      break;
  }
  Serial.print(" ");
  Serial.print(_index);
#endif
}

#ifdef ESP
void onMessage(unsigned char* mac, unsigned char* data, unsigned char len)
{
  // cast & copy data
  Message incomingMessage;
  memcpy(&incomingMessage, data, sizeof(incomingMessage));

  // convert address
  //char macStr[18];
  //macToString(mac, macStr, 18);
  //Serial.printf("Recv: %d -> %d (%s)\n", incomingMessage.index, incomingMessage.state, macStr);

  recv(incomingMessage.state, incomingMessage.index);
}
#else
void simButtons()
{
  if (!digitalRead(4))
  {
    recv(BLINK_OFF, 255);
    delay(1000);
  }
  if (!digitalRead(3))
  {
    recv(PREPARE_GREEN, 1);
    delay(1000);
  }
  if (!digitalRead(2))
  {
    recv(RELEASE_INTERSECTION, 1);
    delay(1000);
  }
}
#endif

void send(State _state, uint8_t _index)
{
#ifdef DEBUG
  Serial.print("sent: ");
  printState(_state, _index);
  Serial.print('\n');
#endif

#ifdef ESP
  // Send the data
  Message remote;
  remote.index = _index;
  remote.state = _state;
  esp_now_send(broadcastMAC, (uint8_t *) &remote, sizeof(remote));
#endif
}

void recv(State _state, uint8_t _index)
{
#ifdef DEBUG  
  Serial.print("recv: ");
  printState(_state, _index);
  Serial.print('\n');
#endif

  // Something is alive, postpone the wait timeout
  if (nextState == WAIT_TIMEOUT && _state != REMOVE_INDEX)
    nextTick = millis() + WAIT_TIMEOUT_TIME;

  uint8_t previous = (currentIndex + length - 1) % length;

  switch (_state)
  {
    case BLINK_OFF:
      if (_index == 255 && currentIndex == 0)
      {
        length++;
        nextState = LENGTH;
        nextTick = 0; // immediate
      }
      break;

    case LENGTH:
      length = _index;
      if (currentIndex == 255)
      {
        currentIndex = length - 1;
        nextState = WAIT_TIMEOUT;
        nextTick = millis() + WAIT_TIMEOUT_TIME; // failsafe mode
        color(RED);
      }
      break;

    case RELEASE_INTERSECTION:
      // Do we need to take over?
#ifdef DEBUG
      Serial.print("us: ");
      Serial.print(currentIndex);
      Serial.print(", prev: ");
      Serial.print(previous);
      Serial.print(", length: ");
      Serial.println(length);
#endif
      if (currentIndex == previous)
      {
        nextState = PREPARE_GREEN;
        nextTick = 0; // immediate        
      }
        
      break;

    case PREPARE_GREEN:
      // Other node is taking over
      nextState = WAIT_TIMEOUT;
      nextTick = millis() + WAIT_TIMEOUT_TIME; // failsafe mode
      break;

    case REMOVE_INDEX:
      // If the index is lower than ours, we need to shift and check if its our turn
      // Note that we could cause conflict if we're 1 and receive 0
      if (currentIndex > _index + 1)
        currentIndex--;

      // Is is our turn?
      if (currentIndex == _index + 1)
      {
        nextState = PREPARE_GREEN;
        nextTick = 0; // immediate        
      }
      break;
  }

  // TODO: detect conflict and randomize message to send index++
}

#ifdef ESP
void color(State _state)
{
#ifdef DEBUG
  Serial.print("color: ");
#endif
  strip.clear();
  switch (_state)
  {
    case GREEN:
      strip.setPixelColor(0, 0, 255, 0);
#ifdef DEBUG
      Serial.println("green");
#endif
      break;

    case BLINK_ON:
    case AMBER:
      strip.setPixelColor(1, 255, 128, 0);
#ifdef DEBUG
      Serial.println("amber");
#endif
      break;

    case RED:
      strip.setPixelColor(2, 255, 0, 0);
#ifdef DEBUG
      Serial.println("red");
#endif
      break;

    case PREPARE_GREEN:
      strip.setPixelColor(2, 255, 0, 0);
      if (!digitalRead(GERMAN_PIN))
        strip.setPixelColor(1, 255, 128, 0);
#ifdef DEBUG
      Serial.println("red+amber");
#endif
      break;

#ifdef DEBUG
    default:
      Serial.println("none");
#endif
  }
  strip.show();  
}
#else
void color(State _state)
{
  digitalWrite(5, LOW);
  digitalWrite(6, LOW);
  digitalWrite(7, LOW);

  switch (_state)
  {
    case BLINK_ON:
    case AMBER:
      digitalWrite(6, HIGH);
      break;
    case RED:
      digitalWrite(5, HIGH);
      break;
    case PREPARE_GREEN:
      digitalWrite(5, HIGH);
      digitalWrite(6, HIGH);
      break;
    case GREEN:
      digitalWrite(7, HIGH);
      break;
  }
}
#endif

void setup()
{
  Serial.begin(115200);
#ifdef ESP
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(255); // Set BRIGHTNESS to about 1/5 (max = 255)

  // init mode
  WiFi.mode(WIFI_STA);

  if (esp_now_init())
  {
    Serial.println("Cannot init ESP-NOW");
    return;
  }

  // register as "controller"
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);

  // register callback
  // esp_now_register_send_cb(onMessage);
  esp_now_register_recv_cb(onMessage);

  // register as peer
  esp_now_add_peer(broadcastMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  pinMode(GERMAN_PIN, INPUT_PULLUP);
  pinMode(SLOW_PIN, INPUT_PULLUP);
  pinMode(FAST_PIN, INPUT_PULLUP);
  
#else
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);

  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
#endif

  currentIndex = 255;
  color(BLINK_ON);
  send(BLINK_ON, currentIndex);
  delay(BLINK_TIME);
  color(BLINK_OFF);
  send(BLINK_OFF, currentIndex);
  delay(BLINK_TIME);
  if (currentIndex == 255)
  {
#ifdef DEBUG
    Serial.println("single master");
#endif
    currentIndex = 0; // "master"
    length = 1;
  }
}

void loop() {
#ifndef ESP
  simButtons();
#endif

  if (millis() < nextTick)
    return;

  if (length == 1)
  {
    // Single light goes in blink mode
    color(nextState);
    if (nextState == BLINK_OFF)
      nextState = BLINK_ON;
    else
      nextState = BLINK_OFF;
    nextTick = millis() + BLINK_TIME;
    return;
  }

  switch (nextState)
  {
    case LENGTH:
      // New length restarts the program loop
      nextState = PREPARE_GREEN;
      send(LENGTH, length); // Special case "index"
      break;

    case BLINK_ON:
      color(BLINK_ON);
      // Go to off autonomously
      nextTick = millis() + BLINK_TIME;
      nextState = BLINK_OFF;
      break;

    case BLINK_OFF:
      color(BLINK_OFF);
      // Do nothing: wait
      delay(250);
      break;

    case PREPARE_GREEN:
      nextState = GREEN;
      color(PREPARE_GREEN);
      nextTick = millis() + PREPARE_GREEN_TIME;
      send(PREPARE_GREEN, currentIndex); // This triggers continuity
      break;

    case GREEN:
      nextState = AMBER;
      color(GREEN);
      nextTick = millis() + GREEN_TIME;
      //send(GREEN, currentIndex);
      break;

    case AMBER:
      nextState = RED;
      color(AMBER);
      nextTick = millis() + AMBER_TIME;
      //send(AMBER, currentIndex);
      break;

    case RED:
      nextState = RELEASE_INTERSECTION;
      color(RED);
      if (!digitalRead(FAST_PIN))
      {
        // Hack: ultra fast
        if (!digitalRead(SLOW_PIN))
          nextTick = 0;
        else
          nextTick = millis() + RED_TIME_FAST;
      }
        
      else if (!digitalRead(SLOW_PIN))
        nextTick = millis() + RED_TIME_SLOW;
      else
        nextTick = millis() + RED_TIME;
      //send(RED, currentIndex);
      break;

    case RELEASE_INTERSECTION:
      // State should be overridden by next node
      nextState = CHECK_CONTINUITY;
      nextTick = millis() + RELEASE_INTERSECTION_TIME;
      send(RELEASE_INTERSECTION, currentIndex);
      break;

    case CHECK_CONTINUITY:
      // If we arrive here, the next node does not exist
      nextTick = millis() + CHECK_CONTINUITY_TIME;
      length--;
      // Make sure we stay within the list
      if (currentIndex >= length)
        currentIndex--;
      send(REMOVE_INDEX, currentIndex);
      break;

    case WAIT_TIMEOUT:
      // Panic: just blink
      nextState = BLINK_OFF;
      nextTick = 0;
      // TODO: if the green node is turned off,
      //       the program halts: build in a failsafe that updates the timeout every time a message arrives
      break;
  }
}
