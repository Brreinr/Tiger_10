import shutil
import py_compile
import subprocess
from pathlib import Path

def setup_extension_bin():
    print("=" * 60)
    print("  [Tiger 10] Protegiendo y empaquetando binarios en vscode-tiger")
    print("=" * 60)

    root = Path(__file__).parent.parent.resolve()
    ext_dir = root / "vscode-tiger"
    bin_dir = ext_dir / "bin"
    bin_dir.mkdir(exist_ok=True)

    # 1. Copiar tiger.exe independiente a vscode-tiger/bin
    dist_exe = root / "dist" / "tiger.exe"
    if dist_exe.exists():
        shutil.copy2(dist_exe, bin_dir / "tiger.exe")
        print("-> [Exito] 'tiger.exe' copiado a vscode-tiger/bin/")
    else:
        print("-> [Aviso] dist/tiger.exe no encontrado directamente.")

    # 2. Copiar tiger.cmd a vscode-tiger/bin
    if (root / "tiger.cmd").exists():
        shutil.copy2(root / "tiger.cmd", bin_dir / "tiger.cmd")

    # 3. Compilar tiger_compiler.py a bytecode compilado (.pyc oculto y binario)
    compiler_py = root / "tiger_compiler.py"
    if compiler_py.exists():
        # Generar bytecode .pyc protegido en .tiger_core.pyc (nombre oculto)
        pyc_path = bin_dir / ".tiger_core.pyc"
        py_compile.compile(str(compiler_py), cfile=str(pyc_path), doraise=True)
        print("-> [Exito] tiger_compiler.py compilado a bytecode binario protegido (.tiger_core.pyc).")

    # 4. Copiar Libraries oficiales a vscode-tiger/bin/Libraries
    libs_src = root / "Libraries"
    libs_dst = bin_dir / "Libraries"
    if libs_src.exists():
        if libs_dst.exists():
            shutil.rmtree(libs_dst)
        shutil.copytree(libs_src, libs_dst)
        print("-> [Exito] 'Libraries/' copiado a vscode-tiger/bin/Libraries")

    # 5. Ocultar la carpeta bin en Windows mediante atributos de sistema si es posible
    try:
        subprocess.run(["attrib", "+h", str(bin_dir / ".tiger_core.pyc")], capture_output=True)
    except Exception:
        pass

    # 6. Sincronizar también con la carpeta de extensiones instalada de VS Code
    installed_ext = Path(r"C:\Users\User\.vscode\extensions\tiger-lang.tiger-language-0.3.0")
    if installed_ext.exists():
        installed_bin = installed_ext / "bin"
        if installed_bin.exists():
            shutil.rmtree(installed_bin)
        shutil.copytree(bin_dir, installed_bin)
        print("-> [Exito] Binarios protegidos sincronizados con la extension instalada de VS Code.")

    print("\n[Completado]: vscode-tiger ahora es completamente autosuficiente y protegido.")

if __name__ == "__main__":
    setup_extension_bin()

