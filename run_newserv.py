import random, binascii, sys, http.server, socketserver, urllib.parse, time, os, json, subprocess, threading

PORT = 8000
LICENSE = "system/licenses.nsi"
DIRECTORY = 'www'
BINARY = "./newserv"

REG_KEYS = []
SERIALS = []

Handler = http.server.SimpleHTTPRequestHandler
newserv = subprocess.Popen(BINARY, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

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

while True:
    user_input = input("")
    write_to_stdin(user_input, False)

os._exit(0)