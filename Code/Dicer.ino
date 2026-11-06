#include <TFT_eSPI.h> 
#include <Bounce2.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "sprites.h" 

// --- Pin Definitions ---
#define BTN_LEFT D3
#define BTN_RIGHT D6
#define BTN_UP D7
#define BTN_DOWN D9
#define I2C_SDA D4
#define I2C_SCL D5

// --- Hardware Objects ---
TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite spr = TFT_eSprite(&tft); 
Adafruit_MPU6050 mpu;

Bounce2::Button leftBtn = Bounce2::Button();
Bounce2::Button rightBtn = Bounce2::Button();
Bounce2::Button upBtn = Bounce2::Button();
Bounce2::Button downBtn = Bounce2::Button();

// --- Game States ---
enum SystemState { STATE_MENU, STATE_ROLLING, STATE_RESULTS };
SystemState currentState = STATE_MENU;

// --- Dice & Queue Data ---
const int NUM_DICE = 6; 
String diceNames[NUM_DICE] = {"D10", "D12", "D20", "COIN", "D6", "D8"};
// A helper array to map the name to the actual mathematical sides
int diceSides[NUM_DICE]    = { 10,    12,    20,    2,      6,    8  };

int currentDieIndex = 0;
const int MAX_TOTAL_DICE = 36;
int queuedDiceCounts[NUM_DICE] = {0, 0, 0, 0, 0, 0}; 
int totalQueued = 0; 
int grandTotalResult = 0;
int rollsMemory[NUM_DICE][MAX_TOTAL_DICE];

unsigned long downPressTime = 0;
bool downHandled = false;

void setup() {
  // Buttons
  leftBtn.attach(BTN_LEFT, INPUT_PULLUP); leftBtn.interval(10); leftBtn.setPressedState(LOW); 
  rightBtn.attach(BTN_RIGHT, INPUT_PULLUP); rightBtn.interval(10); rightBtn.setPressedState(LOW);
  upBtn.attach(BTN_UP, INPUT_PULLUP); upBtn.interval(10); upBtn.setPressedState(LOW);
  downBtn.attach(BTN_DOWN, INPUT_PULLUP); downBtn.interval(10); downBtn.setPressedState(LOW);

  // Screen
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  spr.createSprite(160, 120);

  // Accelerometer
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!mpu.begin()) {
    tft.setTextColor(TFT_RED);
    tft.drawString("MPU6050 FAILED!", 10, 10);
    while (1) { delay(10); } // Halt if sensor is dead
  }
  
  // Set accelerometer range to +/- 8G (perfect for shaking)
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G); 

  drawMenuUI();
}

void loop() {
  switch (currentState) {
    case STATE_MENU:
      handleMenu();
      break;
    case STATE_ROLLING:
      playRollingAnimation();
      calculateResults();
      currentState = STATE_RESULTS;
      break;
    case STATE_RESULTS:
      showResultsScreen();
      break;
  }
}

// ==========================================
// STATE 1: THE MENU
// ==========================================
void handleMenu() {
  leftBtn.update(); rightBtn.update(); upBtn.update(); downBtn.update();

  // 1. Check for a physical shake!
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Calculate total acceleration magnitude (Pythagorean theorem in 3D)
  float accel_mag = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
  
  // Normal gravity is ~9.8. A hard shake easily hits 20+
  if (accel_mag > 50.0 && totalQueued > 0) {
    // Seed our random number generator with physical noise for true randomness!
    randomSeed(analogRead(A0) + (a.acceleration.x * 1000));
    currentState = STATE_ROLLING;
    return; // Exit menu immediately
  }

  // 2. Normal Menu Navigation (Your existing code!)
  if (rightBtn.pressed()) {
    int nextIndex = (currentDieIndex + 1) % NUM_DICE;
    animateTransition(currentDieIndex, nextIndex, -1);
    currentDieIndex = nextIndex;
  }
  if (leftBtn.pressed()) {
    int nextIndex = (currentDieIndex - 1 + NUM_DICE) % NUM_DICE;
    animateTransition(currentDieIndex, nextIndex, 1);
    currentDieIndex = nextIndex;
  }
  if (upBtn.pressed() && totalQueued < MAX_TOTAL_DICE) {
    queuedDiceCounts[currentDieIndex]++;
    totalQueued++;
    drawHeader(); drawStaticDie(currentDieIndex);
  }

  if (downBtn.pressed()) { downPressTime = millis(); downHandled = false; }
  
  if (downBtn.isPressed() && !downHandled && (millis() - downPressTime > 800)) {
    clearAllDice(); downHandled = true; 
  }
  
  if (downBtn.released() && !downHandled && queuedDiceCounts[currentDieIndex] > 0) {
    queuedDiceCounts[currentDieIndex]--;
    totalQueued--;
    drawHeader(); drawStaticDie(currentDieIndex);
  }
}

// ==========================================
// STATE 2: THE ROLL
// ==========================================
void playRollingAnimation() {
  tft.fillScreen(TFT_BLACK);
  
  // Flash blank shapes and random numbers rapidly for 1.5 seconds
  for(int i = 0; i < 20; i++) {
    spr.fillSprite(TFT_BLACK);
    
    // Pick a random die shape from your array
    int randomShape = random(0, NUM_DICE);
    
    // Add jitter to the X and Y coordinates to make it look violent
    int jitterX = 40 + random(-5, 6);
    int jitterY = 20 + random(-5, 6);
    
    spr.setSwapBytes(true);
    spr.pushImage(jitterX, jitterY, 80, 80, (const uint16_t*)diceArray[randomShape]);
    
    // Draw a random flashing number over it
    spr.setTextColor(TFT_BLACK);
    spr.setTextSize(3);
    spr.setTextDatum(MC_DATUM);
    spr.drawNumber(random(1, 21), jitterX + 40, jitterY + 40);
    
    // Push the sprite, slightly off-center for more chaos
    spr.pushSprite(40 + random(-10, 11), 60 + random(-10, 11));
    delay(50);
  }
}

void calculateResults() {
  grandTotalResult = 0;

  // Loop through every type of die
  for (int type = 0; type < NUM_DICE; type++) {
    // For every die queued of this type, roll it!
    for (int count = 0; count < queuedDiceCounts[type]; count++) {
       int singleRoll = random(1, diceSides[type] + 1);

       grandTotalResult += singleRoll;
       rollsMemory[type][count] = singleRoll;
    }
  }
}

// ==========================================
// STATE 3: THE RESULTS
// ==========================================
void showResultsScreen() {
  int scrollOffset = 0;
  int maxScroll = 0;
  bool needsRedraw = true; // Flag to tell the screen when to update

  // Trap the user in this interactive results loop
  while(true) {
    
    // Only redraw the screen if the user pressed a button
    if (needsRedraw) {
      tft.fillScreen(TFT_BLACK);

      // ==========================================
      // 1. Draw the Scrolling Middle Section First
      // ==========================================
      // Subtract the scrollOffset so everything moves UP when you scroll DOWN
      int startY = 45 - scrollOffset; 

      for (int type = 0; type < NUM_DICE; type++) {
        if (queuedDiceCounts[type] > 0) {
          
          // Draw the die sprite
          tft.setSwapBytes(true);
          tft.pushImage(5, startY, 80, 80, (const uint16_t*)diceArray[type]);

          // Draw the die name over it
          tft.setTextColor(TFT_BLACK);
          tft.setTextSize(3);
          tft.setTextDatum(MC_DATUM);
          tft.drawString(diceNames[type], 45, startY + 40);

          // Setup the text grid
          tft.setTextColor(TFT_WHITE);
          tft.setTextDatum(TL_DATUM); 
          
          int cursorX = 95; 
          int cursorY = startY + 5;
          
          int xSpace, ySpace;
          if (queuedDiceCounts[type] > 12) {
            tft.setTextSize(1); xSpace = 20; ySpace = 15;
          } else {
            tft.setTextSize(2); xSpace = 30; ySpace = 25;
          }

          // Draw the numbers
          for (int count = 0; count < queuedDiceCounts[type]; count++) {
            tft.drawString(String(rollsMemory[type][count]), cursorX, cursorY);
            cursorX += xSpace; 
            if (cursorX + 15 > tft.width()) {
              cursorX = 95;      
              cursorY += ySpace; 
            }
          }

          // Move down for the next die type
          startY += max(85, (cursorY - (startY + 5)) + ySpace + 5); 
        }
      }

      // Calculate how far we are allowed to scroll
      int totalContentHeight = startY - (45 - scrollOffset);
      // Screen height (240) minus Header (45) minus Footer (20) = 175px of viewable space
      maxScroll = max(0, totalContentHeight - 175); 


      // ==========================================
      // 2. Draw the Static Header & Footer ON TOP
      // ==========================================
      
      // Draw a solid black box over the top 40 pixels to hide scrolling text
      tft.fillRect(0, 0, tft.width(), 40, TFT_BLACK); 
      tft.setTextColor(TFT_GREEN);
      tft.setTextSize(3);
      tft.setTextDatum(TC_DATUM); 
      tft.drawString("TOTAL: " + String(grandTotalResult), tft.width() / 2, 5);
      tft.drawFastHLine(0, 35, tft.width(), TFT_WHITE);

      // Draw a solid black box over the bottom 20 pixels
      tft.fillRect(0, 220, tft.width(), 20, TFT_BLACK);
      tft.setTextColor(TFT_DARKGREY); 
      tft.setTextSize(1);
      tft.setTextDatum(BC_DATUM);
      tft.drawString("<Exit>        Scroll ^v", tft.width() / 2, tft.height() - 2);

      // Screen is updated, wait for next input
      needsRedraw = false; 
    }

    // ==========================================
    // 2.5 Shake to Re-Roll!
    // ==========================================
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float accel_mag = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
    
    if (accel_mag > 50.0) {
      // Re-seed the physics randomizer so the new roll is completely different
      randomSeed(analogRead(A0) + (a.acceleration.x * 1000));
      
      // Send it right back to the rolling animation!
      currentState = STATE_ROLLING;
      break; 
    }

    // ==========================================
    // 3. Handle Button Inputs
    // ==========================================
    leftBtn.update(); rightBtn.update(); upBtn.update(); downBtn.update();

    // EXIT to menu
    if (leftBtn.pressed() || rightBtn.pressed()) {
      clearAllDice();
      tft.fillScreen(TFT_BLACK);
      drawMenuUI();
      currentState = STATE_MENU;
      break; 
    }

    // SCROLL DOWN (Shows content lower on the page)
    if (downBtn.pressed() && scrollOffset < maxScroll) {
      scrollOffset += 40; // Scroll by 40 pixels per click
      if (scrollOffset > maxScroll) scrollOffset = maxScroll; // Don't overshoot
      needsRedraw = true;
    }

    // SCROLL UP
    if (upBtn.pressed() && scrollOffset > 0) {
      scrollOffset -= 40; 
      if (scrollOffset < 0) scrollOffset = 0; // Don't overshoot
      needsRedraw = true;
    }
  }
}


// ==========================================
// UTILITY / GRAPHICS (From previous steps)
// ==========================================
void clearAllDice() {
  for (int i = 0; i < NUM_DICE; i++) { queuedDiceCounts[i] = 0; }
  totalQueued = 0;
  drawHeader(); drawStaticDie(currentDieIndex);
}

void drawMenuUI() {
  drawHeader();
  drawFooter();
  drawStaticDie(currentDieIndex);
}

void drawHeader() {
  tft.fillRect(0, 0, tft.width(), 34, TFT_BLACK); 
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setTextDatum(TC_DATUM);
  String headerText = "QUEUED: " + String(totalQueued) + "/" + String(MAX_TOTAL_DICE);
  tft.drawString(headerText, tft.width() / 2, 10);
  tft.drawFastHLine(0, 35, tft.width(), TFT_WHITE);
}

void drawFooter() {
  tft.setTextColor(TFT_WHITE); tft.setTextSize(1); tft.setTextDatum(BC_DATUM);
  tft.drawString("< L/R >  ADD^  vDEL", tft.width() / 2, tft.height() - 5);
}

void drawDieToSprite(int index, int xOffset) {
  int xPos = 40 + xOffset; int yPos = 10; 
  spr.setSwapBytes(true); 
  spr.pushImage(xPos, yPos, 80, 80, (const uint16_t*)diceArray[index]);
  spr.setTextColor(TFT_BLACK); spr.setTextSize(3); spr.setTextDatum(MC_DATUM);  
  spr.drawString(diceNames[index], xPos + 40, yPos + 40); 
  if (queuedDiceCounts[index] > 0) {
    spr.setTextColor(TFT_WHITE); spr.setTextSize(2); spr.setTextDatum(TC_DATUM);  
    String amountLabel = "x" + String(queuedDiceCounts[index]);
    spr.drawString(amountLabel, xPos + 40, 94); 
  }
}

void drawStaticDie(int index) {
  spr.fillSprite(TFT_BLACK); drawDieToSprite(index, 0); spr.pushSprite(40, 60); 
}

void animateTransition(int oldIndex, int newIndex, int direction) {
  for (int offset = 0; offset <= 160; offset += 25) { 
    spr.fillSprite(TFT_BLACK); 
    drawDieToSprite(oldIndex, offset * direction); 
    drawDieToSprite(newIndex, (offset - 160) * direction);
    spr.pushSprite(40, 60); delay(5); 
  }
}