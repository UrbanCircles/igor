/**
 * Class: KY040
 * 
 * Description:
 * Class for KY-040 rotary encoders. Without builtin pull up resistors for 
 * CLK/DT you have to set pinMode( ,INPUT_PULLUP) before using the class.
 * The class works with or without interrupts and prevents bounces 
 * by ignoring invalid CLK/DT sequences
 * 
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 * For details see: LICENSE.txt
 * 
 * Valid clockwise sequence for CLK/DT: Low/High->Low/Low->High/Low->High/High
 * 
 * @code
 *        0 1 2 3
 *     --+   +----   High
 * CLK   |   |
 *       +---+       Low
 *       
 *     ----+   +--   High
 * DT      |   |
 *         +---+     Low
 * @endcode 
 *
 * Valid counter-clockwise sequence for CLK/DT: High/Low->Low/Low->Low/High->High/High    
 * 
 * @code
 *        0 1 2 3
 *     ----+   +---  High
 * CLK     |   |
 *         +---+     Low
 *         
 *     --+   +-----  High
 * DT    |   |
 *       +---+       Low
 * @endcode
 *
 * Home: https://github.com/codingABI/KY040
 *
 * @author codingABI https://github.com/codingABI/
 * @copyright 2-Clause BSD License
 * @file KY040.h
 * @version 1.0.1
 */
#pragma once

/** Library version */
#define KY040_VERSION "1.0.1"

#include <arduino.h>

/** When using sleep modes wait X milliseconds for next sleep after a CLK/DT sequence start do prevent missing signals */
#define PREVENTSLEEPMS 150
// Pin idle state
#define INITSTEP 0b11
// Max steps for a signal sequence
#define MAXSEQUENCESTEPS 4

/** Class for a KY-040 rotary encoder */
class KY040 {
  public:
      /** Rotation states */
    enum directions 
    { 
      IDLE, /**< Rotary encoder is idle */
      ACTIVE, /**< Rotary encoder is rotating, but the CLK/DT sequence has not finished */
      CLOCKWISE, /**< CLK/DT sequence for one step clockwise rotation has finished */
      COUNTERCLOCKWISE /**< CLK/DT sequence for one step counter-clockwise rotation has finished */
    };

    /**@brief
     * Constructor of a the KY-040 rotary encoder
     *
     * @param[in] clk_pin Digital input pin connected to CLK aka. A
     * @param[in] dt_pin Digital input pin connected to DT aka. B
     */
    KY040(byte clk_pin, byte dt_pin) 
    {
      m_clk_pin = clk_pin; // aka. A
      m_dt_pin = dt_pin; // aka. B
      v_state = 255;
      v_lastResult = IDLE;
      v_lastSequenceStartMillis = millis();
      v_sequenceStep = 0;
      v_direction = IDLE;
      v_oldState = INITSTEP;
    }

    /**@brief
     * Returns current rotation state from stored pin state.
     *
     * If you do not use interrupts, you have to start setState() and checkRotation() or a function using 
     * these (for example getRotation()) very frequently in your loop to prevent missing signals 
     *
     * @retval KY040::CLOCKWISE        CLK/DT sequence for one step clockwise rotation has finished
     * @retval KY040::COUNTERCLOCKWISE CLK/DT sequence for one step counter-clockwise rotation has finished
     * @retval KY040::IDLE             Rotary encoder is idle
     * @retval KY040::ACTIVE           Rotary encoder is rotating, but the CLK/DT sequence has not finished
     */
    byte checkRotation() 
    {
      byte result = IDLE;
   
      if (v_state != v_oldState) { // State changed?
        if (v_sequenceStep == 0) { // Check for begin of rotation
          if (v_state == c_signalSequenceCW[0]) { // Begin of CW
            v_direction=CLOCKWISE;
            v_sequenceStep = 1;
            v_lastSequenceStartMillis = millis();
          }
          if (v_state == c_signalSequenceCCW[0]) { // Begin of CCW
            v_direction=COUNTERCLOCKWISE; 
            v_sequenceStep = 1;
            v_lastSequenceStartMillis = millis();
          }
        } else {
          switch (v_direction) {
            case CLOCKWISE:
              if (v_state == c_signalSequenceCW[v_sequenceStep]) {
                v_sequenceStep++;
                if (v_sequenceStep >= MAXSEQUENCESTEPS) { // Sequence has finished
                  result=v_direction;
                  v_lastResult=result;
                  v_direction=IDLE;
                  v_sequenceStep=0;
                } else result=ACTIVE;
              } else { 
                // Invalid sequence
                if (v_state == INITSTEP) { // Reset sequence in init state
                  v_direction=IDLE;
                  v_sequenceStep=0;
                }
              }
              break;
            case COUNTERCLOCKWISE:
              if (v_state == c_signalSequenceCCW[v_sequenceStep]) {
                v_sequenceStep++;
                if (v_sequenceStep >= MAXSEQUENCESTEPS) { // Sequence has finished
                  result=v_direction;
                  v_lastResult=result;
                  v_direction=IDLE;
                  v_sequenceStep=0;
                } else result=ACTIVE;
              } else { 
                // Invalid sequence
                if (v_state == INITSTEP) { // Reset sequence in init state
                  v_direction=IDLE;
                  v_sequenceStep=0;
                }
              }
              break;
          }
        }
        v_oldState = v_state;
      }
      // Prevent unsigned long overrun
      if (millis() - v_lastSequenceStartMillis > PREVENTSLEEPMS) {
          v_lastSequenceStartMillis = millis() - PREVENTSLEEPMS - 1;
      }
      return result;
    }

    /**@brief
     * Get and reset last finished rotation step (Do not use inside ISR)
     *
     * @retval KY040::CLOCKWISE        CLK/DT sequence for one step clockwise rotation has finished
     * @retval KY040::COUNTERCLOCKWISE CLK/DT sequence for one step counter-clockwise rotation has finished
     * @retval KY040::IDLE             Rotary encoder is idle
     */
    byte getAndResetLastRotation() 
    {
      cli();
      byte result = v_lastResult;
      v_lastResult = IDLE;
      sei();
      return result;
    }

    /**@brief
     * Read and stores current pin state for CLK and DT and returns the current rotation state.
     *
     * Reads pin state for CLK and DT with DigitalRead() and checks current rotation state by calling checkRotation()
     *
     * @retval KY040::CLOCKWISE        CLK/DT sequence for one step clockwise rotation has finished
     * @retval KY040::COUNTERCLOCKWISE CLK/DT sequence for one step counter-clockwise rotation has finished
     * @retval KY040::IDLE             Rotary encoder is idle
     * @retval KY040::ACTIVE           Rotary encoder is rotating, but the CLK/DT sequence has not finished
     */
    byte getRotation() 
    { 
      setState((digitalRead(m_clk_pin)<<1)+digitalRead(m_dt_pin));
      return checkRotation();
    }

    /**@brief
     * Get stored pin states for CLK and DT (Left bit is for CLK, right bit is for DT). Should be called from ISR, when needed
     *
     * @returns Stored pin states for CLK and DT in two bits (Left bit is for CLK, right bit is for DT)
     */
    byte getState() 
    {
      return v_state;
    }

    /**@brief
     * Checks, if it save to go to sleep
     *
     * Returns true, if device was running long enough to get a full sequence (Do not use inside ISR)
     *
     * @retval true Yes, it is save to go to sleep
     * @retval false No, it is not save and you could miss signals, if you go to sleep anyway
     */
    bool readyForSleep() 
    {
      cli();
      unsigned long lastStepMillis = v_lastSequenceStartMillis;
      sei();
      return (millis()-lastStepMillis > PREVENTSLEEPMS);
    }

    /**@brief
     * Stores pin states for CLK and DT (Left bit is for CLK, right bit is for DT). Should be called from ISR, when needed.
     * 
     * @param[in] state Pin state for CLK and DT in two bits (Left bit is for CLK, right bit is for DT)
     */
    void setState(byte state) 
    {
      v_state = state;
    }
  private:
    byte m_clk_pin; // aka. A
    byte m_dt_pin; // aka. B
    volatile byte v_state;
    volatile byte v_lastResult;
    volatile unsigned long v_lastSequenceStartMillis;
    volatile byte v_sequenceStep;
    volatile byte v_direction;
    volatile byte v_oldState;
    // CLK/DT sequence for a clockwise rotation (One byte instead of a byte array would be enough for the four 2-bit values, but are harder to read)
    const byte c_signalSequenceCW[MAXSEQUENCESTEPS] = {0b01,0b00,0b10,INITSTEP};
    // CLK/DT sequence for a counter-clockwise rotation (One byte instead of a byte array would be enough for the four 2-bit values, but are harder to read)
    const byte c_signalSequenceCCW[MAXSEQUENCESTEPS] = {0b10,0b00,0b01,INITSTEP};    

};