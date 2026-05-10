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
        self._connected_users_info = {}

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
                    print("\rc> CONNECT OK", end="\nc> ")
                    self._connected_user = username

                    with socket.socket(
                        socket.AF_INET, socket.SOCK_STREAM
                    ) as msg_lst_socket:
                        msg_lst_socket.bind(("0.0.0.0", self._client_port))
                        msg_lst_socket.listen()
                        msg_lst_socket.settimeout(self.TIMEOUT)  # sin timeout accept() bloquea forever y nunca salimos del bucle

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
                    print(
                        "\rc> CONNECT FAIL, USER DOES NOT EXIST",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case 2:
                    print(
                        "\rc> USER ALREADY CONNECTED",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case _:
                    print("\rc> CONNECT FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> CONNECT FAIL", file=sys.stderr, end="\nc> ")

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
            print(f"\rc> MESSAGE {m_id} FROM {sender}\n\t{text}\n\tEND", end="\nc> ")
        elif codigo_op == "SEND_MESS_ACK" and len(parts) == 2:
            _, m_id = parts
            print(f"\rc> SEND MESSAGE {m_id} OK", end="\nc> ")
        elif codigo_op == "SEND_MESSAGE_ATTACH":
            # 5 campos, no 4; con maxsplit=3 el filename quedaría pegado al mensaje
            attach_parts = message.split("#", 4)
            if len(attach_parts) == 5:
                _, sender, m_id, text, file_name = attach_parts
                print(
                    f"\rc> MESSAGE {m_id} FROM {sender}\n\t{text}\n\tEND\n\tFILE {file_name}",
                    end="\nc> ",
                )
        elif codigo_op == "SEND_MESS_ATTACH_ACK" and len(parts) == 3:
            _, m_id, file_name = parts
            print(f"\rc> SEND MESSAGE {m_id} {file_name} OK", end="\nc> ")

        elif codigo_op == "GETFILE":
            getfile_parts = message.split("#", 4)
            if len(getfile_parts) == 5:
                _, requester, filename, req_ip, req_port = getfile_parts
                threading.Thread(
                    target=self._handle_send_file,
                    args=(filename, req_ip, int(req_port)),
                ).start()

    def _handle_send_file(self, filename: str, target_ip: str, target_port: int):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((target_ip, target_port))
            with open(filename, "rb") as f:
                while True:
                    chunk = f.read(4096)
                    if not chunk:
                        break
                    sock.sendall(chunk)
            sock.close()
        except Exception:
            pass

    def _handle_get_file(self, recv_socket: socket.socket, local_filename: str):
        try:
            recv_socket.settimeout(self.TIMEOUT)
            conn, _ = recv_socket.accept()
            with open(local_filename, "wb") as f:
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    f.write(chunk)
            conn.close()
            recv_socket.close()
            print("\rc> GETFILE OK", end="\nc> ")
        except Exception:
            print("\rc> GETFILE FAIL", file=sys.stderr, end="\nc> ")
            recv_socket.close()

    def _update_users_info(self):
        server_socket = self._get_connection()
        username = self._connected_user if self._connected_user else ""
        self._send(server_socket, f"USERS#{username}")

        self._connected_users_info = {}
        try:
            response = self._recv_code(server_socket)
            if response == 0:
                count = int(self._recv_str(server_socket))
                for _ in range(count):
                    user_info = self._recv_str(server_socket)

                    parts = [p.strip() for p in user_info.replace(":", " ").split()]
                    if len(parts) >= 3:
                        u_name = parts[0]
                        if parts[1].isdigit():
                            port = int(parts[1])
                            ip = parts[2]
                        elif parts[2].isdigit():
                            port = int(parts[2])
                            ip = parts[1]
                        else:
                            continue
                        self._connected_users_info[u_name] = (ip, port)
        except socket.timeout:
            pass
        finally:
            server_socket.close()

    def _normalize_message(self, message: str) -> str:
        try:
            resp = requests.post(
                f"http://{self._server_address}:{self._server_ws_port}/quitar-espacios",
                json={"cadena": message},
                timeout=2,
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
                    print("\rc> REGISTER OK", end="\nc> ")
                case 1:
                    print("\rc> USERNAME IN USE", file=sys.stderr, end="\nc> ")
                case _:
                    print("\rc> REGISTER FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> REGISTER FAIL", file=sys.stderr, end="\nc> ")

        server_socket.close()

    def unregister(self, username: str):
        server_socket = self._get_connection()
        self._send(server_socket, f"UNREGISTER#{username}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    print("\rc> UNREGISTER OK", end="\nc> ")
                case 1:
                    print(
                        "\rc> USER DOES NOT EXIST",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case _:
                    print("\rc> UNREGISTER FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> UNREGISTER FAIL", file=sys.stderr, end="\nc> ")

        server_socket.close()

    def connect(self, username: str):
        if self._listening_thread:
            print(
                "\rc> CONNECT FAIL, USER ALREADY CONNECTED",
                file=sys.stderr,
                end="\nc> ",
            )
            return

        self._listening_thread = threading.Thread(
            target=self._listen_server, args=(username,)
        )
        self._listening_thread.start()

    def disconnect(self, username: str):
        server_socket = self._get_connection()
        self._send(server_socket, f"DISCONNECT#{username}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    print("\rc> DISCONNECT OK", end="\nc> ")
                    self._terminate = True
                    self._connected_user = None

                    if self._listening_thread:
                        self._listening_thread.join()
                        self._listening_thread = None
                case 1:
                    print(
                        "\rc> DISCONNECT FAIL, USER DOES NOT EXIST",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case 2:
                    print(
                        "\rc> DISCONNECT FAIL, USER NOT CONNECTED",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case _:
                    print("\rc> DISCONNECT FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> DISCONNECT FAIL", file=sys.stderr, end="\nc> ")
            self._terminate = True  # aunque el servidor no responda, cerramos el hilo
            if self._listening_thread:
                self._listening_thread.join()
                self._listening_thread = None

        server_socket.close()

    def send(self, username: str, message: str):
        if not self._connected_user:
            print("\rc> SEND FAIL, USER NOT CONNECTED", file=sys.stderr, end="\nc> ")
            return

        message = self._normalize_message(message)
        server_socket = self._get_connection()
        self._send(server_socket, f"SEND#{self._connected_user}#{username}#{message}")

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    mid = self._recv_str(server_socket)
                    print(f"\rc> SEND OK - MESSAGE {mid}", end="\nc> ")
                case 1:
                    print(
                        "\rc> SEND FAIL, USER DOES NOT EXIST",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case _:
                    print("\rc> SEND FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> SEND FAIL", file=sys.stderr, end="\nc> ")

        server_socket.close()

    def send_attach(self, username: str, message: str, filename: str):
        if filename[0] != "/":
            print("\rc> SENDATTACH FAIL", file=sys.stderr, end="\nc> ")
            return

        server_socket = self._get_connection()

        self._send(
            server_socket,
            f"SENDATTACH#{self._connected_user}#{username}#{message}#{filename}",
        )
        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    mid = self._recv_str(server_socket)
                    print(f"\rc> SENDATTACH OK - MESSAGE {mid}", end="\nc> ")
                case 1:
                    print(
                        "\rc> SENDATTACH FAIL, USER DOES NOT EXIST",
                        file=sys.stderr,
                        end="\nc> ",
                    )
                case _:
                    print("\rc> SENDATTACH FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> SENDATTACH FAIL", file=sys.stderr, end="\nc> ")

        server_socket.close()
        server_socket = None

    def users(self):
        server_socket = self._get_connection()

        username = self._connected_user if self._connected_user else ""
        self._send(server_socket, f"USERS#{username}")
        self._connected_users_info = {}

        try:
            response = self._recv_code(server_socket)
            match response:
                case 0:
                    count = int(self._recv_str(server_socket))
                    out = f"\rc> CONNECTED USERS ({count} users connected) OK"
                    for _ in range(count):
                        user_info = self._recv_str(server_socket)
                        out += f"\n\t{user_info}"
                        parts = [p.strip() for p in user_info.replace(":", " ").split()]
                        if len(parts) >= 3:
                            u_name = parts[0]
                            if parts[1].isdigit():
                                port = int(parts[1])
                                ip = parts[2]
                            elif parts[2].isdigit():
                                port = int(parts[2])
                                ip = parts[1]
                            else:
                                continue
                            self._connected_users_info[u_name] = (ip, port)
                            
                    print(out, end="\nc> ")
                case 1:
                    print("\rc> CONNECTED USERS FAIL, USER IS NOT CONNECTED", end="\nc> ", file=sys.stderr)
                case _:
                    print("\rc> CONNECTED USERS FAIL", file=sys.stderr, end="\nc> ")
        except socket.timeout:
            print("\rc> CONNECTED USERS FAIL", file=sys.stderr, end="\nc> ")

        server_socket.close()
        
    def quit(self):
        if self._connected_user:
            self.disconnect(self._connected_user)
        sys.exit(0)

    def get_file(self, username: str, remote_filename: str, local_filename: str):
        if not self._connected_user:
            print("\rc> GETFILE FAIL", file=sys.stderr, end="\nc> ")
            return

        if username not in self._connected_users_info:
            self._update_users_info()

        if username not in self._connected_users_info:
            print(
                "\rc> FILE TRANSFER FAILED, user not connected.",
                file=sys.stderr,
                end="\nc> ",
            )
            return

        sender_ip, sender_port = self._connected_users_info[username]

        recv_socket = None

        try:
            recv_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            recv_socket.bind(("", 0))
            recv_socket.listen()
            my_port = recv_socket.getsockname()[1]

            # UDP no manda nada, pero el SO elige la interfaz de red correcta
            # y así obtenemos nuestra IP vista desde el servidor
            temp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                temp_sock.connect((self._server_address, self._server_main_port))
                my_ip = temp_sock.getsockname()[0]
            except Exception:
                my_ip = "127.0.0.1"
            finally:
                temp_sock.close()

            thread = threading.Thread(
                target=self._handle_get_file, args=(recv_socket, local_filename)
            )
            thread.start()

            req_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            req_socket.settimeout(self.TIMEOUT)
            req_socket.connect((sender_ip, sender_port))

            msg = f"GETFILE#{self._connected_user}#{remote_filename}#{my_ip}#{my_port}"
            self._send(req_socket, msg)
            req_socket.close()

        except Exception:
            print("\rc> GETFILE FAIL", file=sys.stderr, end="\nc> ")
            if recv_socket:
                recv_socket.close()
