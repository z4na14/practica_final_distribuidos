import sys, socket, threading, time


class Client:
    MAX_MSG_SIZE = 255
    TIMEOUT = 10

    _server_socket = None
    _server_address = None
    _server_port = None
    _client_address = None
    _client_port = None

    _listening_thread = None
    _terminate = False

    _connected_user = None

    def __init__(self, address: str, port: int):
        self._server_address = address
        self._server_port = port

    def _get_connection(self):
        """Establece una nueva conexión con el servidor si no existe una activa"""
        if self._server_socket:
            return
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.connect((self._server_address, self._server_port))
        self._server_socket.settimeout(self.TIMEOUT)

    def _send(self, sock, msg: str):
        """Envía un mensaje terminado en \0 a través del socket"""
        sock.send(bytes(msg + "\x00", "utf_8"))

    def _recv_code(self, sock) -> int:
        """Recibe un byte de código de respuesta del servidor"""
        data = sock.recv(1)
        if not data:
            return -1
        return data[0]

    def _recv_str(self, sock) -> str:
        """Recibe una cadena terminada en \0 del servidor"""
        buf = bytearray()
        while True:
            c = sock.recv(1)
            if not c or c == b'\x00':
                break
            buf += c
        return buf.decode("utf_8")

    def _listen_server(self, username: str):
        """
        Función que ejecuta el hilo en background para mantener la conexión persistente
        y recibir mensajes del servidor.
        """
        # Primero buscamos un puerto libre
        temp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        temp_socket.bind(('', 0))  # Bind a puerto 0 para obtener uno libre
        self._client_port = temp_socket.getsockname()[1]
        temp_socket.close()

        # Conectamos al servidor para enviar la solicitud CONNECT
        self._get_connection()
        self._send(self._server_socket, f"CONNECT#{username}#{self._client_port}")

        try:
            response = self._recv_code(self._server_socket)
            # Cerramos esta conexión ya que solo era para enviar CONNECT
            self._server_socket.close()
            self._server_socket = None

            match response:
                case 0:
                    print("c> CONNECT OK")
                    self._connected_user = username

                    # Ahora creamos el socket de escucha en el puerto asignado
                    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as msg_lst_socket:
                        # Bind al puerto libre que encontramos, escuchando en todas las interfaces
                        msg_lst_socket.bind(("0.0.0.0", self._client_port))
                        msg_lst_socket.listen()
                        msg_lst_socket.settimeout(1.0)  # Timeout corto para poder verificar _terminate

                        # Mientras no se haya solicitado desconexión, seguir escuchando
                        while not self._terminate:
                            try:
                                connection, connected_address = msg_lst_socket.accept()

                                # Recibir el mensaje completo
                                msg = self._recv_str(connection)
                                connection.close()

                                if msg:
                                    self._parse_incoming_message(msg)
                            except socket.timeout:
                                # Timeout normal, continuar el loop para verificar _terminate
                                continue
                            except Exception:
                                # Error en la conexión, continuar
                                continue

                case 1:
                    print("c> CONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> USER ALREADY CONNECTED", file=sys.stderr)
                case _:
                    print("c> CONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECT FAIL", file=sys.stderr)

        # Resetear el flag de terminación para futuras conexiones
        self._terminate = False
        self._connected_user = None
        self._listening_thread = None

    def _parse_incoming_message(self, message: str):
        """
        Parsea los mensajes recibidos a través del socket de mensajes.
        """
        parts = message.split('#', 3)
        if not parts:
            return

        op = parts[0]
        if op == "SEND_MESSAGE" and len(parts) == 4:
            # Mensaje recibido de otro usuario
            _, sender, mid, text = parts
            print(f"MESSAGE {mid} FROM {sender}"
                  f"\n\t{text}\n\tEND", end="\nc> ")
        elif op == "SEND_MESS_ACK" and len(parts) == 2:
            # Confirmación de que nuestro mensaje fue entregado
            _, mid = parts
            print(f"SEND MESSAGE {mid} OK", end="\nc> ")

    def register(self, username: str):
        """
        Registra un usuario en el sistema.
        """
        self._get_connection()
        self._send(self._server_socket, f"REGISTER#{username}")

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
        """
        Da de baja un usuario del sistema.
        """
        self._get_connection()
        self._send(self._server_socket, f"UNREGISTER#{username}")

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
        """
        Conecta un usuario al sistema de mensajería.
        """
        # Si ya existe un hilo de escucha, no permitir nueva conexión
        if self._listening_thread:
            print("c> CONNECT FAIL, USER ALREADY CONNECTED", file=sys.stderr)
            return

        # Crear el hilo que gestionará la conexión
        self._listening_thread = threading.Thread(
            target=self._listen_server,
            args=(username,),
            daemon=True
        )
        self._listening_thread.start()

        # Pequeña espera para que el hilo imprima el resultado antes del siguiente prompt
        time.sleep(0.2)

    def disconnect(self, username: str):
        """
        Desconecta un usuario del sistema de mensajería.
        """
        # Evitar peticiones innecesarias si no hay conexión activa
        if not self._listening_thread:
            print("c> DISCONNECT FAIL, USER NOT CONNECTED", file=sys.stderr)
            return

        self._get_connection()
        self._send(self._server_socket, f"DISCONNECT#{username}")

        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    print("c> DISCONNECT OK")
                    # Señalar al hilo que debe terminar
                    self._terminate = True
                    self._connected_user = None
                    # Esperar a que el hilo termine
                    if self._listening_thread:
                        self._listening_thread.join(timeout=2.0)
                    self._listening_thread = None
                case 1:
                    print("c> DISCONNECT FAIL, USER DOES NOT EXIST", file=sys.stderr)
                case 2:
                    print("c> DISCONNECT FAIL, USER NOT CONNECTED", file=sys.stderr)
                case _:
                    print("c> DISCONNECT FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> DISCONNECT FAIL", file=sys.stderr)
            # Aunque falle, terminar el hilo de escucha como indica la especificación (Sección 6.5)
            self._terminate = True
            if self._listening_thread:
                self._listening_thread.join(timeout=2.0)
            self._listening_thread = None

        self._server_socket.close()
        self._server_socket = None

    def send(self, username: str, message: str):
        """
        Envía un mensaje a otro usuario.
        """
        # Evitar peticiones innecesarias si no estamos conectados
        if not self._connected_user:
            print("c> SEND FAIL, USER NOT CONNECTED", file=sys.stderr)
            return

        self._get_connection()
        self._send(self._server_socket, f"SEND#{self._connected_user}#{username}#{message}")

        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    # En caso de éxito, recibir el ID del mensaje
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
        """Funcionalidad de envío de archivos adjuntos - Parte 2 de la práctica"""
        pass

    def users(self):
        """
        Solicita la lista de usuarios conectados.
        """
        self._get_connection()

        # Enviar el nombre del usuario conectado, o cadena vacía si no hay usuario conectado
        name = self._connected_user if self._connected_user else ""
        self._send(self._server_socket, f"USERS#{name}")

        try:
            response = self._recv_code(self._server_socket)
            match response:
                case 0:
                    # Recibir cantidad de usuarios conectados
                    count = int(self._recv_str(self._server_socket))
                    print(f"c> CONNECTED USERS ({count} users connected) OK")
                    # Recibir y mostrar cada usuario
                    for _ in range(count):
                        user = self._recv_str(self._server_socket)
                        print(f"  {user}")
                case 1:
                    print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED", file=sys.stderr)
                case _:
                    print("c> CONNECTED USERS FAIL", file=sys.stderr)
        except socket.timeout:
            print("c> CONNECTED USERS FAIL", file=sys.stderr)

        self._server_socket.close()
        self._server_socket = None

    def quit(self):
        """
        Finaliza la ejecución del cliente.
        Si hay un usuario conectado, lo desconecta primero.
        """
        if self._connected_user:
            self.disconnect(self._connected_user)
        sys.exit(0)
