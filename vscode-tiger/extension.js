const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const cp = require('child_process');
const os = require('os');

function getPaths(document, context) {
    const docPath = document ? (document.fsPath || document.fileName) : null;
    let wsFolder = null;
    if (document) {
        const ws = vscode.workspace.getWorkspaceFolder(document.uri);
        if (ws) wsFolder = ws.uri.fsPath;
    }

    const rootDir = wsFolder || "C:\\Users\\User\\OneDrive\\Documentos\\Tiger";
    const binTiger = path.join(rootDir, "bin", "tiger.exe");
    const binTigerCmd = path.join(rootDir, "bin", "tiger.cmd");
    const rootTiger = path.join(rootDir, "tiger.exe");

    // 1. Usar bin/tiger.exe (Ultra-rápido ~25-30ms)
    if (fs.existsSync(binTiger)) {
        return { docPath, executable: `& "${binTiger}"` };
    }
    if (fs.existsSync(binTigerCmd)) {
        return { docPath, executable: `& "${binTigerCmd}"` };
    }
    if (fs.existsSync(rootTiger)) {
        return { docPath, executable: `& "${rootTiger}"` };
    }
    // 2. Usar comando tiger global en PATH
    return { docPath, executable: `tiger` };
}

function runInTigerTerminal(command) {
    let terminal = vscode.window.terminals.find(t => t.name === "Tiger");
    if (!terminal) {
        terminal = vscode.window.createTerminal("Tiger");
    }
    terminal.show(true);
    terminal.sendText(command);
}

function findTigerExecutable(document, context) {
    const workspace = document && vscode.workspace.getWorkspaceFolder(document.uri);
    const root = workspace?.uri.fsPath || path.dirname(context.extensionPath);
    for (const candidate of [
        path.join(root, 'bin', 'tiger.exe'),
        path.join(root, 'bin', 'tiger_c.exe'),
        path.join(root, 'tiger.exe')
    ]) {
        if (fs.existsSync(candidate)) return { executable: candidate, cwd: root };
    }
    return { executable: process.platform === 'win32' ? 'tiger.exe' : 'tiger', cwd: root };
}

function checkDocument(document, diagnostics, context) {
    if (!document || document.languageId !== 'tiger' || document.uri.scheme !== 'file') return;
    const { executable, cwd } = findTigerExecutable(document, context);
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tiger-check-'));
    const tempFile = path.join(tempDir, 'buffer.tg');
    fs.writeFileSync(tempFile, document.getText(), 'utf8');
    const checkedVersion = document.version;

    cp.execFile(executable, [tempFile, '--check'], { cwd, timeout: 15000, maxBuffer: 1024 * 1024 }, (error, stdout = '', stderr = '') => {
        try {
            if (document.isClosed || document.version !== checkedVersion) return;
            const output = `${stdout}\n${stderr}`;
            const found = new Map();
            const pattern = /(?:Syntax Error\s+)?L(\d+)(?::C(\d+))?\]?[: ]\s*([^\r\n]*)/g;
            let match;
            while ((match = pattern.exec(output))) {
                const line = Math.max(0, Math.min(document.lineCount - 1, Number(match[1]) - 1));
                const column = Math.max(0, Math.min(document.lineAt(line).text.length, Number(match[2] || 1) - 1));
                const key = `${line}:${column}`;
                const message = match[3].replace(/^\]:\s*/, '').trim() || 'Error de sintaxis';
                if (!found.has(key)) found.set(key, new vscode.Diagnostic(
                    new vscode.Range(line, column, line, Math.min(column + 1, document.lineAt(line).text.length)),
                    message,
                    vscode.DiagnosticSeverity.Error
                ));
            }
            if (error && found.size === 0) {
                found.set('0:0', new vscode.Diagnostic(
                    new vscode.Range(0, 0, 0, 0),
                    (stderr || stdout || error.message).trim().split(/\r?\n/).filter(Boolean).pop() || 'No se pudo comprobar el archivo Tiger.',
                    vscode.DiagnosticSeverity.Error
                ));
            }
            diagnostics.set(document.uri, [...found.values()]);
        } finally {
            try { fs.rmSync(tempDir, { recursive: true, force: true }); } catch (_) {}
        }
    });
}

function activate(context) {
    const diagnostics = vscode.languages.createDiagnosticCollection('tiger');
    const timers = new Map();
    const scheduleCheck = (document, delay = 350) => {
        if (!document || document.languageId !== 'tiger' || document.uri.scheme !== 'file') return;
        const key = document.uri.toString();
        clearTimeout(timers.get(key));
        timers.set(key, setTimeout(() => {
            timers.delete(key);
            checkDocument(document, diagnostics, context);
        }, delay));
    };
    const checkNow = vscode.commands.registerCommand('tiger.check', async (uri) => {
        const editor = vscode.window.activeTextEditor;
        const document = uri ? await vscode.workspace.openTextDocument(uri) : editor?.document;
        if (document) checkDocument(document, diagnostics, context);
    });
    for (const document of vscode.workspace.textDocuments) scheduleCheck(document, 0);
    context.subscriptions.push(
        diagnostics,
        checkNow,
        vscode.workspace.onDidOpenTextDocument(document => scheduleCheck(document, 0)),
        vscode.workspace.onDidChangeTextDocument(event => scheduleCheck(event.document)),
        vscode.workspace.onDidSaveTextDocument(document => scheduleCheck(document, 0)),
        vscode.workspace.onDidCloseTextDocument(document => {
            clearTimeout(timers.get(document.uri.toString()));
            timers.delete(document.uri.toString());
            diagnostics.delete(document.uri);
        })
    );

    // 1. Ejecutar archivo directamente en terminal Tiger interactiva
    const runFileDisposable = vscode.commands.registerCommand('tiger.run', async (uri) => {
        const editor = vscode.window.activeTextEditor;
        const document = uri ? await vscode.workspace.openTextDocument(uri) : editor?.document;
        if (!document) {
            vscode.window.showWarningMessage('No hay ningún archivo Tiger abierto para ejecutar.');
            return;
        }
        await document.save();
        const docPath = document.fsPath || document.fileName;
        const { executable } = getPaths(document, context);
        runInTigerTerminal(`${executable} "${docPath}" --run --fix`);
    });

    // 2. Ejecutar con binario nativo si LLVM está disponible
    const runNativeDisposable = vscode.commands.registerCommand('tiger.runNative', async (uri) => {
        const editor = vscode.window.activeTextEditor;
        const document = uri ? await vscode.workspace.openTextDocument(uri) : editor?.document;
        if (!document) return;
        await document.save();
        const docPath = document.fsPath || document.fileName;
        const { executable } = getPaths(document, context);
        runInTigerTerminal(`${executable} "${docPath}" --run-native --fix`);
    });

    // 3. Compilar a Ensamblador nativo (.asm)
    const compileAsmDisposable = vscode.commands.registerCommand('tiger.compileAsm', async (uri) => {
        const editor = vscode.window.activeTextEditor;
        const document = uri ? await vscode.workspace.openTextDocument(uri) : editor?.document;
        if (!document) return;
        await document.save();
        const docPath = document.fsPath || document.fileName;
        const { executable } = getPaths(document, context);
        runInTigerTerminal(`${executable} "${docPath}" --target asm --fix`);
    });

    // 4. Compilar a WebAssembly (.wat)
    const compileWatDisposable = vscode.commands.registerCommand('tiger.compileWat', async (uri) => {
        const editor = vscode.window.activeTextEditor;
        const document = uri ? await vscode.workspace.openTextDocument(uri) : editor?.document;
        if (!document) return;
        await document.save();
        const docPath = document.fsPath || document.fileName;
        const { executable } = getPaths(document, context);
        runInTigerTerminal(`${executable} "${docPath}" --target wat --fix`);
    });

    context.subscriptions.push(
        runFileDisposable,
        runNativeDisposable,
        compileAsmDisposable,
        compileWatDisposable
    );
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
