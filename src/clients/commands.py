import sys, socket, threading, time

# Protocolo: "codigo#param1#param2#...\0"
#   1=REGISTER  2=UNREGISTER  3=CONNECT  4=DISCONNECT  5=SEND  7=USERS
# Respuesta del servidor: un byte (0=ok, 1/2/3=error)
# Mensajes entrantes por la conexión de CONNECT:
#   "SEND_MESSAGE#remitente#id#texto\0"
#   "SEND_MESS_ACK#id\0"

class Client:
    MAX_MSG_SIZE = 256
    TIMEOUT = 10

    _messages_socket = None
    _listening_thread = None
    _terminate = False

    _server_socket = None
    _address = None
    _port = None

    _connected_user = None

    def __init__(self, address: str, port: int):
        self._address = address
        self._port = port

    def _get_connection(self):
        if self._server_socket: return
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.connect((self._address, self._port))
        self._server_socket.settimeout(self.TIMEOUT)

    def _send(self, sock, msg: str):
        sock.send(bytes(msg + "\x00", "utf_8"))

    def _recv_code(self, sock) -> int:
        data = sock.recv(1)
        if not data:
            return -1
        return data[0]

    def _recv_str(self, sock) -> str:
        buf = bytearray()
        while True:
            c = sock.recv(1)
            if not c or c == b'\x00':
                break
            buf += c
        return buf.decode("utf_8")

    def _listen_server(self, username: str):
        # hilo que mantiene abierta la conexión persistente del CONNECT
        if self._messages_socket: return

        self._messages_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._messages_socket.connect((self._address, self._port))
        self._messages_socket.settimeout(self.TIMEOUT)

        self._send(self._messages_socket, f"3#{username}")
        try:
            response = self._recv_code(self._messages_socket)
            match response:
                case 0:
                    print("c> CONNECT OK")
                    self._connected_user = username
                    self._messages_socket.settimeout(None)  # esperar mensajes sin límite
                    while not self._terminate:
                        msg = self._recv_str(self._messages_socket)
                        if msg:
                            self._parse_incoming_message(msg)
                case 1:
                    print("c> CONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> USER ALREADY CONNECTED", file=sys.stderr)
                case _:
                    print("c> CONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECT FAIL", file=sys.stderr)

        self._messages_socket.close()
        self._terminate = False
        self._messages_socket = None

    def _parse_incoming_message(self, message: str):
        parts = message.split('#', 3)
        if not parts:
            return

        op = parts[0]
        if op == "SEND_MESSAGE" and len(parts) == 4:
            _, sender, mid, text = parts
            print(f"s> MESSAGE {mid} FROM {sender}\n    {text}\n    END")
            print("c> ", end="", flush=True)
        elif op == "SEND_MESS_ACK" and len(parts) == 2:
            print(f"c> SEND MESSAGE {parts[1]} OK")
            print("c> ", end="", flush=True)

    def register(self, username: str):
        self._get_connection()
        self._send(self._server_socket, f"1#{username}")
        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    print("c> REGISTER OK")
                case 1:
                    print("c> USERNAME IN USE", file=sys.stderr)
                case _:
                    print("c> REGISTER FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> REGISTER FAIL", file=sys.stderr)

        self._server_socket.close()
        self._server_socket = None

    def unregister(self, username: str):
        self._get_connection()
        self._send(self._server_socket, f"2#{username}")
        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    print("c> UNREGISTER OK")
                case 1:
                    print("c> USER DOES NOT EXIST", file=sys.stderr)
                case _:
                    print("c> UNREGISTER FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> UNREGISTER FAIL", file=sys.stderr)

        self._server_socket.close()
        self._server_socket = None

    def connect(self, username: str):
        if self._listening_thread: return
        self._listening_thread = threading.Thread(
            target=self._listen_server, args=(username,), daemon=True)
        self._listening_thread.start()
        time.sleep(0.2)  # esperar a que el hilo imprima el resultado antes del siguiente prompt

    def disconnect(self, username: str):
        self._get_connection()
        self._send(self._server_socket, f"4#{username}")
        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    print("c> DISCONNECT OK")
                    self._terminate = True
                    self._connected_user = None
                    self._listening_thread = None
                case 1:
                    print("c> DISCONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> DISCONNECT FAIL, USER NOT CONNECTED", file=sys.stderr)
                case _:
                    print("c> DISCONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> DISCONNECT FAIL", file=sys.stderr)
            self._terminate = True
            self._listening_thread = None

        self._server_socket.close()
        self._server_socket = None

    def send(self, username: str, message: str):
        self._get_connection()
        sender = self._connected_user if self._connected_user else username
        self._send(self._server_socket, f"5#{sender}#{username}#{message}")
        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    mid = self._recv_str(self._server_socket)
                    print(f"c> SEND OK - MESSAGE {mid}")
                case 1:
                    print("c> SEND FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case _:
                    print("c> SEND FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> SEND FAIL", file=sys.stderr)

        self._server_socket.close()
        self._server_socket = None

    def send_attach(self):
        pass  # Parte 2

    def users(self):
        self._get_connection()
        name = self._connected_user if self._connected_user else ""
        self._send(self._server_socket, f"7#{name}")
        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    count = int(self._recv_str(self._server_socket))
                    print(f"c> CONNECTED USERS ({count} users connected) OK")
                    for _ in range(count):
                        print(f"  {self._recv_str(self._server_socket)}")
                case 1:
                    print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED", file=sys.stderr)
                case _:
                    print("c> CONNECTED USERS FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECTED USERS FAIL", file=sys.stderr)

        self._server_socket.close()
        self._server_socket = None

    def quit(self):
        if self._connected_user:
            self.disconnect(self._connected_user)
        sys.exit(0)
