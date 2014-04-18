from threading import Thread
import time
import mysql.connector
from constants import MYSQL_CONFIG, LOG_LEVEL, LOG_RELEASE
import sys
import calendar
#import threading

#_db_lock = threading.RLock()
_vals_table = [0]*256
_history_cans = {
    103: 'temp_cabin',
    104: 'temp_ac',
    106: 'battery_ls',
    108: 'fresh_water',
    122: 'temp_bedroom', 
    123: 'hum_bedroom', 
    147: 'temp_ls',
    148: 'hum_ls',
    159: 'temp_sc',
    160: 'hum_sc'
}


def fetch_history(can_id, from_time):
    if not can_id in _history_cans:
        if LOG_LEVEL <= LOG_RELEASE:
            print 'Tried querying non-historical CAN: {}'.format(can_id)
        return
    
    connection = None
    result = []
    
    try:        
        if LOG_LEVEL <= LOG_RELEASE:
            print 'Querying history for CAN: {}'.format(can_id)
        
        connection = mysql.connector.connect(**MYSQL_CONFIG)
        cursor = connection.cursor()
        
        query = "SELECT time,val FROM {} NATURAL JOIN history_all where time >= %s".format(_history_cans[can_id])
        cursor.execute(query, (from_time,))
        
        result = [(calendar.timegm(timestamp.utctimetuple()), val) for timestamp, val in cursor]

        if LOG_LEVEL <= LOG_RELEASE:
            print 'Found {} entries'.format(len(result))
    except:
        print 'Failed to insert to DB: ', sys.exc_info()[1].message
    finally:
        if cursor is not None:
            cursor.close()
        if connection is not None:
            connection.close()
            
    return result

def _insert_to_db(can_id, val):
    connection = None
    cursor = None
    
    try:        
        if LOG_LEVEL <= LOG_RELEASE:
            print 'Inserting to DB CAN: {} val: {}'.format(can_id, val)
        
        connection = mysql.connector.connect(**MYSQL_CONFIG)
        cursor = connection.cursor()
        
        cursor.execute("INSERT INTO history_all (can_id, val) VALUES (%s, %s)", (can_id, val))
        
        if can_id in _history_cans:
            rec_id = cursor.lastrowid
            cursor.execute("INSERT INTO {} (rec_id) VALUES (%s)".format(_history_cans[can_id]), (rec_id,))
        
        connection.commit()
    except:
        print 'Failed to insert to DB: ', sys.exc_info()[1].message
    finally:
        if cursor is not None:
            cursor.close()
        if connection is not None:
            connection.rollback()
            connection.close()

def set_value(can_id, val):
    _vals_table[can_id] = val
    _insert_to_db(can_id, val)
    
def save_to_db():
    while True:
        time.sleep(60*30)
#         time.sleep(2)
        table = [val for val in _vals_table]
        print str(table)
    
_save_thread = Thread(target=save_to_db)
_save_thread.daemon = True
_save_thread.start()