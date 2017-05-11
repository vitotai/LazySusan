#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include "NBSTepper.h"

static  int _mpin1;
static 	int _mpin2;
static 	int _mpin3;
static 	int _mpin4;

static volatile  short _step;
static bool _forward;
static bool _full;
static volatile  int _ticks;

static volatile  int _overflowCompare;
static volatile  int _settingCompare;

// user reader-writer pattern
static volatile  unsigned long _stepToGo;
static volatile  unsigned long _currentStep;

static volatile unsigned long _intCounter;

static volatile  byte _intRunning;

// 3ms/64us = 46.875
#define MICROSTEP_TICK  47

void _halfStep(void)
{
	if(_step ==0){
  		digitalWrite(_mpin1, HIGH); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, HIGH); 
	}else if(_step ==1){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, HIGH); 
	}else if(_step ==2){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, HIGH); 
	  	digitalWrite(_mpin4, HIGH); 
	}else if(_step ==3){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, HIGH); 
	  	digitalWrite(_mpin4, LOW); 
	}else if(_step ==4){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, HIGH); 
 	 	digitalWrite(_mpin3, HIGH); 
	  	digitalWrite(_mpin4, LOW); 
	}else if(_step ==5){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, HIGH); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, LOW); 
	}
	else if(_step ==6)
	{
  		digitalWrite(_mpin1, HIGH); 
  		digitalWrite(_mpin2, HIGH); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, LOW); 
	}
	else //if(_step ==7)
	{
  		digitalWrite(_mpin1, HIGH); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, LOW); 
	}
}

void _fullStep(void)
{
	if(_step ==0){
  		digitalWrite(_mpin1, HIGH); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, HIGH); 
	}else if(_step ==1){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, LOW); 
 	 	digitalWrite(_mpin3, HIGH); 
	  	digitalWrite(_mpin4, HIGH); 
	}else if(_step ==2){
  		digitalWrite(_mpin1, LOW); 
  		digitalWrite(_mpin2, HIGH); 
 	 	digitalWrite(_mpin3, HIGH); 
	  	digitalWrite(_mpin4, LOW); 
	}else //if(_step ==3)
	{
  		digitalWrite(_mpin1, HIGH); 
  		digitalWrite(_mpin2, HIGH); 
 	 	digitalWrite(_mpin3, LOW); 
	  	digitalWrite(_mpin4, LOW); 
	}
}

NBStepper::NBStepper(){};

void NBStepper::attach(int pin1, int pin2, int pin3, int pin4)
{
	_mpin1=pin1;
	_mpin2=pin2;
	_mpin3=pin3;
	_mpin4=pin4;

	pinMode(_mpin1, OUTPUT);
	pinMode(_mpin2, OUTPUT);
	pinMode(_mpin3, OUTPUT);
	pinMode(_mpin4, OUTPUT);

	_step=0;
	_stepToGo=0;
	_currentStep=0;
	_ticks=0;
	_intRunning=0;
	_overflowCompare =MICROSTEP_TICK;
	_settingCompare=_overflowCompare;
	NBStepper::_setupTimer();
	
}

void NBStepper::_setupTimer(void)
{
	// set Timer 1 prescaler to 1024
	// 64 us, CTC set
/*	TCCR1B = (TCCR1B & 0b11110000) |0x08 |0x04;
	OCR1A = _overflowCompare;
	// enable timer 1
	TIMSK1 |= (1<<OCIE1A);
*/
	noInterrupts();
    TCCR1A = 0;// set entire TCCR1A register to 0
    TCCR1B = 0;// same for TCCR1B
    TCNT1  = 0;//initialize counter value to 0
    // set compare match register for 1000000hz increments with 8 bits prescaler
    OCR1A = _overflowCompare;// = (16*10^6) / (1000000*8) - 1 (must be <65536)
    // turn on CTC mode
    TCCR1B |= (1 << WGM12);
    // Set CS11 bit for 8 prescaler. Each timer has a different bit code to each prescaler
    TCCR1B |= (1 << CS12); 
    TCCR1B |= (1 << CS10);  
    // enable timer compare interrupt
    TIMSK1 |= (1 << OCIE1A);
    interrupts();
}

void NBStepper::setMicroStepTime(int mul)
{
	_settingCompare = mul;
}

void NBStepper::microStep(unsigned long steps)
{
	_stepToGo += steps;
}

void NBStepper::step(unsigned long round)
{
	if(_full) _stepToGo += round * 4L;
	else  _stepToGo += round * 8L;
}

void  NBStepper::setFull(bool full)
{
	while(_intRunning) NULL;
	_full = full;
}

bool NBStepper::isForward(void)
{
	return _forward;
}

void  NBStepper::setForward(void)
{
	if(!_forward)
	{
		while(_intRunning) NULL;
		_forward=true;
		_currentStep = _stepToGo; 
	}
}

void  NBStepper::setBackward(void)
{
	if(_forward)
	{
		while(_intRunning) NULL;
		_forward=false;
		_currentStep = _stepToGo; 
	}
}

void  NBStepper::setRpm(int rpm)
{
}

bool  NBStepper::isRunning(void)
{
	return (_currentStep != _stepToGo);
}

bool  NBStepper::isFull(void)
{
	return _full;
}

void NBStepper::debug(void)
{
	Serial.print("_stepToGo=");
	Serial.print(_stepToGo);

	Serial.print(" _currentStep=");
	Serial.print(_currentStep);

	Serial.print(" _intCounter=");
	Serial.println(_intCounter);

}

ISR (TIMER1_COMPA_vect) 
{
	_intRunning=1;
	_intCounter++;
	
	if(_overflowCompare != _settingCompare)
	{
		_overflowCompare = _settingCompare;
		OCR1A = _overflowCompare;
	}
	
	if(_stepToGo!=_currentStep)
	{
			_currentStep++;
		
			if(!_full)
			{
				if(_forward)
				{
					_step ++;
					if(_step > 7) _step=0;
				}
				else
				{
					_step --;
					if(_step < 0) _step=7; 
				}
				_halfStep(); 
			}
			else
			{
				if(_forward)
				{
					_step ++;
					if(_step > 3) _step=0;
				}
				else
				{
					_step --;
					if(_step < 0) _step=3; 
				}
				_fullStep(); 
			}
	}
	_intRunning=0;
}