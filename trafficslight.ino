#define DEBUG

// ESP-NOW dynamic traffic light simulator
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

volatile uint8_t currentIndex = 255;
volatile uint8_t length = 1;

// TODO: cleanup and put in array
const int BLINK_TIME = 650;
const int GREEN_TIME = 4000;
const int AMBER_TIME = 2500;
const int PREPARE_GREEN_TIME = 1500;

// NOTE: take PREPARE_GREEN_TIME into account
const int RED_TIME_SLOW = 15000;
const int RED_TIME = 5000;
const int RED_TIME_FAST = 2000;

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
  STANDBY,
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

    case STANDBY:
      Serial.print("STANDBY");
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

void send(State _state, uint8_t _index)
{
#ifdef DEBUG
  Serial.print("sent: ");
  printState(_state, _index);
  Serial.print('\n');
#endif

  // Send the data
  Message remote;
  remote.index = _index;
  remote.state = _state;
  esp_now_send(broadcastMAC, (uint8_t *) &remote, sizeof(remote));
}

void recv(State _state, uint8_t _index)
{
#ifdef DEBUG  
  Serial.print("recv: ");
  printState(_state, _index);
  Serial.print('\n');
#endif

  // Another node is running, postpone the wait timeout
  if (nextState == WAIT_TIMEOUT && _state != REMOVE_INDEX)
     nextTick = millis() + WAIT_TIMEOUT_TIME;

  bool isPredecessor = length && _index < 255 && ((_index + 1) % length) == currentIndex;
  bool isSuccessor = length && _index < 255 && ((currentIndex + 1) % length) == _index;

  switch (_state)
  {
    case BLINK_OFF:
      // Is there a new node and are we master?
      if (_index == 255 && currentIndex == 0)
      {
        length++;
        nextState = LENGTH;
        nextTick = 0; // immediate
      }
      break;

    case BLINK_ON:
      // Assume master is sending this: synchronize
      nextState = BLINK_ON;
      nextTick = 0; // immediate
      break;

    case LENGTH:
      length = _index;
      if (currentIndex == 255)
      {
        currentIndex = length - 1;
        nextState = WAIT_TIMEOUT;
        nextTick = millis() + WAIT_TIMEOUT_TIME + random(2000);
        color(RED);
      }
      break;

    case RELEASE_INTERSECTION:
      // Our turn now!
      if (isPredecessor)
      {
        nextState = PREPARE_GREEN;
        nextTick = 0; // immediate
      }
      break;

    case RED:
      // Assume we're on hold
      if (isPredecessor)
      {
        nextState = STANDBY;
        nextTick = 0; // immediate
      }
      break;

    case STANDBY:
      // TODO: We could assume successor
      if (isSuccessor)
      {
        nextState = RELEASE_INTERSECTION;
        // Note that the nextTick was ticking to REMOVE; we now continue broadcast as normal
      }
      break;

    case REMOVE_INDEX:
      // Removing index affects the length (we do a failsafe: if received this, there are at least 2 of us)
      if (length > 2)
        length--;

      // If we're before the incoming index, we don't have to close the gap.
      if (currentIndex <= _index)
        return;

      currentIndex--;
      if (currentIndex == _index)
      {
        // Conflict! Do a complete restart
        ESP.restart();
      }
      else if (currentIndex == _index + 1)
      {
        // Our turn!
        nextState = STANDBY;
        nextTick = 0; // immediate
      }
      break;
  }

  // TODO: detect conflict and randomize message to send index++
}

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

void setup()
{
  Serial.begin(115200);
  strip.begin();
  strip.show();
  strip.setBrightness(255);

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

  currentIndex = 255;
  color(BLINK_ON);
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
  }
}

void loop() {
  if (millis() < nextTick)
    return;

  // If we're the only one or set to blink mode by "shorting" both speed pins
  if (length == 1 || (nextState != LENGTH && !digitalRead(FAST_PIN) && !digitalRead(SLOW_PIN)))
  {
    // Single light goes in blink mode
    color(nextState);
    // // If we're "master", emit the blinking so others can synchronize
    if (!currentIndex)
      send(nextState, currentIndex);

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
      // Next tick is immediate
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
     nextTick = millis() + BLINK_TIME + random(2000);     
     nextState = WAIT_TIMEOUT;
     break;

    case PREPARE_GREEN:
      nextState = GREEN;
      color(PREPARE_GREEN);
      nextTick = millis() + PREPARE_GREEN_TIME;
      break;

    case GREEN:
      nextState = AMBER;
      color(GREEN);
      nextTick = millis() + GREEN_TIME;
      break;

    case AMBER:
      nextState = RED;
      color(AMBER);
      nextTick = millis() + AMBER_TIME;
      break;

    case RED:
      // Note that sending red implies a STANDBY response; if not: REMOVE the index
      nextState = REMOVE_INDEX;
      color(RED);
      if (!digitalRead(FAST_PIN))
          nextTick = millis() + RED_TIME_FAST;
      else if (!digitalRead(SLOW_PIN))
        nextTick = millis() + RED_TIME_SLOW;
      else
        nextTick = millis() + RED_TIME;

      send(RED, currentIndex);
      break;

    case RELEASE_INTERSECTION:
      // We got here because the next node responded with STANDBY
      nextState = WAIT_TIMEOUT;
      nextTick = millis() + WAIT_TIMEOUT_TIME + random(2000);
      send(RELEASE_INTERSECTION, currentIndex);
      break;

    case REMOVE_INDEX:
      // If we arrive here, the next node does not exist
      nextTick = millis() + CHECK_CONTINUITY_TIME;
      if (length > 1)
      {      
        length--;
        // Make sure we stay within the list
        if (currentIndex >= length)
          currentIndex--;
        send(REMOVE_INDEX, currentIndex);
      }
      break;

    case STANDBY:
      nextState = WAIT_TIMEOUT;
      nextTick = millis() + WAIT_TIMEOUT_TIME + random(2000);
      send(STANDBY, currentIndex);
      break;


    case WAIT_TIMEOUT:
      // Program loop failed/halted
      // comes from STANDBY (we're successor), LENGTH (we're new), BLINK_OFF (we're slave), RELEASE_INTERSECTION (we're predecessor)
      break;
  }
}
