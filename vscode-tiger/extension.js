const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const cp = require('child_process');

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

function activate(context) {
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
