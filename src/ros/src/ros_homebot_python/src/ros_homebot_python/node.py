#!/usr/bin/env python
from __future__ import print_function

import os
import re
import sys
import time
import datetime
import threading
import traceback
from Queue import Queue, Empty as EmptyException, Full as FullException
from functools import partial
import serial
import termios

import rospy
from geometry_msgs.msg import Twist
from std_srvs.srv import Empty, EmptyResponse

from ros_homebot_python.controller import Arduino
from ros_homebot_python.packet import Packet, BooleanPacket, LEDPacket
from ros_homebot_msgs import srv as srvs
from ros_homebot_msgs import msg as msgs
from ros_homebot_python import constants as c

import constants as c
import utils

def get_name(packet_id):
    return re.sub(r'[^a-z]+', '_', c.ALL_IDS[packet_id])

def get_packet_id(packet_name):
    return c.NAME_TO_IDS[packet_name]

def get_pub_attr_name(packet_id):
    """
    Generates the attribute name used to store the publisher.
    """
    return get_name(packet_id) + '_pub'
    
def get_srv_attr_name(packet_id):
    """
    Generates the attribute name used to store the service.
    """
    return get_name(packet_id) + '_srv'

def get_srv_type_name(packet_id):
    """
    Generates the packet's equivalent /srv file name.
    
    e.g. get_srv_type_name('a') == 'PanAngle'
    """
    name = re.sub(r'[^a-z]+', ' ', c.ALL_IDS[packet_id])
    name = (''.join(map(str.title, name.split(' '))))
    return name
    
def get_msg_type_name(packet_id):
    """
    Generates the packet's equivalent /msg file name.
    """
    name = re.sub(r'[^a-z]+', ' ', c.ALL_IDS[packet_id])
    name = (''.join(map(str.title, name.split(' '))))
    if packet_id != c.ID_PONG:
        name = name + 'Change'
    return name

def get_socket_event_name_in(device, packet_id):
    if device in c.NAME_TO_INDEX:
        device_name = device
    else:
        device_name = c.INDEX_TO_NAME[device].lower()
    event = '%s_%s' % (device_name, camel_to_underscore(get_msg_type_name(packet_id)))
    event = event.lower()
    return event

def packet_to_message_type(packet_id):
    name = get_msg_type_name(packet_id)
    return getattr(msgs, name)

def packet_to_service_type(packet_id):
    name = get_srv_type_name(packet_id)
    return getattr(srvs, name)
    
def packet_to_service_request_type(packet_id):
    name = get_srv_type_name(packet_id)
    return getattr(srvs, name+'Request')

def to_type(v, typ):
    if isinstance(typ, type):
        return typ(v)
    elif isinstance(typ, basestring):
        if typ.startswith('int') or typ.startswith('uint'):
            return int(v)
        if typ.startswith('float'):
            return float(v)
    else:
        raise NotImplementedError, 'Unknown type: %s' % typ

def get_topic_name(device, packet_id):
    assert device in c.DEVICE_SIGNATURES
    return '/%s_arduino/%s' % (device.lower(), get_name(packet_id))

def subscribe_to_topic(device, packet_id, callback):
    topic_name = get_topic_name(device, packet_id)
    msg_type = packet_to_message_type(packet_id)
    rospy.Subscriber(topic_name, msg_type, callback)

def get_service_proxy(device, packet_id):
    service_name = get_topic_name(device, packet_id)
    service_type = packet_to_service_type(packet_id)
    return rospy.ServiceProxy(service_name, service_type)

def set_line_laser(state):
    from rpi_gpio.srv import DigitalWrite
    state = int(bool(state))
    rospy.ServiceProxy('/rpi_gpio/set_pin', DigitalWrite)(c.LINE_LASER_PIN, state)

def say(text):
    assert isinstance(text, basestring)
    import ros_homebot_msgs.srv
    say = rospy.ServiceProxy('/sound/say', ros_homebot_msgs.srv.TTS)
    say(text, '', 0, 0)

def camel_to_underscore(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

class BaseArduinoNode():
    
    name = None
    
    def __init__(self, **kwargs):
        
        assert self.name in (c.NAME_TORSO, c.NAME_HEAD)
        
        rospy.init_node('%s_arduino' % self.name.lower(), log_level=rospy.DEBUG)
        
        self.verbose = int(rospy.get_param("~verbose", 0))
        self.log('verbose:', self.verbose)
        
        # If true, indicates that all motor processes should halt.
        self.all_stopping = False
        
        # If true, is trying to connect and communicate with Arduino.
        self.running = False
        
        # If true, the serial port has been opened.
        self.connected = True
        
        # The duplex Serial() instance.
        self._serial = None
        
        # Serial port. e.g. /dev/ttyACM0
        self.port = kwargs.pop('port', None) or utils.find_serial_device(self.name)
        print('Using port %s.' % self.port)
        
        # Serial speed.
        self.speed = int(kwargs.pop('speed', 57600))
        
        # The last time we sent a ping request.
        self.last_ping = 0
        
        # The time we last received a pong response.
        self.last_pong = 0
        
        # The time we last received data from the Arduino.
        self.last_read = 0
        
        # The hash value expected for the next read.
        self.expected_hash = None
        
        # A registry of acknowledgement receipts
        self._acks = {} # {packet_id: time read}
        
        # The main read thread handle.
        self._read_thread = None
        
        # The main write thread handle.
        self._write_thread = None
        
        # Serial IO queues.
        queue_size = kwargs.pop('queue_size', 100)
        self.outgoing_queue = Queue(maxsize=queue_size)
        self.serial_out_lock = threading.RLock()
        self.serial_in_lock = threading.RLock()
        
        # Cleanup when termniating the node
        rospy.on_shutdown(self.shutdown)

        # Overall loop rate: should be faster than fastest sensor rate
        self.rate = int(rospy.get_param("~rate", 50))
        r = rospy.Rate(self.rate)

        # Dynamically create a publisher for each message type.
        formats_out = self.publisher_formats
        for _packet_id, _def in formats_out.iteritems():
            pub_attr_name = get_pub_attr_name(_packet_id)
            pub_topic_name = '~' + get_name(_packet_id)
            #msg_type = getattr(msgs, get_msg_type_name(_packet_id))
            msg_type = packet_to_message_type(_packet_id)
            print('creating publisher:')
            print('    pub_attr_name:', pub_attr_name)
            print('    pub_topic_name:', pub_topic_name)
            print('    msg_type:', msg_type)
            setattr(
                self,
                pub_attr_name,
                rospy.Publisher(pub_topic_name, msg_type, queue_size=1))
        
        # Dynamically create a service for each service type.
        formats_in = self.service_formats
        for _packet_id, _def in formats_in.iteritems():
            rospy.Service(
                '~' + get_name(_packet_id),
                getattr(srvs, get_srv_type_name(_packet_id)),
                partial(self._ros_to_arduino_handler, packet_id=_packet_id))
        
        rospy.Service('~reset', Empty, self.reset)

        # Begin processing data to/from Arduino.
        self.connect()
        
        print('Running.')
        
        # Start polling the sensors and base controller
        while not rospy.is_shutdown():
            
            # Publish all sensor values on a single topic for convenience
            now = rospy.Time.now()
            
            r.sleep()
    
    def _arduino_to_ros_handler(self, packet):
        """
        Receives all packets from the Arduino and forwards them to ROS.
        """
        self.log('Receiving packet: %s' % packet)
        handler_name = '_on_packet_%s' % packet.id_name
        
        if self.verbose:
            print('-'*80)
            print('arduino to ros handler:')
            print('packet.id:', packet.id, 'data:', packet.data)
        try:
            packet_id = ord(packet.id)
        except TypeError as e:
            print('Invalid packet ID: %s' % e, file=sys.stderr)
            traceback.print_exc(file=sys.stderr)
            return
            
        if hasattr(self, handler_name): 
            getattr(self, handler_name)(packet)
        else:
            
            # Lookup packet-specific message details.
            packet_id = packet.id
            parameters = packet.parameters
            if packet.id == c.ID_GET_VALUE:
                # If packet is the special get_value container type, convert it to the normal type.
                packet_id = packet.data[0]
                parameters = packet.data[1:].strip().split(' ')
#             if packet_id == c.ID_PAN_ANGLE:#TODO:remove
#                 print('packet:', packet_id, parameters)
            pub_attr_name = get_pub_attr_name(packet_id)
            publisher = getattr(self, pub_attr_name, None)
#             if packet_id == c.ID_PAN_ANGLE:#TODO:remove
#                 print('pub_attr_name:', pub_attr_name)
#                 print('publisher:', publisher, publisher.name)
            if publisher:
                #msg_type = getattr(msgs, get_msg_type_name(packet_id))
                msg_type = packet_to_message_type(packet_id)
            
#                 if packet_id == c.ID_PAN_ANGLE:#TODO:remove
#                     print('msg_type:', msg_type)
                        
                # Create packet-specific message.
                msg = msg_type()
                msg.device = self.name_index
                output_format = self.publisher_formats[packet_id]
                    
                # Ignore acknowledgement packet.
                if len(parameters) == 1 and parameters[0] == c.OK:
                    return
                
                # Ignore malformed packets.
                if len(parameters) != len(output_format):
                    print('Expected %i parameters but received %i.\n' % (len(parameters), len(output_format)), file=sys.stderr)
                    print('parameters:', parameters, file=sys.stderr)
                    print('output_format:', output_format, file=sys.stderr)
                    return
                    
                for (arg_name, arg_type), param in zip(output_format, parameters):
                    setattr(msg, arg_name, to_type(param, arg_type))
                
                # Publish packet on packet-specific publisher.    
                try:
                    publisher.publish(msg)
                #except ROSSerializationException as e:
                except Exception as e:
                    traceback.print_exc(file=sys.stderr)
                    #self.log(e)
            else:
                print('No publisher for %s.' % pub_attr_name)
 
    def _ros_to_arduino_handler(self, req, packet_id):
        """
        Handles ROS service requests and forwards them to the Arduino.
        """
        self.log('Received service request: %s %s' % (req, packet_id))
        
        # Convert ROS message to Arduino packet.
        input_formats = self.service_formats
        parameters = input_formats[packet_id]
        data = []
        for arg_name, arg_type in parameters:
            value = getattr(req, arg_name)
            data.append(str(value))
        packet = Packet(id=packet_id, data=' '.join(data))
        
        # Queue for sending.
        self.outgoing_queue.put(packet)
        
        # Return service response.
        name = type(req).__name__.replace('Request', '')
        resp_cls = getattr(srvs, name + 'Response')
        return resp_cls()
    
    def _write_packet(self, packet, delay=0.1):
        """
        Sends the main packet, prefixed with a checksum packet.
        
        The delay here is very important since the host computer is likely immensely faster
        than the Arduino. Without any delay, the host may overwhelm the Arduino's serial buffer,
        which by default is only 64 bytes, causing the checksum to be lost, causing the resulting
        main packet to be ignored.
        
        We must delay slightly after the checksum packet to give the Arduino enough time
        to process it and extract the checksum.
        """
        
        # Send checksum packet.
        s = '%s %s\n' % (c.ID_HASH, packet.hash)
        self.log('writing_hash_packet:', repr(s))
        self._serial.write(s)
        self._serial.flush()
        time.sleep(delay)
        
        # Send main packet.
        data = packet.data.strip()
        if data:
            s = '%s %s\n' % (packet.id, data)
        else:
            s = '%s\n' % packet.id
        self.log('writing_main_packet:', repr(s))
        self._serial.write(s)
        self._serial.flush()
        time.sleep(delay)
        
    def _write_data_to_arduino(self):
        """
        Continually writes data from outgoing queue to the Arduino.
        """
        while self.running:
    
            # Check heartbeat.
            if self.last_ping+1 <= time.time():
                self.last_ping = time.time()
                self.outgoing_queue.put(Packet(c.ID_PING))
                
            # Automatically reset crashed Arduino?
#             print 'time until timeout:', (time.time() - self._last_pong)
#             if (time.time() - self._last_pong) > c.ARDUINO_PING_TIMEOUT:
#                 self.log('!'*80)
#                 self.log('Arudino unresponsive after %i seconds! Resetting! Possible hardware fault?' % c.ARDUINO_PING_TIMEOUT)
#                 self.log('!'*80)
#                 self._t = threading.Thread(target=self.reset)
#                 self._t.daemon = True
#                 self._t.start()
#                 return

            # Sending pending commands.
            if not self.outgoing_queue.empty():
                packet = self.outgoing_queue.get()
                
                for attempt in xrange(50):
                    self.log('Sending: %s, attempt %i' % (packet, attempt))

                    sent_time = time.time()
                    self._write_packet(packet)
                    
                    if packet.id in c.ACK_IDS:
                        # Wait for acknowledgement.
                        if self._wait_for_ack(packet.id, sent_time):
                            break
                    else:
                        # Don't wait for acknowledgement.
                        break

    def _wait_for_ack(self, packet_id, sent_time, timeout=5):
        """
        Blocks until the given acknowledgment response is received or a timeout occurs.
        """
        t0 = time.time()
        while (time.time() - t0) < timeout:
            if self._acks.get(packet_id, 0) > sent_time:
                return True
            time.sleep(timeout/100.)
        return False

    def _read_raw_packet(self):
        """
        Reads a raw line of data from the Arduino.
        """
        data = (self._serial.readline() or '').strip()
        
        if data:
            
            if data[0] == c.ID_HASH:
                # Check for new hash.
                try:
                    self.expected_hash = int(data[1:].strip())
                except (TypeError, ValueError) as e:
                    traceback.print_exc(file=sys.stderr)
            elif self.expected_hash:
                # Validate packet.
                packet = Packet.from_string(data)
                if packet.hash != self.expected_hash:
                    if self.verbose:
                        print('Invalid packet read due to hash mismatch:', packet)
                    data = ''
                    
        if data:
            # Record a successful read.
            self.last_read = time.time()
        
        return data

    def _read_packet(self):
        """
        Reads a packet from the Arduino.
        """
        data = self._read_raw_packet()
        if data:
            packet = Packet.from_string(data)
            
            # Keep a receipt of when we receive acknowledgments so the write thread
            # knows when to stop waiting.
            if packet.data == c.OK:
                self._acks[packet.id] = time.time()
    
            # Record pong so we know Arduino is still alive.
            if packet.id == c.ID_PONG:
                self._last_pong = time.time()
                
            return packet
    
    def _read_data_from_arduino(self):
        """
        Continually reads data from the Arduino and writes it to ROS.
        """
        while self.running:
    
            # Read packet from Arduino.
            packet = self._read_packet()
            if not packet:
                time.sleep(0.01)
                continue
            #self.log(u'Received: %s' % packet).encode('utf-8'))
            self.log('Received:', packet)
            
            # Process packet.
            self._arduino_to_ros_handler(packet)
            
    def all_stop(self):
        """
        Signals the Arduino to stop all motors.
        """
        self.all_stopping = True
        self.outgoing_queue.put(Packet(c.ID_ALL_STOP))

    def confirm_identity(self, max_retries=10):
        """
        Confirms the device we're connected to is the device we expected.
        """
        assert self.running and self.connected and self._serial
        for _ in xrange(max_retries):
            self.log('Confirming device (attempt %i of %i)...' % (_+1, max_retries))
            
            self._write_packet(Packet(c.ID_IDENTIFY))
            
            ret = self._read_raw_packet()
            if ret == c.ID_IDENTIFY+' '+self.name.upper():
                return True
            elif ret and ret[0] == c.ID_LOG:
                self.log('init log:', ret)
            elif not ret:
                # No response, may have timed out, wait a little.
                time.sleep(0.1)
            else:
                # Invalid or garbled response. Try again, incase device is correct
                # by response was garbled.
                self.log('Invalid identity:', ret)
        return False

    def connect(self):
        """
        Starts the thread that establishes serial connection and begins communication.
        """
        if self.running:
            return
        self.running = True
    
        # Disable reset after hangup.
        # This prevents the Arduino Uno from resetting if we disconnect or lose power.
        # This is necessary if we want to "sleep" by disconnecting head power and using
        # the Arduino to wake us up afterwards.
        with open(self.port) as f:
            attrs = termios.tcgetattr(f)
            attrs[2] = attrs[2] & ~termios.HUPCL
            termios.tcsetattr(f, termios.TCSAFLUSH, attrs)
    
        # Instantiate serial interface.
        self._serial = serial.Serial(
            port=self.port,
            baudrate=self.speed,
            timeout=.1,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS,
            dsrdtr=False,
        )
        self.connected = True
        assert self.confirm_identity(), 'Device identity could not be confirmed.'

        self._read_thread = threading.Thread(target=self._read_data_from_arduino)
        self._read_thread.daemon = True
        self._read_thread.start()
        
        self._write_thread = threading.Thread(target=self._write_data_to_arduino)
        self._write_thread.daemon = True
        self._write_thread.start()
    
    def disconnect(self):
        """
        Terminates the communication thread and closes the serial connection.
        """
        self.running = False
        try:
            self._read_thread.join(5)
            self._write_thread.join(5)
        except RuntimeError:
            pass

    def log(self, *msg):
        if self.verbose:
            #rospy.log(' '.join(map(str, msg)))
            try:
                print(u' '.join(map(unicode, msg)).encode('utf-8'))
            except UnicodeDecodeError as e:
                traceback.print_exc(file=sys.stderr)
            
    @property
    def name_index(self):
        return c.NAME_TO_INDEX[self.name]
    
    @property
    def publisher_formats(self):
        formats_out = c.BOTH_FORMATS_OUT.copy()
        if self.name == c.NAME_HEAD:
            formats_out.update(c.HEAD_FORMATS_OUT)
        if self.name == c.NAME_TORSO:
            formats_out.update(c.TORSO_FORMATS_OUT)
        return formats_out
        
    @property
    def service_formats(self):
        formats_out = c.BOTH_FORMATS_IN.copy()
        if self.name == c.NAME_HEAD:
            formats_out.update(c.HEAD_FORMATS_IN)
        if self.name == c.NAME_TORSO:
            formats_out.update(c.TORSO_FORMATS_IN)
        return formats_out

    def reset(self, req=None):
        """
        Triggers the Arduino to reset as though we pushed its reset button.
        """
        self.disconnect()
        with serial.Serial(self.port) as arduino:
            arduino.setDTR(False)
            time.sleep(1)
            arduino.flushInput()
            arduino.setDTR(True)
        time.sleep(1)
        self.connect()
        return EmptyResponse()
 
    def shutdown(self):
        """
        Called when ROS issues a shutdown event.
        """
        self.log("Disconnecting the %s arduino..." % self.name)
        
        # Automatically signal device to stop.
        self.outgoing_queue.put(Packet(c.ID_ALL_STOP))
        t0 = time.time()
        while not self.outgoing_queue.empty():
            time.sleep(0.1)
            self.log('Waiting for write queue to clear...')
            if time.time() - t0 > 5:
                break
        
        self.disconnect()