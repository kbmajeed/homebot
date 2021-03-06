
#include "Wire.h"
#include "Arduino.h"

#include "I2CAddresses.h"
#include "Sensor.h"
#include "Commands.h"

#define FULL_SPEED      255
#define HALF_SPEED      128
#define QUARTER_SPEED   64

#define COMMOTION_REQUEST_ENCODER B00000001
#define COMMOTION_REQUEST_CURRENT B00000100
#define COMMOTION_REQUEST_BATTERY B00001000
#define COMMOTION_REQUEST_ANALOG2 B00010000
#define COMMOTION_REQUEST_ERRORLOG B00100000
#define COMMOTION_STATUS_UPDATE_FREQ 50 // milliseconds
#define COMMOTION_ENCODER_FREQ 100 // milliseconds

// Our motors and encoders are only connected to M3/M4, which are controlled by IC2
// so save time by talking directly to IC2.
#define DEFAULT_COMMOTION_ADDR COMMOTION_ADDR2

#define MOVEMENT_COMPLETE 0
#define MOVEMENT_ACTIVE 1

// ComMotion variables

byte read_byte(){
	while(Wire.available() < 1){}
	return Wire.read();
}
/*
uint16_t receive_wire_uint16(){

	uint16_t rxnum = 0;

    // Read low byte into rxnum
	rxnum += read_byte();

    // Read high byte into rxnum
    rxnum += read_byte() << 8;

    return rxnum;
}
*/
int16_t receive_wire_int16(){

	int16_t rxnum = 0;

    // Read low byte into rxnum
	rxnum += read_byte();

    // Read high byte into rxnum
    rxnum += read_byte() << 8;

    return rxnum;
}

void set_basic_config(byte mode, byte chassis, byte lowbat, byte maxcur1, byte maxcur2, byte maxcur3, byte maxcur4, byte i2coffset, byte i2cmaster)
{
//Serial.println(String("A: connecting to ")+String(DEFAULT_COMMOTION_ADDR));Serial.flush();
    Wire.beginTransmission(DEFAULT_COMMOTION_ADDR);// Initialize I2C communications with ComMotion shield
//Serial.println(String("B"));Serial.flush();
    Wire.write(CMD_SET_BASIC_CONFIG);// Specify that data packet is basic configuration data
//Serial.println(String("C"));Serial.flush();
    Wire.write(mode);                 // 0=normal, 1=demo
//Serial.println(String("D"));Serial.flush();
    Wire.write(chassis);              // 0=3xOmni, 1=4xOmni, 2=Mecanum, 3=Individual. Add 16 to disable encoder feedback
//Serial.println(String("E"));Serial.flush();
    Wire.write(lowbat);               // 0-255    55=5.5V
//Serial.println(String("F"));Serial.flush();
    Wire.write(maxcur1);              // 0-255   255=2.55A
    Wire.write(maxcur2);              // 0-255   255=2.55A
    Wire.write(maxcur3);              // 0-255   255=2.55A
    Wire.write(maxcur4);              // 0-255   255=2.55A
    Wire.write(i2coffset);            // 0-95    a value of 32 changes ComMotion dipswitch address range to 32-63
    Wire.write(i2cmaster);            // I2C address for I2C master - used for emergency reports such as motor overload
//Serial.println(String("F2"));Serial.flush();
    Wire.endTransmission();           // transmit data from I2C buffer to ComMotion shield
//Serial.println(String("G"));Serial.flush();
}

// Encoder Configuration, only need to send once
void set_encoder_config(int maxrpm, int encres, byte reserve, byte maxstall)
{
    Wire.beginTransmission(DEFAULT_COMMOTION_ADDR); // Initialize I2C communications with ComMotion shield
    Wire.write(CMD_SET_ENCODER_CONFIG);   // Specify that data packet is encoder configuration data
    Wire.write(highByte(maxrpm));       // high byte of motor RPM - 13500rpm for Scamper
    Wire.write( lowByte(maxrpm));       //  low byte of motor RPM - 8500rpm for Rover 5
    Wire.write(highByte(encres));       // high byte of encoder resolution x100 - 800 for Scamper
    Wire.write( lowByte(encres));       //  low byte of encoder resolution x100 - 200 for Rover 5
    Wire.write(reserve);                // 0-50%     reserve power - use when constant speed under variable load is critical
    Wire.write(maxstall);               // 1-255������S   number of ������S between encoder pulses before stall is assumed  10 for Scamper, 25 for Rover 5
    Wire.endTransmission();             // transmit data from I2C buffer to ComMotion shield
}

void set_serial_config(int baud1, int baud2, byte smode)
{
    Wire.beginTransmission(DEFAULT_COMMOTION_ADDR);                    // Initialize I2C communications with ComMotion shield
    Wire.write(CMD_SET_SERIAL_CONFIG);                             // Specify that data packet is serial configuration data
    Wire.write(highByte(baud1));               // Baud rate for ComMotion serial port 1
    Wire.write( lowByte(baud1));               // Common baud rates are: 1200,2400,4800,9600,14400,28800,38400,57600,115200
    Wire.write(highByte(baud2));               // Baud rate for ComMotion serial port 2 (Xbee / WiFly)
    Wire.write( lowByte(baud2));               // Common baud rates are: 1200,2400,4800,9600,14400,28800,38400,57600,115200
    Wire.write(smode);                         // 0-4  Determines how ComMotion shield handles incoming data (default=4)
    Wire.endTransmission();                    // transmit data from I2C buffer to ComMotion shield
}
/*
void request_commotion_status(byte mcu, byte req) // value for mcu must be 0 or 1. Each bit of the request byte is a separate request.
{
    request = req;
    requested_status_count += 1;
    Wire.beginTransmission(DEFAULT_COMMOTION_ADDR + mcu);            // Initialize I2C communications with ComMotion shield MCU 1 or 2
    Wire.write(6);                             // Specify that data packet is a status request
    Wire.write(req);                       // Each bit of the request byte returns a different status report
    Wire.endTransmission();                    // transmit data from I2C buffer to ComMotion shield
}
*/
//---------------------------------------------------------------------------- Individual Motor Control ------------------------------------------------------------------------------------

void set_motor_speeds(int m1, int m2, int m3, int m4)
{
	// Note, the address shouldn't really matter, since the ComMotion forwards commands from one MCU to the other,
	// but the motors are connected to M3 and M4, which are controlled by IC2.
    Wire.beginTransmission(COMMOTION_ADDR2);                    // Initialize I2C communications with ComMotion shield
    Wire.write(CMD_SET_MOTOR_CONFIG);                                                            // Specify that data packet is motor control data
    Wire.write(highByte(m1));                                                 // -255 to +255  speed for motor 1
    Wire.write( lowByte(m1));                                                 // negative values reverse direction of travel
    Wire.write(highByte(m2));                                                 // -255 to +255  speed for motor 2
    Wire.write( lowByte(m2));                                                 // negative values reverse direction of travel
    Wire.write(highByte(m3));                                                 // -255 to +255  speed for motor 3
    Wire.write( lowByte(m3));                                                 // negative values reverse direction of travel
    Wire.write(highByte(m4));                                                 // -255 to +255  speed for motor 4
    Wire.write( lowByte(m4));                                                 // negative values reverse direction of travel
    Wire.endTransmission();                                                   // transmit data from I2C buffer to ComMotion shield
}

class SpeedController
{
    private:
    
        // The actual speed we're telling the motor.
        // This takes into account acceleration.
        int current_speed;
        
        // The desired speed.
        int target_speed;
        
        // The time when the target speed was last set.
        // Used to time acceleration.
        unsigned long target_last_set;
        
        // The amount of velocity to add every second.
        // Pro-rated to milliseconds.
        unsigned int acceleration = 128; // takes 2 seconds to go from 0 to FULL_SPEED
        
        // timer used so status updates do not flood I2C bus
        unsigned long stime;
    
    public:
    
        SpeedController(){
            current_speed = 0;
            target_speed = 0;
            target_last_set = millis();
        }
        
        void set_speed(int s){
            if(s > FULL_SPEED){
                s = FULL_SPEED;
            }else if(s < -FULL_SPEED){
                s = -FULL_SPEED;
            }

            // If we haven't reached target before change, then save partial target
            // as a start for the new calculation.
            if(target_speed != current_speed){
            	current_speed = get_speed();
            }

            target_speed = s;
            target_last_set = millis();
        }
        
        void set_acceleration(unsigned int a){
            acceleration = a;
        }
        
        unsigned int get_acceleration(){
            return acceleration;
        }

        // Get the speed incrementally set over a period of time to reach the target speed.
        int get_speed(){
            int ret_speed = current_speed;
            unsigned long time_since_set = millis() - target_last_set;
            int speed_difference = abs(target_speed - current_speed);
            if(speed_difference){

            	// Calculate the maximum amount of time it should take to accelerate
            	// to the target speed.
                unsigned long time_to_change = float(speed_difference)/float(acceleration)*1000;

                if(time_since_set >= time_to_change){
                    // After acceleration was calculated to reach the target speed,
                    // stop updating and just set the value.
                    current_speed = target_speed;
                    ret_speed = current_speed;
                }else{
                    // Otherwise, calculate the gradual speed change.
                	// We don't save this change and instead calculate it from the original
                	// current_speed so as to not introduce rounding errors.
                	// Once we achieve target speed, we'll save the value, and this function will
                	// then become a glorified getattr around current_speed.
                    int polarity = (target_speed > current_speed) ? +1 : -1;
                    int vel_change = time_since_set * (1/1000.) * acceleration * polarity;
                    if(polarity > 0){
                        ret_speed = min(current_speed + vel_change, target_speed);
                    }else{
                        ret_speed = max(current_speed + vel_change, target_speed);
                    }
                }
            }
            return ret_speed;
        }
        
        // Simply returns the target speed, even if we've not yet fully accelerated.
        int get_speed_instantly(){
            current_speed = target_speed;
            return target_speed;
        }
        
};

class MotionController: public Sensor
{

    private:
            
        SpeedController _motor_left;

        SpeedController _motor_right;
        
        unsigned long _last_encoder_check;

        // Movement parameters.
    	float _movement_linear = 0;
    	float _movement_angular = 0;
    	float _movement_seconds = 0;
    	int _movement_force = 0; // 0=stop if error encountered, 1=ignore errors
    	int _movement_error_code = 0;
    	bool _movement_encoder_changed = false;
    	bool _movement_send_done = false;
    	unsigned long _movement_start_millis = 0;
    	unsigned int _movement_status = MOVEMENT_COMPLETE;

    public:

        //int acount = 0;
        ChangeTracker<int> a_encoder = ChangeTracker<int>(0);

        //int bcount = 0;
        ChangeTracker<int> b_encoder = ChangeTracker<int>(0);

        ChangeTracker<byte> eflag = ChangeTracker<byte>(0);

        ChangeTracker<bool> connected = ChangeTracker<bool>(false);

        int aspeed = 0;

		int bspeed = 0;

		unsigned long checks = 0;
    
        MotionController(){
        }
        
        bool is_connected(){
            return connected.get_latest();
        }
        
        void connect(){
            Wire.beginTransmission(DEFAULT_COMMOTION_ADDR);
            int error = Wire.endTransmission();
            if(!error){
                connected.set(true);
            }else{
            	connected.set(false);
            }
            if(is_connected()){
				// Normal mode, Rover 5 with mecanum wheels, lowbat = 6V, motor currents =2.5A, no offset, Master address=1;
				//Serial.println(String(F("set_basic_config()"));Serial.flush();
				set_basic_config(
					0, //mode, 0=normal
					CHASSIS_CONFIG_INDIVIDUAL, //chassis, 3=individual
					60, //lowbat, 0-255    55=5.5V (given 6V battery, 80%=4.8V=dead, 92%=5.5V=low)
					250, //maxcur1, 0-255   255=2.55A (left)
					250, //maxcur2, 0-255   255=2.55A (right)
					250, //maxcur3, 0-255   255=2.55A (unused)
					250, //maxcur4, 0-255   255=2.55A (unused)
					0, //i2coffset
					TORSO_ARDUINO_ADDR //1 //i2cmaster
				);

				// Max motor rpm = 8500rpm, encoder resolution = 2.00 state changes per motor revolution, 10% reserve power, stall at 25uS
				//Serial.println(String(F("set_encoder_config()")));Serial.flush();
				set_encoder_config(
					8500, //maxrpm=maximum rpms
					200, //encres=encoder resolution
					//600, //maxrpm=maximum rpms, Pololu Metal Gearmotor 2282
					//465, //encres=encoder resolution, Pololu Metal Gearmotor 2282
					10, //reserve=0-50%     reserve power - use when constant speed under variable load is critical
					25 //maxstall=1-255mS   number of milli-seconds between encoder pulses before stall is assumed  10 for Scamper, 25 for Rover 5
				);
            }
        }

        void init(){

        	_last_encoder_check = millis();

            _motor_left = SpeedController();
            _motor_right = SpeedController();
            
            // Do this in main.ino setup()
            //Wire.onReceive(handle_commotion_status_response);

            reset_movement();

            connect();

        }
        
        void reset_movement(){
            _movement_linear = 0;
            _movement_angular = 0;
            _movement_seconds = 0;
            _movement_force = 0;
            _movement_status = MOVEMENT_COMPLETE;
            _movement_encoder_changed = false;
        }

        void set(int left_speed, int right_speed){
            if(!is_connected()){
                return;
            }

            _motor_left.set_speed(left_speed);
            _motor_right.set_speed(right_speed);
            //NA NA left right
            set_motor_speeds(0, 0, _motor_left.get_speed(), _motor_right.get_speed());
        }

    	void set_movement(float linear, float angular, float seconds, int force){
    		reset_movement();
    		_movement_linear = linear; // meter/second
    		_movement_angular = angular; // radians/second
    		_movement_seconds = seconds;
    		_movement_force = force;
    		_movement_start_millis = millis();
    		_movement_status = MOVEMENT_ACTIVE;
    		_movement_send_done = false;
    		float left_speed_out = (linear - angular*TORSO_TREAD_WIDTH_METERS/2) * VELOCITY_TO_SPEED;
    		float right_speed_out = (linear + angular*TORSO_TREAD_WIDTH_METERS/2) * VELOCITY_TO_SPEED;
    		set(left_speed_out, right_speed_out);
    	}

    	bool is_executing_movement(){
    		return MOVEMENT_ACTIVE == _movement_status;
    	}

    	bool is_checking_movement_error(){
    		return !_movement_force;
    	}

    	bool is_encoder_stalled(){
    		// Motors/encoders are considered stalled if we should be moving but haven't received an encoder update in 500ms.
    		return !_movement_encoder_changed && (millis() - _movement_start_millis) > 500;
    	}

    	void end_movement(int error_code){

    		reset_movement();

            // Queue a movement completion status report.
            _movement_send_done = true;
            _movement_error_code = error_code;

            if(error_code){
            	// Something bad may have just happened (like we just detected an edge or wall) so stop immediately.
            	stop();
            }else{
            	// Movement completed without issue, so stop gradually using deceleration.
                set(0, 0);
            }
    	}

    	bool has_movement_expired(){
    		return is_executing_movement() && (millis() - _movement_start_millis)/1000 >= _movement_seconds;
    	}
        
        void set_acceleration(unsigned int a){
            _motor_left.set_acceleration(a);
            _motor_right.set_acceleration(a);
        }

        unsigned int get_acceleration(){
            return _motor_left.get_acceleration();
        }

        virtual void update(){

        	// Occassionally, we're unable to connect on startup, so retry.
        	if(!is_connected()){
        		connect();
        	}

        	// If we still can't connect, then abort.
            if(!is_connected()){
            	eflag.set(COMMOTION_ERROR_DISCONNECT);
                return;
            }
            
            set_motor_speeds(0, 0, _motor_left.get_speed(), _motor_right.get_speed());
            
            /*
            // Query encoder state.
            // 2016.2.12 CKS This doesn't work.
            // Using MCU=0 it doesn't seem to break anything, but no I2C response is ever recieved.
            // Using MCU=1 causes it to hang indefinitely.
            // request status update from ComMotion shield every 50mS (20 times a second)
            if(millis()-stime >= COMMOTION_STATUS_UPDATE_FREQ){
                // toggle between MCU 1 and MCU 2
                //MCU++;if(MCU>1) MCU=0;//causes arduino/commotion to crash/hang?
                //MCU=1;//causes arduino/commotion to crash/hang
                //MCU=0;
                stime = millis();
                //request_commotion_status(MCU, B00000001|B00100000);
                request_commotion_status(MCU, B00000001);
            }
            */

            // Poll encoder state.
            if(!checks || (millis() - _last_encoder_check > COMMOTION_ENCODER_FREQ)){
				delay(10); // necessary, otherwise set_motor_speed() interferes causing non-deterministic hang
				//Wire.requestFrom(COMMOTION_ADDR1, 4);
				Wire.requestFrom(COMMOTION_ADDR2, REQUESTFOR_SIZE);

				a_encoder.set(receive_wire_int16());
				b_encoder.set(receive_wire_int16());

				_movement_encoder_changed |= a_encoder.is_changed();
				_movement_encoder_changed |= b_encoder.is_changed();

//				acount = receive_wire_int16();
//				bcount = receive_wire_int16();

				eflag.set(read_byte());

				aspeed = receive_wire_int16();
				bspeed = receive_wire_int16();

				_last_encoder_check = millis();

				checks += 1;
            }
        }
        
        void stop(){
            if(!is_connected()){
                return;
            }
            
            reset_movement();

            _motor_left.set_speed(0);
            _motor_right.set_speed(0);
            //NA NA left right
            set_motor_speeds(0, 0, _motor_left.get_speed_instantly(), _motor_right.get_speed_instantly());
            
        }

        virtual bool get_and_clear_changed(){
        	bool a = a_encoder.get_and_clear_changed();
        	bool b = b_encoder.get_and_clear_changed();
        	bool c = eflag.get_and_clear_changed();
        	return a || b || c;
        	//return true;
        }

        String get_a_encoder_packet(){
        	return
        		String(ID_GET_VALUE)+String(' ')+
				String(ID_MOTOR_ENCODER)+String(' ')+
				String(0)+String(' ')+
				String(a_encoder.get())
				//String(acount)
			;
        }

        String get_b_encoder_packet(){
        	return
        		String(ID_GET_VALUE)+String(' ')+
				String(ID_MOTOR_ENCODER)+String(' ')+
				String(1)+String(' ')+
				String(b_encoder.get())
				//String(bcount)
			;
        }

        String get_eflag_packet(){
        	return
        		String(ID_GET_VALUE)+String(' ')+
				String(ID_MOTOR_ERROR)+String(' ')+
				String(eflag.get())
			;
        }

        String get_movement_packet(){
        	if(_movement_send_done){
        		_movement_send_done = false;
        		return String(ID_TWIST_DONE)+String(' ')+String(_movement_error_code);
        	}else{
        		return String("");
        	}
        }

        //DEPRECATED
        virtual String get_reading_packet(){
        	return
				get_a_encoder_packet()

				+String('\n')+

				get_b_encoder_packet()

				+String('\n')+

				get_eflag_packet()
			;
        }

        String get_acceleration_packet(){
            return String(ID_GET_VALUE)+String(' ')+
                String(ID_MOTOR_ACCEL)+String(' ')+
                String(get_acceleration());
        }

};
