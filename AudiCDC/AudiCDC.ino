/**
 * Copyright Mark A. Patel
 * Audi CD Changer Emulator for use with H18 Bluetooth audio module.
 * 
 * This code assumes the following connections:
 * 
 * - Radio Data Out  ->  DATA_IN_PIN (D2)
 * - Radio Clock In  <-  SCK (Pin 13)
 * - Radio Data In   <-  MOSI (Pin 11)
 * - BT Key Input    <-  D6 (via X ohm resistor and diode) 
 *   BT Key Input    <-  D7 (via X ohm resistor and diode)
 */
#include <SPI.h>

/* PIN ASSIGNMENTS */
// Pin for receiving data from the radio
#define DATA_IN_PIN 2
// Pins to generate key signals on the Bluetooth module
#define PREV_PIN 6
#define NEXT_PIN 7

/* TIMING CONSTANTS */
#define BT_KEY_PRESS_DURATION 150 // 40-800msec seems to work

// Pulse width durations for signals from the radio (micros) 
#define LEADER_TIME_HIGH      9000
#define LEADER_TIME_LOW       4500
#define ZERO_TIME_LOW         562
#define MIN_LOW_TIME_FOR_ONE  843

#define PULSE_TIME_TOLERANCE 500

/* RADIO COMMANDs */
// First and second bytes for command packets from the radio
#define RADIO_CMD_PREFIX1 0x53
#define RADIO_CMD_PREFIX2 0x2C

// Commands from the radio
#define RADIO_CMD_TURN_ON     0xE4
#define RADIO_CMD_TURN_OFF    0x10
#define RADIO_CMD_PREV        0x78
#define RADIO_CMD_NEXT        0xF8
#define RADIO_CMD_PREV_CD     0x18
#define RADIO_CMD_NEXT_CD     0x38

// Delay between SPI byte transmissions
#define SPI_TX_BYTE_DELAY 800

// Clock time (millis) when last packet was sent to HU
unsigned long last_package_send_time = 0;

unsigned long last_edge_time = micros();
unsigned long time_high = 0;
int8_t pulse_number = -1;

// Stores the received data as a series of bits
volatile uint8_t rx_data[32];
// Indicates when rx_data is ready to be processed
volatile bool rx_data_ready = false;

bool playing = false;

// Time when next key should be released
long next_key_up_millis = millis();

// Time when prev key should be released
long prev_key_up_millis = millis();

void setup() {
  Serial.begin(115200);

  // Initialize pins controlling the BT buttons
  pinMode(NEXT_PIN, OUTPUT);
  pinMode(PREV_PIN, OUTPUT);
  digitalWrite(NEXT_PIN, HIGH);
  digitalWrite(PREV_PIN, HIGH);
  
  pinMode(DATA_IN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(DATA_IN_PIN), on_data_in_level_changed, CHANGE);

  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE1);
  SPI.setClockDivider(SPI_CLOCK_DIV128); //62.5kHz@8Mhz 125kHz@16MHz
}


void spi_tx(uint8_t f1, uint8_t cd_num, uint8_t track_num, uint8_t mins, uint8_t secs, uint8_t mode, uint8_t f2, uint8_t f3)
{
  SPI.transfer(f1);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(cd_num);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(track_num);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(mins);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(secs);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(mode);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(f2);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  SPI.transfer(f3);
  delayMicroseconds(SPI_TX_BYTE_DELAY);
  // Record when package was sent
  last_package_send_time = millis();
}


void on_data_in_level_changed() {
  // Record the time when the change occurred
  const unsigned long edge_time = micros();
  
  if (digitalRead(DATA_IN_PIN) == LOW) {
    // Half-way through the pulse, record how long the pulse was high and wait for next edge
    time_high = edge_time - last_edge_time; 
  } else {
    // Pulse complete, determine how long the pulse was low
    const unsigned long time_low = edge_time - last_edge_time;
    
    // Process the pulse...
    if (pulse_number == -1) {
      // Looking for a leader pulse
      if ((time_high > LEADER_TIME_HIGH - 1000) && (time_low > LEADER_TIME_LOW - 500)) {
        // Found the leader, start reading data bits
        pulse_number = 0;
        rx_data_ready = false;
      }
    } else if (pulse_number < 32) {
      // Store the data bit pulse as a 1 or 0 based on how long it was low
      rx_data[pulse_number++] = time_low > MIN_LOW_TIME_FOR_ONE;
      // Data is ready once last bit has been read
      if (pulse_number == 32) {
        rx_data_ready = true;
      }
    } else if (pulse_number == 32) {
      // End of trailing pulse and quiet period, prepare to look for the next leader
      pulse_number = -1;
    }
  }  
  // Record time of current edge
  last_edge_time = edge_time;
}


/**
 * Checks if a valid command packet has been received from the radio and returns
 * the command code, or 0 if no new commands have been received.
 */
uint8_t getRxCommand() {
  if (rx_data_ready) {
    // Convert stream of bits into 4 bytes, MSB to LSB
    uint8_t rx_bytes[] = {0, 0, 0, 0};
    for (int i = 0; i < 32; i++) {
      if (rx_data[i]) {
        rx_bytes[i >> 3] |= 128 >> (i & 7);
      }
    }
    // Mark the data as being processed
    rx_data_ready = false;
    
    // Check that the first two bytes match our expected codes and that
    // the command code bytes are complementary
    if ((rx_bytes[0] == RADIO_CMD_PREFIX1) && (rx_bytes[1] == RADIO_CMD_PREFIX2) &&
      (rx_bytes[2] | rx_bytes[3] == 0xFF)) {
      // Return the valid command code
      return rx_bytes[2];
    }
  }
  // No command
  return 0;
}



void loop() {
  const unsigned long now = millis();

  // Check if we're due to send a message to the radio
  if (now > last_package_send_time + 400) {
    if (playing) {
      spi_tx(0x34,0xBE,0xFE,0xFE,0xFE,0xFE,0xAE,0x3C); // Playing
    } else {
      spi_tx(0x74,0xBE,0xFE,0xFE,0xFE,0xFF,0x8F,0x7C); // Idle
    }
  }
  
  // Check for an incoming command
  const uint8_t cmd = getRxCommand();
  switch (cmd) {
    case 0:
      break;
      
    // Power off (or exit CD mode)
    case RADIO_CMD_TURN_OFF:
      playing = false;
      spi_tx(0x74,0xBE,0xFE,0xFE,0xFE,0xFF,0x8F,0x7C); //IDLE
      break;

    // Power on in CD mode (or switch to CD mode)
    case RADIO_CMD_TURN_ON:
      spi_tx(0x14,0xBE,0xFE,0xFE,0xFE,0xFA,0xCF,0x1C);
      delay(1);
      spi_tx(0x34,0x2E,0xFE,0xFE,0xFE,0xFA,0xFF,0x3C);
      playing = true;
      break;

    // Previous track
    case RADIO_CMD_PREV:
    case RADIO_CMD_PREV_CD:
      digitalWrite(PREV_PIN, LOW);
      prev_key_up_millis = now + BT_KEY_PRESS_DURATION;
      break;

    // Next track
    case RADIO_CMD_NEXT:
    case RADIO_CMD_NEXT_CD:
      digitalWrite(NEXT_PIN, LOW);
      next_key_up_millis = now + BT_KEY_PRESS_DURATION;
      break;

    default:
      break;
      //Serial.println(cmd, HEX);
      // Don't care
  }

  // Release BT keys if due
  if (prev_key_up_millis < now) {
    digitalWrite(PREV_PIN, HIGH);
  }
  if (next_key_up_millis < now) {
    digitalWrite(NEXT_PIN, HIGH);
  }
  
  
}
