/*
 * sys_io.h
 *
 * Handles raw interaction with system I/O
 *
Copyright (c) 2021 Collin Kidder, Michael Neuweiler, Charles Galpin

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */


#ifndef SYS_IO_H_
#define SYS_IO_H_

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "eeprom_layout.h"
#include "PrefHandler.h"
#include "Logger.h"

class CANIODevice;

typedef struct {
    uint16_t offset;
    uint16_t gain;
} ADC_COMP;

enum SystemType {
    GEVCU1 = 1,
    GEVCU2 = 2,
    GEVCU3 = 3,
    GEVCU4 = 4,
    GEVCU5 = 5,
    GEVCU6 = 6,
    GEVCU7 = 7
};

class ExtendedIODev
{
public:
    CANIODevice *device;
    uint8_t localOffset;
};

//Base address 0x20. Then A0-A2 set lower 3 bits so 0x20-0x27 but only A0 is high on GEVCU7
#define PCA_ADDR    0x21

#define PCA_READ_IN0    0
#define PCA_READ_IN1    1
#define PCA_WRITE_OUT0  2
#define PCA_WRITE_OUT1  3
#define PCA_POLARITY_0  4
#define PCA_POLARITY_1  5
#define PCA_CFG_0       6
#define PCA_CFG_1       7
#define PCA_WRITE       0
#define PCA_READ        1

class SystemIO
{
public:
    SystemIO();
    
    void setup();
    void setup_ADC_params();

    int16_t getAnalogIn(uint8_t which); //get value of one of the 4 analog inputs
    boolean setAnalogOut(uint8_t which, int32_t level);
    int32_t getAnalogOut(uint8_t which);
    boolean getDigitalIn(uint8_t which); //get value of one of the 4 digital inputs
    void setDigitalOutput(uint8_t which, boolean active); //set output high or not
    boolean getDigitalOutput(uint8_t which); //get current value of output state (high?)    
    
    void setDigitalInLatchMode(int which, LatchModes::LATCHMODE mode);
    void unlockDigitalInLatch(int which);
    
    void installExtendedIO(CANIODevice *device);
    
    int numDigitalInputs();
    int numDigitalOutputs();
    int numAnalogInputs();
    int numAnalogOutputs();
    
    void setSystemType(SystemType);
    SystemType getSystemType();
    bool calibrateADCOffset(int, bool);
    bool calibrateADCGain(int, int32_t, bool);

private:
    void initDigitalMultiplexor();
    int _pGetDigitalInput(int pin);
    void _pSetDigitalOutput(int pin, int state);
    int _pGetDigitalOutput(int pin);
    int16_t _pGetAnalogRaw(uint8_t which);

    SystemType sysType;

    ADC_COMP adc_comp[NUM_ANALOG]; //GEVCU 7 has 8 adc inputs

    int adcMuxSelect;

    uint8_t pcaDigitalOutputCache;
    
    int numDigIn;
    int numDigOut;
    int numAnaIn;
    int numAnaOut;
    
    ExtendedIODev extendedDigitalOut[NUM_EXT_IO];
    ExtendedIODev extendedDigitalIn[NUM_EXT_IO];
    ExtendedIODev extendedAnalogOut[NUM_EXT_IO];
    ExtendedIODev extendedAnalogIn[NUM_EXT_IO];    
};

extern PrefHandler *sysPrefs;
extern SystemIO systemIO;

#endif