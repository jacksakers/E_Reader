#ifndef BUTTONHANDLER_H
#define BUTTONHANDLER_H

#include <Arduino.h>

// ==================== BUTTON HANDLER CLASS ====================
// Provides reliable button input with debouncing and edge detection

class Button {
private:
  uint8_t pin;
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long debounceDelay;
  bool pressed;
  bool released;
  bool held;
  unsigned long pressStartTime;
  unsigned long holdThreshold;
  unsigned long lastRepeatTime;
  unsigned long repeatDelay;
  bool repeating;
  
public:
  Button(uint8_t buttonPin, unsigned long debounceMs = 50, unsigned long holdMs = 500) {
    pin = buttonPin;
    debounceDelay = debounceMs;
    holdThreshold = holdMs;
    repeatDelay = 150;  // Repeat every 150ms when held
    lastState = HIGH;
    currentState = HIGH;
    lastDebounceTime = 0;
    pressed = false;
    released = false;
    held = false;
    repeating = false;
    pressStartTime = 0;
    lastRepeatTime = 0;
  }
  
  void begin() {
    pinMode(pin, INPUT_PULLUP);  // Use internal pull-up resistor
    currentState = digitalRead(pin);
    lastState = currentState;
  }
  
  // Call this in your loop() to update button state
  void update() {
    // Reset edge detection flags
    pressed = false;
    released = false;
    
    // Read current button state (LOW when pressed due to pull-up)
    int reading = digitalRead(pin);
    
    // If the state changed, reset debounce timer
    if (reading != lastState) {
      lastDebounceTime = millis();
    }
    
    // If enough time has passed since last state change, accept it
    if ((millis() - lastDebounceTime) > debounceDelay) {
      // If the state actually changed
      if (reading != currentState) {
        currentState = reading;
        
        // Detect button press (goes from HIGH to LOW)
        if (currentState == LOW) {
          pressed = true;
          pressStartTime = millis();
          held = false;
        }
        // Detect button release (goes from LOW to HIGH)
        else {
          released = true;
          held = false;
        }
      }
    }
    
    // Check for held state
    if (currentState == LOW && !held) {
      if ((millis() - pressStartTime) > holdThreshold) {
        held = true;
        repeating = true;
        lastRepeatTime = millis();
      }
    }
    
    // Handle button repeat when held
    if (repeating && currentState == LOW) {
      if ((millis() - lastRepeatTime) > repeatDelay) {
        pressed = true;  // Trigger pressed event again
        lastRepeatTime = millis();
      }
    }
    
    // Stop repeating when released
    if (currentState == HIGH) {
      repeating = false;
    }
    
    lastState = reading;
  }
  
  // Returns true once when button is first pressed
  bool wasPressed() {
    return pressed;
  }
  
  // Returns true once when button is released
  bool wasReleased() {
    return released;
  }
  
  // Returns true if button is currently being pressed
  bool isPressed() {
    return currentState == LOW;
  }
  
  // Returns true once when button has been held for holdThreshold time
  bool wasHeld() {
    return held;
  }
  
  // Returns how long button has been held (0 if not pressed)
  unsigned long getHoldDuration() {
    if (currentState == LOW) {
      return millis() - pressStartTime;
    }
    return 0;
  }
};

// ==================== BUTTON MANAGER ====================
// Manages all device buttons

class ButtonManager {
private:
  Button* homeBtn;
  Button* exitBtn;
  Button* prvBtn;
  Button* nextBtn;
  Button* okBtn;
  
public:
  ButtonManager(uint8_t homePin, uint8_t exitPin, uint8_t prvPin, 
                uint8_t nextPin, uint8_t okPin) {
    homeBtn = new Button(homePin);
    exitBtn = new Button(exitPin);
    prvBtn = new Button(prvPin);
    nextBtn = new Button(nextPin);
    okBtn = new Button(okPin);
  }
  
  void begin() {
    homeBtn->begin();
    exitBtn->begin();
    prvBtn->begin();
    nextBtn->begin();
    okBtn->begin();
  }
  
  // Update all buttons - call this every loop
  void update() {
    homeBtn->update();
    exitBtn->update();
    prvBtn->update();
    nextBtn->update();
    okBtn->update();
  }
  
  // Get button objects
  Button* home() { return homeBtn; }
  Button* exit() { return exitBtn; }
  Button* prv() { return prvBtn; }
  Button* next() { return nextBtn; }
  Button* ok() { return okBtn; }
  
  // Quick check functions
  bool anyPressed() {
    return homeBtn->wasPressed() || exitBtn->wasPressed() || 
           prvBtn->wasPressed() || nextBtn->wasPressed() || okBtn->wasPressed();
  }
  
  bool anyHeld() {
    return homeBtn->isPressed() || exitBtn->isPressed() || 
           prvBtn->isPressed() || nextBtn->isPressed() || okBtn->isPressed();
  }
};

#endif // BUTTONHANDLER_H
