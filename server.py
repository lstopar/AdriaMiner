import socket
from protocol import read_msg, Message, parse_vesna_table
from constants import SERVER_IP, SERVER_PORT, LOG_LEVEL, LOG_RELEASE, LOG_DEBUG
import sys
import time
import threading
#from concurrent.futures.thread import ThreadPoolExecutor
import db
from datetime import date, timedelta

#_executor = ThreadPoolExecutor(max_workers=1)
_write_lock = threading.Lock()

_sock = None

def write_str(msg_str):
    '''
    
    Writes the message to socket.
    
    '''
    
    _write_lock.acquire()
    
    try:
        if LOG_LEVEL == LOG_DEBUG:
            print 'Writing message: ' + msg_str
        
        _sock.sendall(msg_str)
        
        if LOG_LEVEL == LOG_DEBUG:
            print 'Message written!'
    except:
        print 'An exception occurred writing to socket: ', sys.exc_info()[1].message
    finally:
        _write_lock.release()

def _fetch_history(can_id, dev_id):
    '''
    
    Fetches history for the given CAN ID from the db and writes it to socket.
    
    '''
    try:
        if LOG_LEVEL <= LOG_RELEASE:
            print 'Fetching history for can: ' + str(can_id)
            
        hist = db.fetch_history(can_id, date.today()-timedelta(days=1))
        
        hist_content = ','.join([str(entry[0]) + ',' + str(entry[1]) for entry in hist])

        write_str('PUSH history?{}&{}\r\nLength={}\r\n{}\r\n'.format(can_id, dev_id, len(hist_content), hist_content))
    except:
        print 'An exception occurred while fetching history: ', sys.exc_info()[1].message
        
def _process_push(table):
    try:
        entries = parse_vesna_table(table)
        for can_id, val in entries:
            db.set_value(can_id, val)
    except:
        print 'An exception occurred while processing PUSH request: ', sys.exc_info()[1].message  
    
    

def _process_msg(msg):
    '''
    
    Processes the received message.
    
    '''
    try:
        if msg.method == Message.method_push and msg.command == 'res_table':
            _process_push(msg.content)
        elif msg.method == Message.method_get and msg.command == 'history' and msg.has_params() and msg.has_recipient_id():
            _fetch_history(int(msg.params), msg.recipient_id)
        else:
            print 'Unknown message: ', str(msg)
    except:
        print 'An exception occurred while processing message: ', sys.exc_info()[1].message
    
    

def read_msgs():
    '''
    
    Continuously reads messages from the socketS.
    
    '''
    try:
        while True:
            msg = read_msg(_sock)
            _process_msg(msg)
    except IOError as e:
        print 'An IO error occurred while reading from socket!', e.strerror
    except ValueError as e:
        print 'A ValueError occurred while reading from socket!', e.strerror
    finally:
        _sock.close()
            
        
# main loop
while True:
    try:
        print 'Connecting socket...'
        _sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        _sock.connect((SERVER_IP, SERVER_PORT))
        write_str('PUSH res_table|GET history&py_miner,pm1\r\n')
        
        print 'Socket connected!'
        read_msgs();
    except:
        print 'An exception occurred in the constants loop, restarting socket: ', sys.exc_info()[1].message
    finally:
        if _sock is not None:
            print 'Closing socket...'
            _sock.close()
            print 'Socket closed!'
            
    time.sleep(1)
    
