#ifndef NBStepper_h
#define NBStepper_h



class NBStepper
{
public:
    NBStepper();
	// attach
	void attach(int pin1, int pin2, int pin3, int pin4);
  	void step(unsigned long round);
  	void microStep(unsigned long steps);
  	
  	void setForward(void);
  	void setBackward(void);
  	void setFull(bool full);
  	void setRpm(int rpm);
  	bool isRunning(void);
  	bool isForward(void);
  	bool isFull(void);
  	
  	void setMicroStepTime(int mul);
  	
  	void debug(void);
  	
private:
	void _setupTimer(void);
};

#endif