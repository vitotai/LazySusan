#include <EEPROM.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

#include <Buzz.h>

#include <RotaryEncoder.h>

#include <NBStepper.h>

const int s1 = 13;
const int s2 = 12;
const int s3 = 11;
const int s4 =8;


#define RA_CLK_PIN 4
#define RB_DT_PIN 3
#define SW_PIN 2
#define ANCHOR_PIN A2
#define BUZZ_PIN 6


#define STEPS_PER_ROUND 508
#define MAX_SPIN_OVER 508

#define MAX_BUCKET_NUMBER 8
#define MIN_BUCKET_NUMBER 4

#define DEFAULT_BUCKET_NUM 6

#define MAX_EVENT_NUM 12


#define MIN_BOIL_TIME 5
#define MAX_BOIL_TIME 120

//********************************************************************
//* Global objects
//********************************************************************
byte gBucketNumber;

RotaryEncoder encoder(RA_CLK_PIN,RB_DT_PIN,SW_PIN);
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
Buzzer buzzer(BUZZ_PIN);

const byte eventAlarm[] PROGMEM ={7,20,10,20,10,30,10,20,10,20,10,30,10}; // x25
const byte hopAlarm[] PROGMEM ={7,30,10,30,10,30,10,40}; // x25

//********************************************************************
//* Function declaration
//********************************************************************

typedef	void (*SetupFunc)(void);
typedef	void (*LoopFunc)(void);

typedef struct _CScreen{
	SetupFunc setup;
	LoopFunc loop;
}CScreen;

#define MainMenu_SCREEN 	0
#define Edit_SCREEN 	1
#define Run_SCREEN 	2
#define Remote_SCREEN 	3
#define Misc_SCREEN 	4

#define ManualSpin_SCREEN 5
#define TestSpin_SCREEN 6
#define SetSpinner_SCREEN 7

#define Options_SCREEN 	8


void switchScreen(byte sid);



//********************************************************************
//* PersistenceStorage
//********************************************************************

// 0-10 reserved

#define AddressBucketNumber 11
#define AddressAutoAnchor  12
#define AddressForwardClockwise 13
#define AddressSpinOver 14
//#define AddressSpinOver 15


#define EVENT_TIME_BASE 20
#define EVENT_TYPE_BASE 36

#define AddressTime(i) ((i) + EVENT_TIME_BASE)
#define AddressType(i) ((i) + EVENT_TYPE_BASE)

#define STEP_BASE 50
#define AddressStep(i) ((i)*2 + STEP_BASE)

class PersistenceStorage
{
private:
	void initializeSettings(void)
	{
		gBucketNumber=DEFAULT_BUCKET_NUM;
		updateBucketNumber(gBucketNumber);
		byte i;
		
		int averageStep =(int)( STEPS_PER_ROUND/gBucketNumber);
		
		for(i=2;i<=gBucketNumber;i++)
		{
			updateStep(i,averageStep);
		}
		updateStep(1,averageStep/2); // 1st bucket will turn from anchor point
	}
	
public:

	bool forwardCW(void)
	{
		return (bool) EEPROM.read(AddressForwardClockwise);
	}

	void updateForwardCW(bool fcw)
	{
		EEPROM.update(AddressForwardClockwise,(byte)fcw);
	}
	
	byte readTime(byte index)
	{
		return EEPROM.read(AddressTime(index));
	}

	void updateTime(byte index, byte value)
	{
/*		Serial.print("Set time of ");
		Serial.print(index);
		Serial.print(" value:");
		Serial.println(value);
*/
		EEPROM.update(AddressTime(index),value);
	}
	
	byte readType(byte index)
	{
		return EEPROM.read(AddressType(index));
	}
	
	void updateType(byte index, byte value)
	{
		/*Serial.print("Set Type of ");
		Serial.print(index);
		Serial.print(" value:");
		Serial.println(value);*/
		
		EEPROM.update(AddressType(index),value);
	}

	int readStep(byte index)
	{
		byte hh=EEPROM.read(AddressStep(index));
		byte ll=EEPROM.read(AddressStep(index) +1);
		return ((int)hh<<8)|ll;
	}

	void updateStep(byte index,int value)
	{
		EEPROM.update(AddressStep(index),(byte)(value >>8));
		EEPROM.update(AddressStep(index)+1,(byte)(value & 0xFF));
	}

	byte readBucketNumber(void)
	{
		byte value= EEPROM.read(AddressBucketNumber);
		
		Serial.print(F("readBucketNumber:"));
		Serial.println(value);

		return value;
	}
	
	void updateBucketNumber(byte value)
	{
		Serial.print(F("updateBucketNumber:"));
		Serial.println(value);
		EEPROM.update(AddressBucketNumber,value);
	}

	bool readAutoAnchor(void)
	{
		return EEPROM.read(AddressAutoAnchor);
	}

	void updateAutoAnchor(bool value)
	{

		EEPROM.update(AddressAutoAnchor,value);
	}

	int readSpinOver(void)
	{
		byte hh=EEPROM.read(AddressSpinOver);
		byte ll=EEPROM.read(AddressSpinOver +1);
		int value=((int)hh<<8)|ll;
		Serial.print(F("readSpinOver:"));
		Serial.println(value);

		return value;
	}

	void updateSpinOver(int value)
	{
		Serial.print(F("updateSpinOver:"));
		Serial.println(value);

		EEPROM.update(AddressSpinOver,(byte)(value >>8));
		EEPROM.update(AddressSpinOver+1,(byte)(value & 0xFF));
	}


	void checkNupdateSetting(void)
	{
		// read number of bucket, the reasonable value should be somewhere between
		// 4 & 8
		gBucketNumber=readBucketNumber();
		if(gBucketNumber < MIN_BUCKET_NUMBER || gBucketNumber > MAX_BUCKET_NUMBER)
		{
			initializeSettings();
			return;
		}
	}

};

PersistenceStorage eeprom;

//********************************************************************
//* Spinner
//********************************************************************

#define NUMBER_QUEUE 32

#define  ActionTypeForward 0
#define  ActionTypeBackward 1
#define  ActionTypeHold 2


typedef struct _SpinnerAction
{
	byte type;
	int  value;
}SpinnerAction;


class Spinner
{
private:
	NBStepper _stepper;
	byte _qStart;
	byte _qEnd;
	SpinnerAction _actions[NUMBER_QUEUE];
	unsigned long _holdingTimeOut;
	bool  _isFindingAnchor;

/*
	void debugQueue(void)
	{
			Serial.print(F("_qEnd="));
			Serial.print(_qEnd);
			Serial.print(F(" _qStart="));
			Serial.println(_qStart);
	}
*/	
public:
	bool isRunning(void)
	{
		if( (_qStart != _qEnd)
			|| _stepper.isRunning()
			|| _holdingTimeOut >= millis())
		{
				return true;
		}
		return false;
	}
	
	Spinner(void){
		pinMode (ANCHOR_PIN, INPUT_PULLUP);
		_qStart=0;
		_qEnd = 0;
		_isFindingAnchor=false;
		
		_stepper.attach(s1,s2,s3,s4);
  	}
	
	void addAction(byte type,int value)
	{
/*
		Serial.print("Action Queue Add:");
		Serial.print(type);
		Serial.print(" value:");
		Serial.println(value);
*/
//debugQueue();
		// check if full
		if( ((_qEnd + 1 + NUMBER_QUEUE - _qStart) % NUMBER_QUEUE) == 0)
		{
			// full return
			Serial.println(F("Queue Full!!"));
			return;
		}
		_actions[_qEnd].type=type;
		_actions[_qEnd].value = value;
		_qEnd = (_qEnd+1)% NUMBER_QUEUE;
	}
	
	void step(int s){
		//Serial.println("Rotate:>>");
		addAction(ActionTypeForward,s);
	}
	
	void backStep(int s){
		//Serial.println("Rotate:<<");
		addAction(ActionTypeBackward,s);
	}
	
	void nextHop(byte index)
	{
		int step=eeprom.readStep(index);
		int spinOver=eeprom.readSpinOver();
		
		if(spinOver)
		{
			addAction(ActionTypeForward, step+spinOver);
			addAction(ActionTypeHold, 1);
			addAction(ActionTypeBackward, spinOver);
		}
		else
		{
			addAction(ActionTypeForward, step);
			addAction(ActionTypeHold, 3);
		}
		
		Serial.print(F("Rotate to Hop Bucket#"));
		Serial.print(index);
		Serial.print(F(" steps:"));
		Serial.println(step);
	}
	
	void loop(void)
	{
		if(_isFindingAnchor)
		{
			if(!_stepper.isRunning())
			{
				if(anchored())
				{
					_isFindingAnchor=false;
				}
				else
				{
					//step more
					_stepper.step(1);
				}
			}
			return;
		}
		//else
		if( (_qStart != _qEnd)
			&& !_stepper.isRunning()
			&& _holdingTimeOut <= millis())
		{
			if(_actions[_qStart].type == ActionTypeForward)
			{
				if(eeprom.forwardCW())
					_stepper.setBackward();
				else
					_stepper.setForward();

				_stepper.step(_actions[_qStart].value);
			} 
			else if(_actions[_qStart].type == ActionTypeBackward)
			{
				if(eeprom.forwardCW())
					_stepper.setForward();
				else
					_stepper.setBackward();

				_stepper.step(_actions[_qStart].value);
			}
			else if(_actions[_qStart].type == ActionTypeHold)
			{
				_holdingTimeOut = millis() + (unsigned long) _actions[_qStart].value * 1000L;
			}

			_qStart = (_qStart+1)% NUMBER_QUEUE;
			//debugQueue();
		}
	}
	
	void setStepNumber(byte index,int num)
	{
		/*Serial.print(F("set Hop#"));
		Serial.print(index);
		Serial.print(F(" step number:"));
		Serial.println(num);*/
		
		eeprom.updateStep(index,num);
	}
	
	void findAnchor(void)
	{
		Serial.println(F("Rotate to anchor"));

		if(eeprom.forwardCW())
			_stepper.setBackward();
		else
			_stepper.setForward();

		_isFindingAnchor=true;
	}
	
	bool anchored(void)
	{
		return digitalRead(ANCHOR_PIN) == 0;
	}
};
Spinner spinner;
//********************************************************************
//* Menu
//********************************************************************

byte menuX[]={0,0,8,8};
byte menuY[]={0,1,0,1};
byte targetScreen[]={Edit_SCREEN,Run_SCREEN,Remote_SCREEN,Misc_SCREEN};

class MainMenu{
	private:
	short _currentIndex;
	
	void movePointer(short inc)
	{
		if((inc <0 &&_currentIndex > 0)
		   ||(inc > 0 && _currentIndex < 3))
		{
			lcd.setCursor(menuX[_currentIndex],menuY[_currentIndex]);
			lcd.write(' ');
			_currentIndex += inc; 
			lcd.setCursor(menuX[_currentIndex],menuY[_currentIndex]);
			lcd.write('>');
		}
	}
	
	public:
	
	MainMenu(void)
	{
		_currentIndex=0;
	}
	void show(void)
	{
		lcd.setCursor(menuX[0]+1,menuY[0]);
		lcd.print(F("Edit"));
		lcd.setCursor(menuX[1]+1,menuY[1]);
		lcd.print(F("Run"));
		lcd.setCursor(menuX[2]+1,menuY[2]);
		lcd.print(F("Remote"));
		lcd.setCursor(menuX[3]+1,menuY[3]);
		lcd.print(F("More.."));		
		lcd.setCursor(menuX[_currentIndex],menuY[_currentIndex]);
		lcd.write('>');
	}
	
	void loop(void)
	{
		RotaryEncoderStatus status=encoder.read();
  		switch(status)
  		{
    		case RotaryEncoderStatusPushed:
        		switchScreen(targetScreen[_currentIndex]);
        		break;

    		case RotaryEncoderStatusFordward:
        		movePointer(1);
        		break;

    		case RotaryEncoderStatusBackward:
        		movePointer(-1);
        		break;
		}  
	}
};

MainMenu mainMenu;

void mainMenuSetup(void)
{
	mainMenu.show();
}
void mainMenuLoop(void)
{
	mainMenu.loop();
}

//********************************************************************
//* Edit menu
//********************************************************************


byte _currentEditIndex;
byte _currentHopIndex;
bool _isAlarm;
bool _isEditingType;

unsigned long _blinkStartTime;
bool _labelShown;

//****************
//Boil Time 100min
//Hop #1    100min

void clearEditTitle(void)
{
	lcd.setCursor(2,1);
	lcd.print(F("        "));
	_labelShown=false;
}

void showEditTitle(void)
{
	lcd.setCursor(0,1);
	lcd.print(_currentEditIndex);
	lcd.write(':');
	if (_isAlarm) lcd.print(F("Alarm"));
	else 
	{
		lcd.print(F("Hop #"));
		lcd.print(_currentHopIndex);
	}
	_labelShown=true;
}

void printValue(byte value)
{
	lcd.setCursor(10,1);
	if(value <10)
	{
		lcd.print(F("  "));
	}
	else if(value <100)
	{
		lcd.write(' ');
	}
	lcd.print(value);
	lcd.write('m');
}
byte editingMin;
byte editingMax;
byte editingValue;

void initEditValue(byte index)
{
	if(index ==0)
	{
		editingMin=MIN_BOIL_TIME; // for test
		editingMax=MAX_BOIL_TIME;
	}
	else
	{
		editingMin=0;
		editingMax=eeprom.readTime(index -1); // previous value is maximum value of next
	}
	editingValue=eeprom.readTime(index);
	_isAlarm = eeprom.readType(index);
	
	if(editingValue>editingMax) editingValue=editingMax;
	if(editingValue<editingMin) editingValue=editingMin;
	
	_isEditingType=false;
	printValue(editingValue);
}

void toggleType(void)
{
	_isAlarm = !_isAlarm;
	if(_labelShown)
	{
		clearEditTitle();
		showEditTitle();
	}
}

void editInc(void)
{
	if(_isEditingType) 
	{
		toggleType();
	}
	else
	{
	if(editingValue<editingMax)
	{
		editingValue++;
		printValue(editingValue);
	}
	}
}

void editDec(void)
{
	if(_isEditingType) 
	{
		toggleType();
	}
	else
	{
	if(editingValue>editingMin)
	{
		editingValue--;
		printValue(editingValue);
	}
	}
}

void editEnter(void)
{

	if(_isEditingType) 
	{
		if(!_labelShown)
			showEditTitle();

		_isEditingType=false;
		return;
	}
	
	// clear edit row
	lcd.setCursor(0,1);
	lcd.print(F("                "));
	
	
	eeprom.updateTime(_currentEditIndex,editingValue);
	eeprom.updateType(_currentEditIndex,_isAlarm);

	if(editingValue && _currentEditIndex < MAX_EVENT_NUM)
	{
		_currentEditIndex++;
		if(!_isAlarm) _currentHopIndex ++;

		if(_currentHopIndex <= gBucketNumber)
			_isAlarm = false;
		else
			_isAlarm = true;

		initEditValue(_currentEditIndex);
		showEditTitle();
	}
	else
	{
		// end of enter
		switchScreen(MainMenu_SCREEN);		
	}
}
void editSetup(void)
{
	lcd.setCursor(0,0);
	lcd.print(F("Hop/Alarm Time"));
	lcd.setCursor(13,1);
	lcd.print(F("MIN"));

	_currentEditIndex=0;
	_currentHopIndex=1;

	lcd.setCursor(0,1);
	lcd.print(F("                "));
	lcd.setCursor(0,1);
	lcd.print(F("Boil Time"));

	initEditValue(0);
}


void editLoop(void)
{
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    		case RotaryEncoderStatusPushed:
    			editEnter();
        		break;

    		case RotaryEncoderStatusFordward:
        		editInc();
        		break;

    		case RotaryEncoderStatusBackward:
        		editDec();
        		break;
        
	   		case RotaryEncoderStatusLongPressed:
				_isEditingType = ! _isEditingType;
				if(_isEditingType)
				{
					_blinkStartTime=millis();
					clearEditTitle();
				}
	       		break; 

	}  
	if(_isEditingType)
	{
		unsigned long current=millis();
		if((current - _blinkStartTime) > 500)
		{
			if (_labelShown) clearEditTitle();
			else showEditTitle();
			_blinkStartTime = current;
		}
	}
}
//********************************************************************
//* Manual Spin
//********************************************************************

#define AccelTimeOut 300
#define SPIN_UI false

bool _clockwise;
#if SPIN_UI == true
short _col;
#endif

unsigned long _lastTimeSpin;

int _spinCount;

void resetSpinCount(void)
{
	_spinCount=0;
}

int getSpinCount(void)
{
	return _spinCount;
}

void spinInit(void)
{
	_spinCount=0;
	_clockwise=true;
#if SPIN_UI == true
	_col=0;
	lcd.setCursor(0,1);
#endif
}

void spinRun(bool ff)
{
	unsigned long current;
	byte steps=1;
	current=millis();

	if(_clockwise == ff) // the same direction
	{
		// previous clockwise, keep the same check last time
		if((current - _lastTimeSpin) <AccelTimeOut)
		{
			// 
			steps=5;
		}
	}
	_clockwise=ff;
    _lastTimeSpin = current;
	
#if SPIN_UI == true	
	if(_clockwise != ff || 
		(ff &&  _col > 15) ||
		(!ff &&  _col < 0))
		
    {
    	// different direction, or col limit meets
    	lcd.setCursor(0,1);
    	lcd.print(F("                "));
    		
    	if(ff)
    	{
    		lcd.setCursor(0,1);
    		_col=0;
    	}
    	else
    	{
    		//lcd.setCursor(15,1);
    		_col=15;
    	}
    }
    
    if(ff)
    {
    	lcd.print(F(">"));
    	_col++;
    }
    else
    {
    	lcd.setCursor(_col,1);
    	lcd.print(F("<"));
    	_col--;
    }	
#endif
     
	if(ff)
	{
	    spinner.step(steps);
	    _spinCount += steps;
	}
	else
	{
		spinner.backStep(steps);
		 _spinCount -= steps;
	}
}

bool spinAction(RotaryEncoderStatus status)
{
	bool turn=false;
 	switch(status)
  	{
    	case RotaryEncoderStatusFordward:
        	spinRun(true);
        	turn=true;
        	break;

    	case RotaryEncoderStatusBackward:
        	spinRun(false);
        	turn=true;
        	break;
	}  
	return turn;
}
//********************************************************************
//* anchoring
//********************************************************************
bool _continueMoving;

void initAnchoring(void)
{
	lcd.setCursor(0,1);

	if(eeprom.readAutoAnchor())
	{
		lcd.print(F("Finding Anchor.."));
		spinner.findAnchor();
	}
	else
	{
		lcd.print(F("Push to Stop"));
		_continueMoving=true;
		spinner.step(1);
	}

}

bool findAnchorDone(void)
{
	if(eeprom.readAutoAnchor())
	{
		if(spinner.anchored())
		{
			return true;
		}
	}
	else
	{
		RotaryEncoderStatus status=encoder.read();
		if(_continueMoving)
		{
			if(status == RotaryEncoderStatusPushed)
			{
				_continueMoving=false;
				lcd.setCursor(0,1);
				lcd.print(F("Turn to Anchor"));
				spinInit();
			}
			else
			{
				if(!spinner.isRunning())
				{
					spinner.step(1);
				}
			}
		}
		else
		{
  			switch(status)
  			{
    			case RotaryEncoderStatusPushed:
    				return true;
        			break;
        		default:
        			spinAction(status);
        			break;
			}
		}
	}
	
	return false;
}
//********************************************************************
//* Run
//********************************************************************
void printTime(char* buf,unsigned long value)
{
	unsigned long minute =(value)/60;
    unsigned long hmin =minute/100;
    unsigned long dmin =minute - hmin * 100;


    if(minute >99)
    {        
    	buf[0]='0' + hmin;
	}
	else
	{
    	buf[0]=' ';
    }
	buf[1]= (char)((dmin/10) + '0');
	buf[2]= (char)((dmin%10) + '0');
	buf[3]='m';
    
    unsigned long seconds=value -  minute*60;
	buf[4]=(char)((seconds/10) + '0');
	buf[5]=(char)((seconds%10) + '0');

    buf[6]='\0';
}

//[   Next #5 00m00]
//[100m00s #6 00m00]

#define RunningStateAnchoring 0
#define RunningStateWaitBoiling 1
#define RunningStateRunning 2

byte _runningState;

byte _nextHop;
//bool _isAlarm;
byte _currentEventIndex;

bool _finishedHop;
bool _finishedBoil;

unsigned long _latestUpdateSecond;
unsigned long _startTime;
unsigned long _boilCountDown;
unsigned long _firstCountDown;
unsigned long _secondCountDown;

void runSetup(void)
{
	_runningState=RunningStateAnchoring;
	_finishedHop=false;
	_finishedBoil=false;

	lcd.setCursor(0,0);
	lcd.print(F("Prepare to Run"));

	initAnchoring();
}

void alarm(void)
{
	Serial.println("Alarm.. ");
	buzzer.playCustom(eventAlarm,false);
}

void spinHop(byte index)
{
	Serial.print("Time's up for ");
	Serial.println(index);
	buzzer.playCustom(hopAlarm,false);
	spinner.nextHop(index);
}

void shortLabel(byte line, byte index, byte hopIndex)
{
	bool alarm=eeprom.readType(index);	
	
	lcd.setCursor(8,line);
	if(alarm)
	{
		lcd.write('!');
		lcd.write(' ');
	}
	else
	{
		lcd.write('#');
		lcd.print(hopIndex);
	}
}

void loadHop(void)
{
	do{
	
		if(_currentEventIndex > MAX_EVENT_NUM)
		{
			_firstCountDown=0;
			_finishedHop = true;
		}
		else
		{
		byte t=eeprom.readTime(_currentEventIndex);
		_isAlarm=eeprom.readType(_currentEventIndex);
	
/*		
		Serial.print("loadHop ");
		Serial.print(_currentEventIndex);
		Serial.print(" value:");
		Serial.print(t);
		Serial.print(" alarm:");
		Serial.print(_isAlarm);
		Serial.print(" _nextHop:");
		Serial.println(_nextHop);
*/	
		if(t==0)
		{
			_firstCountDown =0;
			_finishedHop = true;
		}
		else
		{
			shortLabel(0,_currentEventIndex,_nextHop);

			_firstCountDown = _boilCountDown - ((unsigned long)t) * 60;

			byte t2=eeprom.readTime(_currentEventIndex+1);
			if(t2 ==0) _secondCountDown =0;
			else 
			{
				//label
				if(_isAlarm)
					shortLabel(1,_currentEventIndex+1, _nextHop);
				else
					shortLabel(1,_currentEventIndex+1, _nextHop+1);

				_secondCountDown=_boilCountDown - ((unsigned long)t2) * 60;
			}
		}
		Serial.print("_firstCountDown:");
		Serial.println(_firstCountDown);
    	if(	_firstCountDown ==0) 
    	{
    		if(!_isAlarm)
    		{ 
    			Serial.println("loadHop spin");
	    		spinHop(_nextHop);
    			_nextHop++;
    		}
    		else
    		{
    			alarm();
    		}
    		_currentEventIndex++;
    	}
    	}
 	}while(_firstCountDown==0 && !_finishedHop);
}


void updateTimeLabel(void)
{
	char buf[8];
	printTime(buf,_boilCountDown);
	lcd.setCursor(0,1);
	lcd.print(buf);
	
/*	Serial.print("update first:");
	Serial.print(_firstCountDown);
	Serial.print(" second:");
	Serial.println(_secondCountDown);*/
	
	
	if(_firstCountDown ==0)
	{
		// end
		lcd.setCursor(8,0);
		lcd.print(F(" - END -"));
		lcd.setCursor(8,1);
		lcd.print(F("        "));
	}
	else
	{
		lcd.setCursor(10,0);
		printTime(buf,_firstCountDown);
		lcd.print(buf);
		if(_secondCountDown ==0)
		{
			lcd.setCursor(8,1);
			lcd.print(F(" - END -"));
		}
		else
		{
			lcd.setCursor(10,1);
			printTime(buf,_secondCountDown);
			lcd.print(buf);
		}
	}
}

void setupBoiling(void)
{
	lcd.clear();
	lcd.setCursor(3,0);
	lcd.print(F("Next"));
	lcd.setCursor(6,1);
	lcd.print(F("s"));
	_startTime= millis();
	_latestUpdateSecond=0;
	_nextHop=1;
	_currentEventIndex=1;
	
	_boilCountDown=(unsigned long)eeprom.readTime(0) * 60;
	loadHop();
	updateTimeLabel();
}

void setupWaitBoiling(void)
{
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.print(F("Wait Boiling..."));
	lcd.setCursor(0,1);
	lcd.print(F("Push to run!"));
}

void postionLoop(void)
{
	if(findAnchorDone())
	{
    	setupWaitBoiling();
    	_runningState=RunningStateWaitBoiling;
	}
}

void waitBoiling(void)
{
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    	case RotaryEncoderStatusPushed:
    		setupBoiling();
    		_runningState=RunningStateRunning;
        	break;
        default:
        	break;
	}
}


void boilingLoop(void)
{
	// input
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    	case RotaryEncoderStatusLongPressed:
    		switchScreen(MainMenu_SCREEN);
        	break;
	}

	if(_finishedBoil) return;
	
	unsigned long now=millis();
	unsigned long seconds= (now - _startTime)/1000;
	if(_latestUpdateSecond != seconds)
	{
		unsigned long diff=seconds - _latestUpdateSecond;
		_latestUpdateSecond = seconds;
		if(_boilCountDown > diff)
		{
			_boilCountDown -= diff;
		}
		else
		{
			_boilCountDown=0;
			//TODO: buzz
			_finishedBoil=true;
		}
		if(!_finishedHop)
		{
			if(_firstCountDown > diff)
			{
				_firstCountDown -= diff;
			
				if(_secondCountDown !=0)
				{
					_secondCountDown -= diff;
				}
			}
			else
			{
				if(!_isAlarm)
				{
					Serial.println("runningLoop spin");
					spinHop(_nextHop);
					_nextHop++;
				}
				else
				{
					alarm();
				}

				_firstCountDown=0; 
				// buzz, load next one 
				if(_secondCountDown >0) // has next ,load
				{
					_currentEventIndex++;
					
					loadHop();
				}
				else
				{
					_finishedHop=true;
				}
			}
		}
		updateTimeLabel();
	}

}

void runLoop(void)
{
	if(_runningState == RunningStateAnchoring)
	{
		postionLoop();
		
	}
	else if(_runningState == RunningStateWaitBoiling)
	{
		waitBoiling();
	}
	else
	{
		boilingLoop();
	}
}
//********************************************************************
//* Setting
//********************************************************************
void remoteSetup(void)
{
	lcd.setCursor(0,0);
	lcd.print(F("Remote control"));
	lcd.setCursor(0,1);
	lcd.print(F("to be implemented"));
}

void remoteLoop(void)
{
	RotaryEncoderStatus status=encoder.read();
  switch(status)
  {
    case RotaryEncoderStatusPushed:
        switchScreen(MainMenu_SCREEN);
        break;        
    default:
      break;

	}  

}

//********************************************************************
//* Misc/More/settings
//********************************************************************

const byte menuId[]={TestSpin_SCREEN,SetSpinner_SCREEN,ManualSpin_SCREEN,Options_SCREEN,MainMenu_SCREEN};
const char* menuStrings[]={"Test Spinner","Set Position","Manual Spin","Settings","Back"};
byte _currentMenuIndex;

void showMenuItem(void)
{
	//Serial.println(_currentMenuIndex);
	lcd.setCursor(1,1);
	lcd.print(F("               "));
	lcd.setCursor(1,1);
	lcd.print(menuStrings[_currentMenuIndex]);
}

void miscSetup(void)
{
	_currentMenuIndex=0;
	lcd.setCursor(0,0);
	lcd.print(F("Settings/Options"));
	lcd.setCursor(0,1);
	lcd.print(F(">"));
	showMenuItem();
}

void miscLoop(void)
{
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    	case RotaryEncoderStatusPushed:
       		switchScreen(menuId[_currentMenuIndex]);
       	 	break;        

    	case RotaryEncoderStatusFordward:
        	if(_currentMenuIndex < (sizeof(menuId)/sizeof(byte)-1))
        	{
        		_currentMenuIndex++;
        		showMenuItem();
        	}
        	break;

    	case RotaryEncoderStatusBackward:
        	if(_currentMenuIndex>0)
        	{
        		_currentMenuIndex--;
        		showMenuItem();
        	}
        	break;
    	
    	default:
     	 	break;

	}  
}

//********************************************************************
//* manualSpin
//********************************************************************
void manualSpinSetup(void)
{
	lcd.setCursor(0,0);
	lcd.print(F("Manual Spin"));
	lcd.setCursor(0,1);
	lcd.print(F("Step:0"));
	spinInit();
}

void manualSpinLoop(void)
{
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    	case RotaryEncoderStatusPushed:
       		switchScreen(Misc_SCREEN);
       	 	break;        
		default:
			 if(spinAction(status))
			 {
			 	lcd.setCursor(5,1);
			 	lcd.print(F("      "));
			 	lcd.setCursor(5,1);
			 	lcd.print(getSpinCount());
			 }
			 break;
	}
}

//********************************************************************
//* testSpin
//********************************************************************
byte _step;
bool _promptAction;

void testSpinPrompt(void)
{
	lcd.setCursor(0,1);
	if(_step > gBucketNumber)
	{
		lcd.print(F("Push to Exit    "));
	}
	else
	{
		if(_promptAction)
		{
			lcd.print(F("Push to test #"));
			lcd.print(_step);
		}
		else
		{
			lcd.print(F("                "));
			lcd.setCursor(0,1);
			lcd.print(F("Adding Hop#"));
			lcd.print(_step);			
		}
	}
}

void testSpinSetup(void)
{
	lcd.setCursor(0,0);
	lcd.print(F("Test Spin"));
	_step=0;
	initAnchoring();
}

void testSpinLoop(void)
{
	if(_step ==0)
	{
		if(findAnchorDone())
		{
			 _step++;
    		_promptAction=true;
			 testSpinPrompt();
		}
		return;
	}	
	// else// _step >0
	
	RotaryEncoderStatus status=encoder.read();

	if(spinner.isRunning())
	{
		//ignore input during running.
		return;
	}
	else if(_promptAction == false)
	{
		_promptAction = true;
    	_step++;
		testSpinPrompt();
	}
	
  	switch(status)
  	{
    	case RotaryEncoderStatusPushed:
    		if(_step > gBucketNumber)
    		{
	    		switchScreen(Misc_SCREEN);
    		}
    		else
    		{
    			// step to next
    			spinner.nextHop(_step);
				_promptAction=false;
    			testSpinPrompt();
    		}
       	 	break;        
	}
}
//********************************************************************
//* setSpinner
//********************************************************************
unsigned long _stepNumber;
void setSpinnerLabel(void)
{
	lcd.setCursor(0,1);

	if(_step > gBucketNumber)
	{
		lcd.print(F("   Finished!    "));
	}
	else
	{	
		lcd.print(F("Bucket#"));
		lcd.print(_step);
		lcd.print(F("        "));
	}
}

void setSpinnerSetup(void){
	_step=0;
	_stepNumber=0;

	lcd.setCursor(0,0);
	lcd.print(F("Set Positions"));
	
	initAnchoring();
}

void setSpinnerLoop(void){
	if(_step==0)
	{
		if(findAnchorDone())
		{
				_step ++;
				setSpinnerLabel();
				spinInit();
		}		
		return;
	}
	// else
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    	case RotaryEncoderStatusPushed:
    		if(_step > gBucketNumber)
    		{
	    		switchScreen(Misc_SCREEN);
    		}
    		else
    		{
    			spinner.setStepNumber(_step,getSpinCount());
    			resetSpinCount();
    			// step to next
    			_step++;
    			setSpinnerLabel();
    		}
       	 	break;

		default:
			 if(spinAction(status))
			 {
			 	lcd.setCursor(9,1);
			 	lcd.print(F(":       "));
			 	lcd.setCursor(10,1);
			 	lcd.print(getSpinCount());
			 }
			 break;
	}
}
//********************************************************************
//* options
//********************************************************************
// 1. bucket numebr:  4~8
// 2. auto anchor?  yes/no
// 3. spin over     0-?

byte _optionItem;
byte _valuePos;
int _optionMaxValue;
int _optionMinValue;
int _value;

void printValue(void)
{
	lcd.setCursor(_valuePos,1);
	lcd.print(F("    "));
	lcd.setCursor(_valuePos,1);
	if(_optionItem == 1)
	{
		if(_value==0) lcd.print(F("NO"));
		else lcd.print(F("YES"));
	}
	else if(_optionItem == 3)
	{
		if(_value==0) lcd.print(F("CCW"));
		else lcd.print(F("CW"));
	}
	else
	{
		lcd.print(_value);
	}
}

void changeValue(int change)
{
	_value += change;
	if(_value > _optionMaxValue) _value=_optionMaxValue;
	if(_value < _optionMinValue) _value=_optionMinValue;
	printValue();
}

void updateValue(void)
{
	if(_optionItem == 0)
	{
		eeprom.updateBucketNumber((byte)_value);
		gBucketNumber=(byte)_value;
	}
	else if(_optionItem == 1)
	{
		eeprom.updateAutoAnchor((byte)_value);
	}
	else if(_optionItem == 2)
	{
		eeprom.updateSpinOver(_value);
	}
	else if(_optionItem == 3)
	{
		eeprom.updateForwardCW((bool)_value);
	}

}

void optionLabel(void)
{
	lcd.setCursor(0,1);
	lcd.print(F("                "));
	lcd.setCursor(0,1);
	if(_optionItem == 0)
	{
		lcd.print(F("Bucket Number:"));
		_valuePos=14;
		
		_optionMaxValue=8;
		_optionMinValue=4;
		_value=(int)eeprom.readBucketNumber();
	}
	else if(_optionItem == 1)
	{
		lcd.print(F("Auto Anchor:"));
		_valuePos=12;
		
		_optionMaxValue=1;
		_optionMinValue=0;
		_value=(int)eeprom.readAutoAnchor();
	}
	else if(_optionItem == 2)
	{
		lcd.print(F("Spin Over:"));
		_valuePos=10;
		
		_optionMaxValue=MAX_SPIN_OVER;
		_optionMinValue=0;
		_value=eeprom.readSpinOver();
	}
	else if(_optionItem == 3)
	{
		lcd.print(F("FW Rotate:"));
		_valuePos=11;
		
		_optionMaxValue=1;
		_optionMinValue=0;
		_value=(int)eeprom.forwardCW();
	}

}

void optionsSetup(void)
{
	lcd.setCursor(0,0);
	lcd.print(F("Options/Settings"));

	_optionItem=0;
	optionLabel();
	changeValue(0);
}

void optionsLoop(void)
{
	RotaryEncoderStatus status=encoder.read();
  	switch(status)
  	{
    	case RotaryEncoderStatusPushed:
        	updateValue();
        	_optionItem++;
        	if(_optionItem >3)
        	{
        		// finish
        		switchScreen(Misc_SCREEN);
        	}
        	else
        	{
				optionLabel();
				changeValue(0);
        	}
        	break;
	
	    case RotaryEncoderStatusFordward:
	    	changeValue(1);
	        break;

    	case RotaryEncoderStatusBackward:
    		changeValue(-1);
        	break;
	}
}

//********************************************************************
//* Switching Screen
//********************************************************************
const CScreen _screens[]  =
{
	{&mainMenuSetup,&mainMenuLoop},
	{&editSetup,&editLoop},
	{&runSetup,&runLoop},
	{&remoteSetup,&remoteLoop},
	{&miscSetup,&miscLoop},
	{&manualSpinSetup,&manualSpinLoop},
	{&testSpinSetup,&testSpinLoop},
	{&setSpinnerSetup,&setSpinnerLoop},
	{&optionsSetup,&optionsLoop}
};

LoopFunc _currentLoop;

void switchScreen(byte sid)
{
	CScreen *screen=(CScreen *)_screens+sid;

	lcd.clear();
	
	(* screen->setup)();
	_currentLoop=screen->loop;
}

//********************************************************************
//* Main program
//********************************************************************
#if PROFILING == true
// for profiling
unsigned long lasttime;
unsigned long minTime;
unsigned long maxTime;
unsigned long count;

void profileInit(void)
{
  	minTime=0xFFFFFF; maxTime=0; count=0;
	lasttime=millis();
}

void profile(void)
{
	unsigned long current=millis();
	unsigned long dif=current - lasttime;
	lasttime = current;
	if(minTime > dif) minTime=dif;
	if(maxTime < dif) maxTime=dif;
	count ++;
	
	if(count ==1000)
	{
		count=0;
		Serial.print("min:");
		Serial.print(minTime);
		Serial.print("max:");
		Serial.println(maxTime);
	}
}
#endif
void hello(void)
{
	lcd.setCursor(1,0);
	lcd.print(F("LazySusan 0.1a"));
	lcd.setCursor(1,1);
	lcd.print(F("Citra, Tapaz"));
	delay(1500);
	lcd.setCursor(0,1);
	lcd.print(F("     ,or Merkur?"));
	delay(1200);
	lcd.clear();
	
}

void setup() {
  // put your setup code here, to run once:
  	Serial.begin(115200);
  	lcd.begin(20,4);
  	
  	hello();
  	
  	eeprom.checkNupdateSetting();
  	
  	switchScreen(MainMenu_SCREEN);
#if PROFILING == true
  	profileInit();
#endif

	buzzer.longBeep();
}


void loop() {

  // put your main code here, to run repeatedly:  
  spinner.loop();
  buzzer.loop();

  (*_currentLoop)();
#if PROFILING == true  
  profile();
#endif
}
