import shutil
from pathlib import Path

def sync_extension():
    source_dir = Path(r"c:\Users\User\OneDrive\Documentos\Tiger\vscode-tiger")
    target_dir = Path(r"C:\Users\User\.vscode\extensions\tiger-lang.tiger-language-0.3.0")
    
    if not target_dir.exists():
        print(f"No se encontró el directorio instalado: {target_dir}")
        return
        
    for item in ["syntaxes", "themes", "language-configuration.json", "package.json", "extension.js"]:
        src = source_dir / item
        dst = target_dir / item
        if src.is_dir():
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
        elif src.is_file():
            shutil.copy2(src, dst)
            
    print("Sincronización completada exitosamente en .vscode/extensions.")

if __name__ == "__main__":
    sync_extension()

