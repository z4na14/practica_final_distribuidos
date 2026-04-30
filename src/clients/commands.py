import sys, socket

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

    server_socket = None
    address = None
    port = None

    def __init__(self, address: str, port: int):
        self.address = address
        self.port = port

    def get_connection(self):
        """Creates the client socket and initializes the connection with the server."""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.connect((self.address, self.port))
        self.server_socket.settimeout(self.TIMEOUT)

    def register(self, username: str):
        self.get_connection()

        self.server_socket.send(bytes(f"{1}#{username}", "utf_8"))
        try:
            response = int(self.server_socket.recv(self.MAX_MSG_SIZE))
            match response:
                case 0: print("c> REGISTER OK")
                case 1: print("c> USERNAME IN USE", file=sys.stderr)
                case _: print("c> REGISTER FAIL"  , file=sys.stderr)
        except socket.timeout:
            print("c> REGISTER FAIL", file=sys.stderr)

    def unregister(self, username: str):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(self.TIMEOUT)
            sock.connect((self.address, self.port))

            sock.send(bytes(f"{2}#{username}", "utf_8"))

            try:
                response = int(sock.recv(self.MAX_MSG_SIZE))
                match response:
                    case 0: print("c> UNREGISTER OK")
                    case 1: print("c> USER DOES NOT EXIST", file=sys.stderr)
                    case _: print("c> UNREGISTER FAIL"    , file=sys.stderr)

            except socket.timeout:
                print("c> UNREGISTER FAIL", file=sys.stderr)
    
    def connect(self, username: str):
        pass
    
    def disconnect(self, username: str):
        pass

    
    def send(self, username: str, message: str):
        pass
    
    def send_attach(self):
        pass
    
    def users(self):
        pass

    @staticmethod
    def quit():
        sys.exit(0)