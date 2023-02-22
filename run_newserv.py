import random, binascii, sys, http.server, socketserver, urllib.parse, time, os, json, subprocess, threading

PORT = 8000
LICENSE = "system/licenses.nsi"
DIRECTORY = 'www'
BINARY = "./newserv"
RUNNING = True

REG_KEYS = []
SERIALS = []

Handler = http.server.SimpleHTTPRequestHandler
newserv = subprocess.Popen(BINARY, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

event_data = [
    {'start': '02/01', 'event': 'val'},
    {'start': '02/21', 'event': 'spring'},
    {'start': '03/01', 'event': 'white'},
    {'start': '03/21', 'event': 'spring'},
    {'start': '04/01', 'event': 'easter'},
    {'start': '04/21', 'event': 'spring'},
    {'start': '06/07', 'event': 'wedding'},
    {'start': '06/12', 'event': 'sonic'},
    {'start': '07/01', 'event': 'summer'},
    {'start': '09/21', 'event': 'fall'},
    {'start': '10/01', 'event': 'hall'},
    {'start': '11/01', 'event': 'fall'},
    {'start': '12/01', 'event': 'xmas'},
    {'start': '12/27', 'event': 'newyear'}
]

arks_quest_data = [
    [
        {'id': 101, 'time': 20, 'ann': 'The 32nd WORKS divisionof the Pioneer 2 outer space task force needsyour help in the forest!'},
        {'id': 102, 'time': 20, 'ann': 'The 32nd WORKS divisionof the Pioneer 2 outer space task force needsyour help in the caves!'},
        {'id': 103, 'time': 20, 'ann': 'The 32nd WORKS divisionof the Pioneer 2 outer space task force needsyour help in the mines!'},
        {'id': 104, 'time': 20, 'ann': 'The 32nd WORKS divisionof the Pioneer 2 outer space task force needsyour help in the ruins!'}],
    [
        {'id': 108, 'time': 27.5, 'ann': 'Monsters seem to be   constantly regeneratingin one area of the      forest.'},
        {'id': 109, 'time': 32.5, 'ann': 'Monsters seem to be   constantly regeneratingin one area of the      caves.'},
        {'id': 110, 'time': 27.5, 'ann': 'Monsters seem to be   constantly regeneratingin one area of the      mines.'},
        {'id': 111, 'time': 27.5, 'ann': 'Monsters seem to be   constantly regeneratingin one area of the      ruins.'}],
    [
        {'id': 117, 'time': 15, 'ann': 'We need your help with a top-secret mission!'}],
    [
        {'id': 233, 'time': 27.5, 'ann': 'A large number of      monsters have appearedon Gal Da Val Island! Weneed help in the north  jungle and seaside!'},
        {'id': 234, 'time': 32.5, 'ann': 'A large number of      monsters have appearedon Gal Da Val Island! Weneed help in the        mountains!'},
        {'id': 235, 'time': 32.5, 'ann': 'A large number of      monsters have appearedon Gal Da Val Island! Weneed help in the         seabed!'},
        {'id': 236, 'time': 10, 'ann': 'A large number of      monsters have appearedon Gal Da Val Island! Weneed help in the east   and west towers!'}],
]

MIN_TIME_BETWEEN_QUESTS = 6
EXTRA_TIME_BETWEEN_QUESTS = 4
EMERGENCY_PRE_ANNOUNCEMENT = "CALLING ALL HUNTERS!  EMERGENCY MESSAGE    INCOMING PLEASE        STANDBY FOR MORE      INFORMATION"
EMERGENCY_POST_ANNOUNCEMENT ="PLEASE REPORT TO THE QUEST COUNTER FOR     FURTHER INSTRUCTIONS"
MINS_UNTIL_ANNOUNCEMENT = 1

def arks_quest_manager():
    time.sleep(MIN_TIME_BETWEEN_QUESTS / 2 * 60)
    current_quest = None
    quest_index = 0
    while RUNNING:
        if quest_index == 0:
            new_quest = random.choice(arks_quest_data)
            if current_quest != None and new_quest[quest_index]['id'] == current_quest[quest_index]['id']:
                continue
        current_quest = new_quest
        write_to_stdin("announce " + EMERGENCY_PRE_ANNOUNCEMENT)
        time.sleep(MINS_UNTIL_ANNOUNCEMENT * 60 - 1)
        write_to_stdin("set-quest %d" % (current_quest[quest_index]['id']))
        time.sleep(1)
        write_to_stdin("announce " + current_quest[quest_index]['ann'])
        time.sleep(7.5)
        write_to_stdin("announce " + EMERGENCY_POST_ANNOUNCEMENT)
        time.sleep(current_quest[quest_index]['time'] * 60)
        write_to_stdin("set-quest 0")
        time.sleep((MIN_TIME_BETWEEN_QUESTS + random.randint(0, EXTRA_TIME_BETWEEN_QUESTS)) * 60)
        if quest_index < len(current_quest) - 1:
            quest_index += 1
        else:
            quest_index = 0

# Return the current event
def current_event():
    now = time.localtime()
    revent = None
    for event in event_data:
        if now.tm_mon >= int(event['start'][:2]) and now.tm_mday >= int(event['start'][3:]):
            revent = event['event']
    if revent == None:
        return event_data[-1]['event']
    else:
        return revent

last_event = current_event()

def check_event():
    global last_event
    event = current_event()
    if event != last_event:
        last_event = event
        write_to_stdin("set-event " + event)

def event_watcher():
    time.sleep(5)
    write_to_stdin("set-event %s" % current_event())
    while RUNNING:
        time.sleep(60)
        check_event()

# define a function to write data to stdin
def write_to_stdin(data, echo=True):
    if echo:
        print(data)
    newserv.stdin.write((data + "\n").encode())
    newserv.stdin.flush()

# define a function to continuously read from stdout
def read_stdout():
    while RUNNING:
        output = newserv.stdout.readline().decode().strip()
        if output:
            print(output)

def get_serials():
    f = open(LICENSE, "rb")
    data = ""
    while data != b"":
        data = f.read(84)
        if data == b"":
            break
        SERIALS.append(int.from_bytes(data[40:44], byteorder='little'))

class MyHandler(Handler):
    def send_file(self, file, content_type=""):
        file_path = os.path.join(os.getcwd(), DIRECTORY, file)
        with open(file_path, 'rb') as file:
            self.send_response(200)
            if content_type != "":
                self.send_header('Content-type', content_type)
            self.end_headers()
            while True:
                data = file.read(1024)
                if not data:
                    break
                self.wfile.write(data)

    def do_POST(self):
        if self.path == '/pso-register':
            try:
                content_length = int(self.headers['Content-Length'])
                post_data = self.rfile.read(content_length)
                post_data = urllib.parse.parse_qs(post_data.decode('utf-8'))
                password = post_data.get('password', [''])[0]
                result = self.register(password, post_data.get('reg-key', [''])[0])
                self.send_response(200)
                self.send_header('Content-type', 'text/html')
                self.end_headers()
                self.wfile.write(result.encode())
            except Exception as e:
                print(e)
                self.send_error(500, 'Internal Server Error')
        else:
            self.send_error(404, 'File Not Found')

    def do_GET(self):
        if ".." in self.path:
            self.send_error(404, 'File Not Found')
        elif self.path[:7] == '/bg.png':
            self.send_file("bg.png", 'image/png')
        else:
            file_path = os.path.join(os.getcwd(), DIRECTORY, 'index.html')
            with open(file_path, 'rb') as file:
                file_contents = file.read()
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(file_contents)

    def register(self, password, regKey):
        if not regKey in REG_KEYS:
            time.sleep(3)
            print("Access denied: " + regKey)
            return json.dumps({"err": "Access denied!"})
        print(regKey)
        serial = ""
        for i in range(10):
            serial += "%d" % random.randint(0, 9)

        access = b""
        for i in range(12):
            access += b"%d" % random.randint(0, 9)

        gcpass = password
        gcpass_bytes = gcpass.encode("utf-8")
        if len(gcpass_bytes) > 8:
            print("Password can't be over 8 chars")
            return json.dumps({"err": "Password can't be over 8 chars."})
        elif len(gcpass_bytes) == 0:
            print("Password can't be empty")
            return json.dumps({"err": "Password can't be empty."})
        elif len(gcpass_bytes) < 8:
            gcpass_bytes += b"\x00" * (8 - len(gcpass_bytes))
        serial_n = int(serial)
        if serial_n == 0:
            print("Serial can't be 0.")
            return json.dumps({"err": "Serial can't be 0 (try again)."})
        if serial_n in SERIALS:
            print("Serial already exists.")
            return json.dumps({"err": "Serial already exists (try again)."})
        SERIALS.append(serial_n)
        write_to_stdin("add-license serial=%s access-key=%s gc-password=%s" % (serial, access.decode("utf-8"), gcpass))

        return json.dumps({"serial": serial, "access-key": access.decode("utf-8"), "gc-password": gcpass})

def run_http_serv():
    with socketserver.TCPServer(("localhost", PORT), MyHandler) as httpd:
        print(f"Serving directory '{DIRECTORY}' at http://localhost:{PORT}")
        httpd.allow_reuse_address = True
        httpd.request_queue_size = 1024
        httpd.serve_forever()

get_serials()
t = threading.Thread(target=read_stdout)
t.daemon = True
t.start()
h = threading.Thread(target=run_http_serv)
h.daemon = True
h.start()
e = threading.Thread(target=event_watcher)
e.daemon = True
e.start()
q = threading.Thread(target=arks_quest_manager)
q.daemon = True
q.start()

while True:
    user_input = input("")
    write_to_stdin(user_input, False)

RUNNING = False
os._exit(0)