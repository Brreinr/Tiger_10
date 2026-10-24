# -*- coding: utf-8 -*-
import sys, json

class _TigerBridge(dict):
    def __getattr__(self, k): return self.get(k)
    def __setattr__(self, k, v): self[k] = v
    def __getitem__(self, k): return self.get(k)
    def __setitem__(self, k, v): super().__setitem__(k, v)

_globals = {
    'TIGER': _TigerBridge({}),
    '_json': json,
    'print': print
}
_globals['TIGER'].imprimir = print
_globals['TIGER'].print = print

sys.stdout.write("__TIGER_READY__\n")
sys.stdout.flush()

while True:
    header = sys.stdin.readline()
    if not header:
        break
    if "__TIGER_REQ_START__" in header:
        lines = []
        while True:
            l = sys.stdin.readline()
            if not l or "__TIGER_REQ_END__" in l:
                break
            lines.append(l)
        script = "".join(lines)
        _globals['TIGER'].clear()
        try:
            exec(script, _globals)
        except Exception as e:
            sys.stderr.write(f"Err: {e}\n")
            sys.stderr.flush()
        sys.stdout.write("__TIGER_BATCH_DONE__\n")
        sys.stdout.flush()

