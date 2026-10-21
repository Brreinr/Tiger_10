import sys
import json
import time
from pathlib import Path
import tiger_compiler

def run_server():
    sys.stdout.write("READY\n")
    sys.stdout.flush()

    while True:
        try:
            line = sys.stdin.readline()
            if not line:
                break
            line = line.strip()
            if not line:
                continue

            req = json.loads(line)
            file_path = req.get("file", "")

            t0 = time.perf_counter_ns()

            p = Path(file_path).resolve()
            if not p.is_file():
                resp = {"status": "error", "error": f"Archivo no encontrado: {file_path}", "time_ms": 0}
            else:
                try:
                    # Ejecutar en memoria directamente
                    res = tiger_compiler.run_file(p, stream_output=False)
                    elapsed = (time.perf_counter_ns() - t0) / 1_000_000
                    resp = {
                        "status": "ok",
                        "output": res,
                        "time_ms": round(elapsed, 3)
                    }
                except Exception as err:
                    elapsed = (time.perf_counter_ns() - t0) / 1_000_000
                    resp = {
                        "status": "error",
                        "error": str(err),
                        "output": "",
                        "time_ms": round(elapsed, 3)
                    }

            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
        except Exception as global_err:
            sys.stdout.write(json.dumps({"status": "error", "error": str(global_err)}) + "\n")
            sys.stdout.flush()

if __name__ == "__main__":
    run_server()

