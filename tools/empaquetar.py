import os
import sys
import subprocess
from pathlib import Path

def build_dist():
    print("=" * 60)
    print("  [Tiger 10] Generando paquete de distribucion para usuarios")
    print("=" * 60)
    
    root_dir = Path(__file__).parent.parent.resolve()
    dist_dir = root_dir / "dist"
    dist_dir.mkdir(exist_ok=True)
    
    # Intentar generar binario independiente con PyInstaller si está disponible
    try:
        import PyInstaller.__main__
        print("\n-> Compilando 'tiger.exe' standalone (ejecutable nativo sin dependencias)...")
        PyInstaller.__main__.run([
            str(root_dir / "tiger_compiler.py"),
            "--name=tiger",
            "--onefile",
            f"--distpath={dist_dir}",
            f"--workpath={root_dir / 'build_tmp'}",
            "--clean",
            "-y"
        ])
        print("-> [Exito] 'tiger.exe' creado en carpeta 'dist/'.")
    except ImportError:
        print("\n-> [Aviso] PyInstaller no instalado. Usando lanzador ultra-rapido 'tiger.bat'.")
        print("   (Para generar tiger.exe unico, ejecuta: pip install pyinstaller && python empaquetar.py)")

    # Copiar librerías esenciales y ejemplos a dist
    import shutil
    libs_src = root_dir / "Libraries"
    libs_dst = dist_dir / "Libraries"
    if libs_src.exists():
        if libs_dst.exists():
            shutil.rmtree(libs_dst)
        shutil.copytree(libs_src, libs_dst)
        print("-> [Exito] Carpeta 'Libraries/' copiada a la distribucion.")

    shutil.copy(root_dir / "tiger.cmd", dist_dir / "tiger.cmd")
    shutil.copy(root_dir / "tiger.bat", dist_dir / "tiger.bat")
    shutil.copy(root_dir / "tiger_compiler.py", dist_dir / "tiger_compiler.py")
    if (root_dir / "ejemplo.tg").exists():
        shutil.copy(root_dir / "ejemplo.tg", dist_dir / "ejemplo.tg")

    print("\n" + "=" * 60)
    print("  DISTRIBUCION LISTA EN: " + str(dist_dir))
    print("  Cualquier persona puede:")
    print("    1. Abrir la carpeta 'dist'")
    print("    2. Ejecutar en terminal:  tiger ejemplo.tg --run")
    print("=" * 60)

if __name__ == "__main__":
    build_dist()

