import argparse
import json
import os
import subprocess
import threading
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
MAX_POINTS = 120          # cantidad de muestras visibles (~2 min a 1/s)

# ---------------------------------------------------------------------------
# Estado compartido entre el hilo lector y el servidor HTTP
# ---------------------------------------------------------------------------
state_lock = threading.Lock()
points = deque(maxlen=MAX_POINTS)   # cada item: {"t":int, "value":float}
meta = {"signal": 1, "type": "senoidal", "unit": "V"}
epoch = 0                           # se incrementa en cada cambio de senal
reader_proc = None                  # subproceso del reader (C)


def reader_loop(proc):
    """Lee lineas JSON del stdout del reader y las acumula."""
    global meta
    for raw in proc.stdout:
        raw = raw.strip()
        if not raw:
            continue
        try:
            sample = json.loads(raw)
        except json.JSONDecodeError:
            continue

        with state_lock:
            meta = {
                "signal": sample.get("signal", meta["signal"]),
                "type": sample.get("type", meta["type"]),
                "unit": sample.get("unit", meta["unit"]),
            }
            t = sample.get("t")
            value = sample.get("value")
            # evitar duplicados: misma t que la ultima muestra
            if points and points[-1]["t"] == t:
                continue
            points.append({"t": t, "value": value})


def switch_signal(n):
    """Ordena al reader cambiar de senal y resetea el buffer/grafico."""
    global epoch
    if reader_proc and reader_proc.stdin:
        try:
            reader_proc.stdin.write(f"{n}\n")
            reader_proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            return False
    with state_lock:
        points.clear()      # reset de los datos -> el grafico arranca limpio
        epoch += 1
        meta["signal"] = n
        meta["type"] = "cuadrada" if n == 2 else "senoidal"
    return True


# ---------------------------------------------------------------------------
# Servidor HTTP
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # silenciar el log por request

    def _send(self, code, body, ctype="application/json"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)

        if url.path == "/" or url.path == "/index.html":
            try:
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            except FileNotFoundError:
                self._send(404, "index.html no encontrado", "text/plain")
            return

        if url.path == "/data":
            with state_lock:
                payload = {
                    "epoch": epoch,
                    "signal": meta["signal"],
                    "type": meta["type"],
                    "unit": meta["unit"],
                    "points": list(points),
                }
            self._send(200, json.dumps(payload))
            return

        if url.path == "/select":
            qs = parse_qs(url.query)
            try:
                n = int(qs.get("signal", ["1"])[0])
                n = 2 if n == 2 else 1
            except ValueError:
                n = 1
            ok = switch_signal(n)
            self._send(200 if ok else 500,
                       json.dumps({"ok": ok, "signal": n}))
            return

        self._send(404, "No encontrado", "text/plain")


def main():
    global reader_proc

    ap = argparse.ArgumentParser(description="Servidor web del TP#5")
    ap.add_argument("--reader", default=os.path.join(HERE, "..", "user", "reader"),
                    help="ruta al ejecutable reader (C)")
    ap.add_argument("--dev", default="/dev/SdeC_signals",
                    help="path del device")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--signal", type=int, default=1, choices=[1, 2],
                    help="senal inicial")
    args = ap.parse_args()

    reader_path = os.path.abspath(args.reader)
    if not os.path.exists(reader_path):
        raise SystemExit(f"No encuentro el reader en {reader_path}. "
                         f"Compilalo con 'make' en user/.")

    # lanzar el reader (C) como subproceso, en modo texto y line-buffered
    reader_proc = subprocess.Popen(
        [reader_path, str(args.signal), args.dev],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    with state_lock:
        meta["signal"] = args.signal
        meta["type"] = "cuadrada" if args.signal == 2 else "senoidal"

    t = threading.Thread(target=reader_loop, args=(reader_proc,), daemon=True)
    t.start()

    httpd = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"Servidor en http://0.0.0.0:{args.port}  (Ctrl-C para salir)")
    print(f"reader = {reader_path}   device = {args.dev}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nCerrando...")
    finally:
        httpd.shutdown()
        if reader_proc:
            reader_proc.terminate()


if __name__ == "__main__":
    main()
