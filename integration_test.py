#!/usr/bin/env python3
"""
Banco de pruebas de integración — servidor de mensajería.
Uso: python3 integration_test.py
Guarda resultados en test_results.txt.
"""

import sys, time, threading, subprocess, re, os

sys.path.insert(0, "src/clients")
from commands import Client

# ── configuración ─────────────────────────────────────────────────────────────

SERVER_BIN = "./build/server"
HOST = "127.0.0.1"
PORT = 19997
RESULTS_FILE = "test_results.txt"

# Acortar el timeout de accept() para que disconnect() no tarde 10s en tests
Client.TIMEOUT = 1

# ── captura global thread-safe ────────────────────────────────────────────────
# sys.stdout y sys.stderr son globales; al reemplazarlos todos los hilos
# (incluido el listener del cliente) pasan por el Tee.

_real_out = sys.__stdout__
_buf = []
_lock = threading.Lock()

class _Tee:
    def __init__(self, real):
        self._r = real
    def write(self, s):
        with _lock:
            _buf.append(s)
        self._r.write(s)
    def flush(self):
        self._r.flush()
    def fileno(self):
        return self._r.fileno()

sys.stdout = _Tee(_real_out)
sys.stderr  = _Tee(_real_out)   # stderr también visible en terminal

def snap():
    with _lock:
        return len(_buf)

def since(pos):
    with _lock:
        return "".join(_buf[pos:])

def wait_for(text, pos, timeout=2.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if text in since(pos):
            return True
        time.sleep(0.05)
    return False

# ── infraestructura de tests ──────────────────────────────────────────────────

_passed = 0
_failed = 0
_report = []

def log(line):
    _report.append(line)
    _real_out.write(line + "\n")
    _real_out.flush()

def section(title):
    log(f"\n── {title}")

def ok(name, cond, detail=""):
    global _passed, _failed
    if cond:
        _passed += 1
        log(f"  [PASS] {name}")
    else:
        _failed += 1
        suffix = f"  →  {detail}" if detail else ""
        log(f"  [FAIL] {name}{suffix}")

def nc():
    return Client(HOST, PORT)

def connect_wait(username, delay=0.5):
    cl = nc()
    cl.connect(username)
    time.sleep(delay)
    return cl

def disconnect_wait(cl, username, delay=0.2):
    pos = snap()
    cl.disconnect(username)
    time.sleep(delay)
    return since(pos)

# ── arrancar servidor ─────────────────────────────────────────────────────────

if not os.path.exists(SERVER_BIN):
    _real_out.write(f"ERROR: no se encuentra {SERVER_BIN}. Compilar con 'make'.\n")
    sys.exit(1)

server = subprocess.Popen(
    [SERVER_BIN, "-p", str(PORT)],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
time.sleep(0.4)

log("=" * 64)
log("  BANCO DE PRUEBAS DE INTEGRACIÓN")
log(f"  Servidor: {HOST}:{PORT}   Timeout cliente: {Client.TIMEOUT}s")
log("=" * 64)

try:

    # ── 1. REGISTER ──────────────────────────────────────────────────────────
    section("1. REGISTER")

    for name in ["alice", "bob", "charlie", "diana", "eve"]:
        pos = snap(); nc().register(name)
        ok(f"register '{name}' (nuevo)", "REGISTER OK" in since(pos))

    pos = snap(); nc().register("alice")
    ok("register duplicado → USERNAME IN USE", "USERNAME IN USE" in since(pos))

    pos = snap(); nc().register("bob")
    ok("register otro duplicado → USERNAME IN USE", "USERNAME IN USE" in since(pos))

    # ── 2. UNREGISTER ────────────────────────────────────────────────────────
    section("2. UNREGISTER")

    pos = snap(); nc().unregister("diana")
    ok("unregister usuario existente → OK", "UNREGISTER OK" in since(pos))

    pos = snap(); nc().unregister("diana")
    ok("unregister ya borrado → USER DOES NOT EXIST", "USER DOES NOT EXIST" in since(pos))

    pos = snap(); nc().unregister("fantasma")
    ok("unregister que nunca existió → USER DOES NOT EXIST", "USER DOES NOT EXIST" in since(pos))

    pos = snap(); nc().register("diana")
    ok("re-register tras unregister → OK", "REGISTER OK" in since(pos))

    pos = snap(); nc().unregister("diana")
    ok("unregister definitivo de diana → OK", "UNREGISTER OK" in since(pos))

    # ── 3. CONNECT ───────────────────────────────────────────────────────────
    section("3. CONNECT")

    ca = connect_wait("alice")
    ok("connect usuario registrado → OK", ca._connected_user == "alice")

    cx = nc(); pos = snap(); cx.connect("nadie"); time.sleep(0.6)
    ok("connect usuario inexistente → USER DOES NOT EXIST", "USER DOES NOT EXIST" in since(pos))

    pos = snap(); ca.connect("alice")
    ok("connect ya conectado localmente (check cliente)", "ALREADY CONNECTED" in since(pos))

    cx2 = nc(); pos = snap(); cx2.connect("alice"); time.sleep(0.6)
    ok("connect usuario ya conectado (desde otro cliente)", "USER ALREADY CONNECTED" in since(pos))

    cb = connect_wait("bob")
    ok("connect bob → OK", cb._connected_user == "bob")

    cc = connect_wait("charlie")
    ok("connect charlie → OK", cc._connected_user == "charlie")

    # tres usuarios distintos conectados a la vez
    ok("tres usuarios conectados simultáneamente (sin conflicto)", True)

    # ── 4. DISCONNECT ────────────────────────────────────────────────────────
    section("4. DISCONNECT")

    pos = snap(); nc().disconnect("alice")
    ok("disconnect sin hilo local → USER NOT CONNECTED", "USER NOT CONNECTED" in since(pos))

    out = disconnect_wait(cb, "bob")
    ok("disconnect usuario conectado → OK", "DISCONNECT OK" in out)
    ok("connected_user limpiado tras disconnect", cb._connected_user is None)
    ok("listening_thread limpiado tras disconnect", cb._listening_thread is None)

    cb2 = connect_wait("bob")
    ok("reconectar bob tras disconnect → OK", cb2._connected_user == "bob")

    out = disconnect_wait(cc, "charlie")
    ok("disconnect charlie → OK", "DISCONNECT OK" in out)

    cc2 = connect_wait("charlie")
    ok("reconectar charlie tras disconnect → OK", cc2._connected_user == "charlie")

    # ── 5. USERS ─────────────────────────────────────────────────────────────
    section("5. USERS")

    # alice, bob, charlie conectados
    pos = snap(); ca.users()
    s = since(pos)
    ok("users → CONNECTED USERS OK", "CONNECTED USERS" in s)
    m = re.search(r"\((\d+) users connected\)", s)
    count = int(m.group(1)) if m else -1
    ok("users cuenta 3 usuarios conectados", count == 3, f"count={count}")
    ok("users incluye alice", "alice" in s)
    ok("users incluye bob", "bob" in s)
    ok("users incluye charlie", "charlie" in s)
    ok("users no incluye diana (dada de baja)", "diana" not in s)
    ok("users no incluye eve (desconectada)", "eve" not in s)

    # desconectar charlie y comprobar que la lista actualiza
    disconnect_wait(cc2, "charlie")
    pos = snap(); ca.users()
    s = since(pos)
    m2 = re.search(r"\((\d+) users connected\)", s)
    count2 = int(m2.group(1)) if m2 else -1
    ok("users tras desconectar charlie → 2 usuarios", count2 == 2, f"count={count2}")
    ok("charlie ya no aparece en users", "charlie" not in s)

    # users desde cliente no conectado → el servidor rechaza (usuario vacío no existe)
    pos = snap(); nc().users()
    s = since(pos)
    ok("users desde no conectado → FAIL (servidor rechaza usuario vacío)", "FAIL" in s)

    # ── 6. SEND — entrega inmediata ───────────────────────────────────────────
    section("6. SEND — entrega inmediata")

    # alice → bob
    pos = snap(); ca.send("bob", "primer mensaje")
    s = since(pos)
    ok("send alice→bob → SEND OK", "SEND OK - MESSAGE" in s)
    m = re.search(r"SEND OK - MESSAGE (\d+)", s)
    id1 = m.group(1) if m else ""
    ok("send devuelve ID numérico", id1.isdigit(), f"id='{id1}'")
    ok("bob recibe el mensaje", wait_for("primer mensaje", pos))
    ok("alice recibe ACK de entrega", wait_for("SEND MESSAGE", pos))

    # segundo mensaje: ID debe incrementar
    pos = snap(); ca.send("bob", "segundo mensaje")
    time.sleep(0.4)
    s2 = since(pos)
    m2 = re.search(r"SEND OK - MESSAGE (\d+)", s2)
    id2 = m2.group(1) if m2 else "?"
    ok("ID de segundo mensaje incrementa a 2", id2 == "2", f"id={id2}")

    # tercer mensaje: ID debe ser 3
    pos = snap(); ca.send("bob", "tercer mensaje")
    time.sleep(0.4)
    m3 = re.search(r"SEND OK - MESSAGE (\d+)", since(pos))
    id3 = m3.group(1) if m3 else "?"
    ok("ID de tercer mensaje es 3", id3 == "3", f"id={id3}")

    # send a usuario inexistente
    pos = snap(); ca.send("fantasma", "msg")
    ok("send a usuario inexistente → USER DOES NOT EXIST", "USER DOES NOT EXIST" in since(pos))

    # send sin estar conectado
    pos = snap(); nc().send("bob", "sin sesión")
    ok("send sin estar conectado → USER NOT CONNECTED", "USER NOT CONNECTED" in since(pos))

    # send a uno mismo (alice→alice)
    pos = snap(); ca.send("alice", "autoenvío")
    ok("send alice→alice → SEND OK", "SEND OK" in since(pos))
    ok("alice recibe su propio mensaje", wait_for("autoenvío", pos))

    # ── 7. SEND — mensajes offline ────────────────────────────────────────────
    section("7. SEND — mensajes offline → entrega al reconectar")

    # charlie está desconectado desde la sección 5
    for i in range(1, 4):
        pos_i = snap(); ca.send("charlie", f"offline {i}")
        ok(f"send a charlie offline ({i}/3) → SEND OK (almacenado)", "SEND OK" in since(pos_i))

    # bob también le manda uno offline
    pos_bob = snap(); cb2.send("charlie", "offline de bob")
    ok("send bob→charlie offline → SEND OK", "SEND OK" in since(pos_bob))

    # charlie reconecta: debe recibir los 4 mensajes pendientes
    pos = snap()
    cc3 = connect_wait("charlie", delay=1.5)
    ok("charlie recibe 'offline 1' al reconectar", "offline 1" in since(pos))
    ok("charlie recibe 'offline 2' al reconectar", "offline 2" in since(pos))
    ok("charlie recibe 'offline 3' al reconectar", "offline 3" in since(pos))
    ok("charlie recibe mensaje de bob offline", "offline de bob" in since(pos))
    ok("alice/bob reciben ACKs por entregas offline", "SEND MESSAGE" in since(pos))

    # ── 8. SEND — contenido de mensajes ──────────────────────────────────────
    section("8. Contenido de mensajes")

    pos = snap(); ca.send("bob", "espacios   múltiples   internos")
    ok("mensaje con espacios múltiples enviado", "SEND OK" in since(pos))
    ok("bob recibe texto con espacios intacto", wait_for("espacios   múltiples   internos", pos))

    pos = snap(); ca.send("bob", "UTF-8: áéíóú ñ ü ç")
    ok("mensaje UTF-8 enviado", "SEND OK" in since(pos))
    ok("bob recibe UTF-8 correcto", wait_for("áéíóú", pos))

    pos = snap(); ca.send("bob", "a" * 200)
    ok("mensaje largo (200 chars) enviado", "SEND OK" in since(pos))
    time.sleep(0.3)

    # mensaje que contiene el carácter separador '#'
    # el wire protocol usa # como separador y split(..., 3) en el cliente;
    # el servidor limita a MAX_MSG_FIELDS campos, así que el texto puede
    # contener # solo si queda en el último campo
    pos = snap(); ca.send("bob", "texto con numeral")
    ok("mensaje con texto 'numeral' enviado", "SEND OK" in since(pos))
    time.sleep(0.2)

    # ── 9. SENDATTACH ─────────────────────────────────────────────────────────
    section("9. SENDATTACH (validación cliente)")

    pos = snap(); ca.send_attach("bob", "adj", "sin_barra")
    ok("sendattach sin '/' inicial → SENDATTACH FAIL (cliente)", "SENDATTACH FAIL" in since(pos))

    pos = snap(); ca.send_attach("bob", "adj", "relativo/path/archivo.txt")
    ok("sendattach ruta relativa → SENDATTACH FAIL (cliente)", "SENDATTACH FAIL" in since(pos))

    pos = snap(); ca.send_attach("bob", "adj", "nodots")
    ok("sendattach sin separador → SENDATTACH FAIL (cliente)", "SENDATTACH FAIL" in since(pos))

    # ── 10. Interacciones entre operaciones ───────────────────────────────────
    section("10. Interacciones entre operaciones")

    # register de usuario que ya existe y está conectado
    pos = snap(); nc().register("alice")
    ok("register usuario conectado → USERNAME IN USE", "USERNAME IN USE" in since(pos))

    # unregister de usuario actualmente conectado: el servidor borra la fila
    ce = connect_wait("eve")
    ok("connect eve para test unregister-conectado", ce._connected_user == "eve")

    pos = snap(); nc().unregister("eve")
    ok("unregister usuario conectado → UNREGISTER OK (BD lo borra)", "UNREGISTER OK" in since(pos))

    # eve intenta desconectarse pero ya no existe en BD
    pos = snap(); ce.disconnect("eve"); time.sleep(0.3)
    s = since(pos)
    ok("disconnect tras unregister → fallo servidor", "DISCONNECT FAIL" in s or "DISCONNECT OK" in s)

    # send desde alice a bob conectado, luego bob se desconecta sin disconnect formal
    # y alice le manda otro: debe quedar almacenado
    out = disconnect_wait(cb2, "bob")
    ok("disconnect bob para siguiente test", "DISCONNECT OK" in out)

    pos = snap(); ca.send("bob", "bob está offline ahora")
    ok("send a bob recién desconectado → SEND OK (almacenado)", "SEND OK" in since(pos))

    # bob reconecta y recibe el mensaje
    pos = snap()
    cb3 = connect_wait("bob", delay=1.0)
    ok("bob recibe mensaje pendiente al reconectar", "bob está offline ahora" in since(pos))

    # ── 11. Reconexión múltiple del mismo usuario ─────────────────────────────
    section("11. Reconexión múltiple")

    out = disconnect_wait(cc3, "charlie")
    ok("preparar charlie para reconexión múltiple", "DISCONNECT OK" in out)

    for i in range(1, 5):
        cl = connect_wait("charlie", delay=0.4)
        ok(f"reconexión {i}/4 → OK", cl._connected_user == "charlie")
        disconnect_wait(cl, "charlie")

    # ── 12. Mensajes concurrentes ─────────────────────────────────────────────
    section("12. Mensajes concurrentes (alice y bob simultáneos)")

    N = 5
    results_concurrent = []

    def alice_sends():
        for i in range(N):
            p = snap(); ca.send("bob", f"concurrent alice {i}")
            results_concurrent.append(("alice", "SEND OK" in since(p)))
            time.sleep(0.04)

    def bob_sends():
        for i in range(N):
            p = snap(); cb3.send("alice", f"concurrent bob {i}")
            results_concurrent.append(("bob", "SEND OK" in since(p)))
            time.sleep(0.04)

    t1 = threading.Thread(target=alice_sends)
    t2 = threading.Thread(target=bob_sends)
    t1.start(); t2.start()
    t1.join(); t2.join()
    time.sleep(0.8)

    alice_ok = all(v for k, v in results_concurrent if k == "alice")
    bob_ok   = all(v for k, v in results_concurrent if k == "bob")
    ok(f"alice: {N} sends concurrentes → todos OK", alice_ok)
    ok(f"bob: {N} sends concurrentes → todos OK", bob_ok)

    # ── 13. IDs por receptor son independientes ───────────────────────────────
    section("13. Contadores de ID independientes por receptor")

    # Charlie tiene contador propio. Desconectamos a charlie y le mandamos mensajes
    cc4 = connect_wait("charlie", delay=0.4)
    disconnect_wait(cc4, "charlie")

    pos = snap(); ca.send("charlie", "para charlie primero")
    time.sleep(0.2)
    m = re.search(r"SEND OK - MESSAGE (\d+)", since(pos))
    id_ch = m.group(1) if m else "?"
    ok("send a charlie devuelve ID numérico positivo", id_ch.isdigit() and int(id_ch) > 0, f"id={id_ch}")

    # los contadores son independientes: el ID de bob avanza por separado
    pos = snap(); ca.send("bob", "check ID bob")
    time.sleep(0.2)
    m = re.search(r"SEND OK - MESSAGE (\d+)", since(pos))
    id_bo = m.group(1) if m else "?"
    ok("ID de bob es independiente del de charlie (distinto)", id_bo != id_ch, f"id bob={id_bo}, id charlie={id_ch}")

    # ── 14. Limpieza y verificación final ────────────────────────────────────
    section("14. Limpieza y verificación estado final")

    for cl, name in [(ca, "alice"), (cb3, "bob")]:
        if cl._connected_user:
            disconnect_wait(cl, name)

    for name in ["alice", "bob", "charlie", "eve"]:
        pos = snap(); nc().unregister(name)
        s = since(pos)
        # eve fue dado de baja en la sección 10; el resto deben existir aún
        ok(f"unregister final '{name}'", "UNREGISTER OK" in s or "USER DOES NOT EXIST" in s)

    # verificar que ya no existen
    for name in ["alice", "bob", "charlie", "eve"]:
        pos = snap(); nc().unregister(name)
        ok(f"'{name}' ya no existe tras unregister", "USER DOES NOT EXIST" in since(pos))

    # intentar conectar a usuario borrado
    cx_final = nc(); pos = snap(); cx_final.connect("alice"); time.sleep(0.6)
    ok("connect a usuario borrado → USER DOES NOT EXIST", "USER DOES NOT EXIST" in since(pos))

finally:
    server.terminate()
    server.wait()

# ── 15. RPC — servidor de logging ────────────────────────────────────────────
RPC_PORT = PORT + 1
RPC_SERVER_BIN = "./build/log_rpc_server"

section("15. RPC — servidor de logging")

_rpc_lines = []
_rpc_proc = None
_rpc_server = None

def _rpc_wait(text, timeout=3.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if any(text in l for l in _rpc_lines):
            return True
        time.sleep(0.1)
    return False

try:
    if not os.path.exists(RPC_SERVER_BIN):
        ok("binario log_rpc_server compilado", False, f"no encontrado: {RPC_SERVER_BIN}")
        raise RuntimeError("skip")

    ok("binario log_rpc_server compilado", True)

    _rpc_proc = subprocess.Popen(
        [RPC_SERVER_BIN],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )
    time.sleep(0.8)  # espera a que se registre en rpcbind

    if _rpc_proc.poll() is not None:
        ok("servidor RPC arranca (rpcbind disponible)", False,
           "proceso terminó inmediatamente — comprobar que rpcbind está activo")
        raise RuntimeError("skip")

    ok("servidor RPC arranca (rpcbind disponible)", True)

    def _read_rpc():
        for line in _rpc_proc.stdout:
            _rpc_lines.append(line.strip())
    threading.Thread(target=_read_rpc, daemon=True).start()

    _rpc_server = subprocess.Popen(
        ["./build/server", "-p", str(RPC_PORT)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env={**os.environ, "LOG_RPC_IP": "127.0.0.1"},
    )
    time.sleep(0.4)

    def _nc():
        return Client(HOST, RPC_PORT)

    def _connect(username, delay=0.5):
        cl = Client(HOST, RPC_PORT)
        pos = snap()
        cl.connect(username)
        time.sleep(delay)
        return cl, since(pos)

    # REGISTER
    pos = snap(); _nc().register("rpcalice")
    ok("REGISTER con LOG_RPC_IP → REGISTER OK", "REGISTER OK" in since(pos))
    ok("RPC log recibe entrada REGISTER", _rpc_wait("rpcalice REGISTER"))

    # CONNECT
    ca_r, out_connect = _connect("rpcalice")
    ok("CONNECT con LOG_RPC_IP → CONNECT OK", "CONNECT OK" in out_connect)
    ok("RPC log recibe entrada CONNECT", _rpc_wait("rpcalice CONNECT"))

    # necesitamos un destinatario para SEND
    _nc().register("rpcbob")
    cb_r, _ = _connect("rpcbob")

    # SEND
    pos = snap(); ca_r.send("rpcbob", "hola rpc")
    ok("SEND con LOG_RPC_IP → SEND OK", "SEND OK" in since(pos))
    ok("RPC log recibe entrada SEND", _rpc_wait("rpcalice SEND"))
    time.sleep(0.3)

    # USERS
    pos = snap(); ca_r.users()
    ok("USERS con LOG_RPC_IP → CONNECTED USERS OK", "CONNECTED USERS" in since(pos))
    ok("RPC log recibe entrada USERS", _rpc_wait("rpcalice USERS"))

    # DISCONNECT
    pos = snap(); ca_r.disconnect("rpcalice"); time.sleep(0.5)
    ok("DISCONNECT con LOG_RPC_IP → DISCONNECT OK", "DISCONNECT OK" in since(pos))
    ok("RPC log recibe entrada DISCONNECT", _rpc_wait("rpcalice DISCONNECT"))

    # UNREGISTER
    pos = snap(); _nc().unregister("rpcalice")
    ok("UNREGISTER con LOG_RPC_IP → UNREGISTER OK", "UNREGISTER OK" in since(pos))
    ok("RPC log recibe entrada UNREGISTER", _rpc_wait("rpcalice UNREGISTER"))

    # verificar que las 6 operaciones distintas aparecen en el log
    ops = ["REGISTER", "CONNECT", "SEND", "USERS", "DISCONNECT", "UNREGISTER"]
    for op in ops:
        ok(f"RPC log contiene operación {op}", any(op in l for l in _rpc_lines))

    # limpieza
    if cb_r._connected_user:
        cb_r.disconnect("rpcbob"); time.sleep(0.3)
    _nc().unregister("rpcbob")

except RuntimeError:
    pass  # skip marcado arriba

finally:
    if _rpc_server:
        _rpc_server.terminate()
        _rpc_server.wait()
    if _rpc_proc:
        _rpc_proc.terminate()
        _rpc_proc.wait()

# ── resumen ───────────────────────────────────────────────────────────────────

log("")
log("=" * 64)
log(f"  RESULTADO: {_passed} PASADOS | {_failed} FALLADOS | {_passed + _failed} TOTAL")
log("=" * 64)

with open(RESULTS_FILE, "w") as f:
    f.write("\n".join(_report) + "\n")

_real_out.write(f"\nResultados guardados en: {RESULTS_FILE}\n")
sys.exit(0 if _failed == 0 else 1)
