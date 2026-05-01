import sys, socket, threading


# List of commands
# - QUIT
# - REGISTER (1)
# - UNREGISTER (2)
# - CONNECT (3)
# - DISCONNECT (4)
# - SEND (5)
# - SENDATTACH (6)
# - USERS (7)

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
        """Creates the client socket and initializes the connection with the server."""
        if self._server_socket: return

        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.connect((self._address, self._port))
        self._server_socket.settimeout(self.TIMEOUT)

    ### --- MESSAGES THREAD HELPERS ---
    def _listen_server(self, username: str):
        """Running function for the messages thread running in the background"""
        if self._messages_socket: return

        self._messages_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._messages_socket.connect((self._address, self._port))
        self._messages_socket.settimeout(self.TIMEOUT)

        self._messages_socket.send(bytes(f"3#{username}", "utf_8"))
        try:
            response = int(self._messages_socket.recv(self.MAX_MSG_SIZE))
            match response:
                case 0:
                    print("c> CONNECT OK")
                    self._connected_user = username

                    # While waiting for the disconnect, keep running the loop
                    while not self._terminate: self._parse_incoming_message(
                        str(self._messages_socket.recv(self.MAX_MSG_SIZE)))

                case 1:
                    print("c> CONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> CONNECT FAIL, USER ALREADY CONNECTED", file=sys.stderr)
                case _:
                    print("c> CONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECT FAIL", file=sys.stderr)


        # Close it before clearing local variable lol
        self._messages_socket.close()

        # Toggle conditions
        self._terminate = False
        self._messages_socket = None

        return

    def _parse_incoming_message(self, message: str):
        pass

    ### ---

    def register(self, username: str):
        self._get_connection()

        self._server_socket.send(bytes(f"1#{username}", "utf_8"))
        try:
            response = int(self._server_socket.recv(self.MAX_MSG_SIZE))
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

        self._server_socket.send(bytes(f"2#{username}", "utf_8"))
        try:
            response = int(self._server_socket.recv(self.MAX_MSG_SIZE))
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
        # If already existing thread, ignore instruction
        if self._listening_thread: return
        # Create thread and start it
        self._listening_thread = threading.Thread(target=self._listen_server, args=(username,))
        self._listening_thread.start()

    def disconnect(self, username: str):
        pass

    def send(self, username: str, message: str):
        pass

    def send_attach(self):
        pass

    def users(self):
        pass

    def quit(self):
        self.disconnect(self._connected_user)
        sys.exit(0)
