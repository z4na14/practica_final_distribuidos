import socket
import sys
import threading

import requests


class Client:
    MAX_MSG_SIZE = 255
    TIMEOUT = 10

    def __init__(self, address: str, port: int):
        self._server_address = address
        self._server_main_port = port
        self._server_ws_port = 3000
        self._client_port = None
        self._listening_thread = None
        self._terminate = False
        self._connected_user = None

    def _get_connection(self):
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.connect((self._server_address, self._server_main_port))
        server_socket.settimeout(self.TIMEOUT)
        return server_socket

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
            if not c or c == b"\x00":
                break
            buf += c
        return buf.decode("utf_8")

    def _listen_server(self, username: str):
        temp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        temp_socket.bind(("", 0))  # puerto 0 = el SO elige uno libre
        self._client_port = temp_socket.getsockname()[1]
        temp_socket.close()

        server_socket = self._get_connection()
        self._send(server_socket, f"CONNECT#{username}#{self._client_port}")

        try:
            response = self._recv_code(server_socket)
            server_socket.close()

            match response:
                case 0:
                    print("c> CONNECT OK")
                    self._connected_user = username

                    with socket.socket(
                        socket.AF_INET, socket.SOCK_STREAM
                    ) as msg_lst_socket:
                        msg_lst_socket.bind(("0.0.0.0", self._client_port))
                        msg_lst_socket.listen()
                        msg_lst_socket.settimeout(self.TIMEOUT)  # para poder comprobar _terminate; sin esto accept() bloquea indefinidamente

                        while not self._terminate:
                            try:
                                connection, _ = msg_lst_socket.accept()
                                msg = self._recv_str(connection)
                                connection.close()
                                if msg:
                                    self._parse_incoming_message(msg)
                            except socket.timeout:
                                continue

                case 1:
                    print("c> CONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> USER ALREADY CONNECTED", file=sys.stderr)
                case _:
                    print("c> CONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECT FAIL", file=sys.stderr)

        self._terminate = False
        self._connected_user = None
        self._listening_thread = None

    def _parse_incoming_message(self, message: str):
        parts = message.split("#", 3)
        if not parts:
            return

        codigo_op = parts[0]
        if codigo_op == "SEND_MESSAGE" and len(parts) == 4:
            _, sender, m_id, text = parts
            print(f"MESSAGE {m_id} FROM {sender}\n\t{text}\n\tEND", end="\nc> ")
        elif codigo_op == "SEND_MESS_ACK" and len(parts) == 2:
            _, m_id = parts
            print(f"SEND MESSAGE {m_id} OK", end="\nc> ")
        elif codigo_op == "SEND_MESSAGE_ATTACH":
            pass
        elif codigo_op == "SEND_MESS_ATTACH_ACK":
            pass
        elif codigo_op == "GETFILE":
            pass

    def _get_file(self):
        pass

    def _normalize_message(self, message: str) -> str:
        try:
            resp = requests.post(
                f'http://{self._server_address}:{self._server_ws_port}/quitar-espacios',
                json={'cadena': message},
                timeout=2
            )
            if resp.status_code == 200:
                return resp.text
        except Exception:
            pass  # si el web service no está disponible, mandamos el mensaje tal cual
        return message

    def register(self, username: str):
        server_socket = self._get_connection()
        self._send(server_socket, f"REGISTER#{username}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    print("c> REGISTER OK")
                case 1:
                    print("c> USERNAME IN USE", file=sys.stderr)
                case _:
                    print("c> REGISTER FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> REGISTER FAIL", file=sys.stderr)

        server_socket.close()

    def unregister(self, username: str):
        server_socket = self._get_connection()
        self._send(server_socket, f"UNREGISTER#{username}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    print("c> UNREGISTER OK")
                case 1:
                    print("c> USER DOES NOT EXIST", file=sys.stderr)
                case _:
                    print("c> UNREGISTER FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> UNREGISTER FAIL", file=sys.stderr)

        server_socket.close()

    def connect(self, username: str):
        if self._listening_thread:
            print("c> CONNECT FAIL, USER ALREADY CONNECTED", file=sys.stderr)
            return

        self._listening_thread = threading.Thread(
            target=self._listen_server, args=(username,)
        )
        self._listening_thread.start()

    def disconnect(self, username: str):
        if not self._listening_thread:
            print("c> DISCONNECT FAIL, USER NOT CONNECTED", file=sys.stderr)
            return

        server_socket = self._get_connection()
        self._send(server_socket, f"DISCONNECT#{username}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    print("c> DISCONNECT OK")
                    self._terminate = True
                    self._connected_user = None
                    self._listening_thread.join()
                    self._listening_thread = None
                case 1:
                    print("c> DISCONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> DISCONNECT FAIL, USER NOT CONNECTED", file=sys.stderr)
                case _:
                    print("c> DISCONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> DISCONNECT FAIL", file=sys.stderr)
            self._terminate = True  # aunque el servidor no responda, cerramos el hilo
            if self._listening_thread:
                self._listening_thread.join()
                self._listening_thread = None

        server_socket.close()

    def send(self, username: str, message: str):
        if not self._connected_user:
            print("c> SEND FAIL, USER NOT CONNECTED", file=sys.stderr)
            return

        message = self._normalize_message(message)
        server_socket = self._get_connection()
        self._send(
            server_socket, f"SEND#{self._connected_user}#{username}#{message}"
        )

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    mid = self._recv_str(server_socket)
                    print(f"c> SEND OK - MESSAGE {mid}")
                case 1:
                    print("c> SEND FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case _:
                    print("c> SEND FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> SEND FAIL", file=sys.stderr)

        server_socket.close()

    def send_attach(self, username: str, message: str, filename: str):
        """
        Envio de archivos adjuntos a otro cliente
        """
        if (filename[0] != '/'):
            # Los paths tienen que ser absolutos
            print("c> SENDATTACH FAIL", file=sys.stderr)
            return
        
        server_socket = self._get_connection()

        self._send(server_socket, f"SENDATTACH#{self._connected_user}#{username}#{message}#{filename}")
        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    mid = self._recv_str(server_socket)
                    print(f"c> SENDATTACH OK - MESSAGE {mid}")
                case 1:
                    print("c> SENDATTACH FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case _:
                    print("c> SENDATTACH FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> SENDATTACH FAIL", file=sys.stderr)

        server_socket.close()
        server_socket = None

    def users(self):
        server_socket = self._get_connection()

        username = self._connected_user if self._connected_user else ""
        self._send(server_socket, f"USERS#{username}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    count = int(self._recv_str(server_socket))
                    print(f"c> CONNECTED USERS ({count} users connected) OK")
                    for _ in range(count):
                        user = self._recv_str(server_socket)
                        print(f"\t{user}")
                case 1:
                    print(
                        "c> CONNECTED USERS FAIL, USER IS NOT CONNECTED",
                        file=sys.stderr,
                    )
                case _:
                    print("c> CONNECTED USERS FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECTED USERS FAIL", file=sys.stderr)

        server_socket.close()

    def quit(self):
        if self._connected_user:
            self.disconnect(self._connected_user)
        sys.exit(0)
