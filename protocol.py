from constants import LOG_LEVEL, LOG_DEBUG
import struct

DELIMITER_PARAMS = '?'
DELIMITER_ID = '&'

VESNA_ENTRY_LEN = 6
VESNA_TYPE_INT = 0
VESNA_TYPE_FLOAT = 1

class Message:
    
    method_push = 0
    method_post = 1
    method_get = 2
    
    def __init__(self):
        self.method = None
        self.command = None
        self.params = None
        self.recipient_id = None
        self.content = None
    
    def has_params(self):
        return self.params is not None
    
    def has_recipient_id(self):
        return self.recipient_id is not None
        
    def has_content(self):
        return self.method == Message.method_push or self.method == Message.method_post
    
    def get_method_str(self):
        if self.method == Message.method_push:
            return 'PUSH'
        elif self.method == Message.method_post:
            return 'POST'
        elif self.method == Message.method_get:
            return 'GET'
        else:
            return str(self.method)
    
    def __str__(self):
        result = ''
        if self.method is not None:
            result += 'Method: ' + self.get_method_str() + '\t'
        if self.command is not None:
            result += 'Command: ' + str(self.command) + '\t'
        if self.params is not None:
            result += 'Params: ' + str(self.params) + '\t'
        if self.recipient_id is not None:
            result += 'ID: ' + str(self.recipient_id) + '\t'
        if self.content is not None:
            result += 'Content: ' + str(self.content) + '\t'
        return result


def _read_line(sock):
    line = ''
    last_chs = []
    ch = None
    while True:
        ch = sock.recv(1)
        line += ch
        
        last_chs.append(ch)
        if len(last_chs) > 2:
            last_chs.pop(0)
            if last_chs[0] == '\r' and last_chs[1] == '\n':
                break
            
    if LOG_LEVEL == LOG_DEBUG:
        print 'Received line: ' + line
        
    return line
        

def _parse_content(sock, msg):
    line2 = _read_line(sock).strip()
    content_len = int(line2.split('=')[1])
    msg.content = sock.recv(content_len)
    sock.recv(2)
       

def read_msg(sock):
    line1 = _read_line(sock)
    msg = Message()
    
    start_idx = 0
    if line1[0:4] == 'PUSH':
        msg.method = Message.method_push
        start_idx = 5
    elif line1[0:4] == 'POST':
        msg.method = Message.method_post
        start_idx = 5
    elif line1[0:3] == 'GET':
        msg.method = Message.method_get
        start_idx = 4
    else:
        raise ValueError('Invalid message method: ' + str(line1))
    
    i = start_idx
    while i < len(line1) and line1[i] != DELIMITER_PARAMS and line1[i] != DELIMITER_ID:
        i += 1
        
    msg.command = line1[start_idx:i-2] if i == len(line1) else line1[start_idx:i]
    
    start_idx = i+1
    if i < len(line1) and line1[i] == DELIMITER_PARAMS:
        while i < len(line1) and line1[i] != DELIMITER_ID:
            i += 1
            
        msg.params = line1[start_idx:i-2] if i == len(line1) else line1[start_idx:i]
        
    start_idx = i+1
    if i < len(line1) and line1[i] == DELIMITER_ID:
        while i < len(line1):
            i += 1
        msg.recipient_id = line1[start_idx:i-2]
    
    if msg.has_content():
        _parse_content(sock, msg)
        
    if LOG_LEVEL == LOG_DEBUG:
        print 'Received message: ' + str(msg)
        
    return msg

def parse_vesna_table(table):
    n = len(table) / VESNA_ENTRY_LEN
    res = [0]*n
    
    for i in xrange(n):
        can_id = struct.unpack('B', table[VESNA_ENTRY_LEN*i])[0]
        val_type = struct.unpack('B', table[VESNA_ENTRY_LEN*i+1])[0]
        val = None
        
        if val_type == VESNA_TYPE_INT:
            val = struct.unpack('B', table[i*VESNA_ENTRY_LEN + 2])[0]
        elif val_type == VESNA_TYPE_FLOAT:
            val = struct.unpack('f', table[i*VESNA_ENTRY_LEN + 2:i*VESNA_ENTRY_LEN + 6])[0]
        else:
            raise ValueError('Unknown type: ' + type)
        
        res[i] = (can_id, val)
        
    return res
    