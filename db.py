from threading import Thread
import time

_vals_table = [0]*256

def fetch_history(can_id, from_time):
    return [(1397826993,1),(1397827093,2),(1397827193,3),(1397827293,4),(1397827393,5)]

def set_value(can_id, val):
    print 'Setting value ' + str(val) + ' for CAN ' + str(can_id)
    _vals_table[can_id] = val
    
def save_to_db():
    while True:
        time.sleep(60*30)
#         time.sleep(2)
        table = [val for val in _vals_table]
        print str(table)
    
_save_thread = Thread(target=save_to_db)
_save_thread.daemon = True
_save_thread.start()