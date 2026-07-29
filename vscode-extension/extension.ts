/**
 * C-Forge VSCode Extension v3.1.0
 *
 * Features:
 *   - Full LSP client (autocompletado, hover, ir a definición, referencias,
 *     renombrar, acciones de código, tokens semánticos, formato, firma)
 *   - Ejecutar archivo / selección en terminal integrado
 *   - Formato automático al guardar
 *   - Diagnósticos en tiempo real
 *   - Snippets + resaltado de sintaxis (TextMate grammar)
 */

import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';

import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    Executable,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

// ── Activate ──────────────────────────────────────────────────────────────────
export function activate(context: vscode.ExtensionContext) {
    console.log('C-Forge extension v3.1.0 activada');

    const config = vscode.workspace.getConfiguration('cforge');

    // ── Start LSP ────────────────────────────────────────────────────────────
    if (config.get<boolean>('lspEnabled', true)) {
        startLSP(context);
    }

    // ── Commands ─────────────────────────────────────────────────────────────
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.runFile', () => runFile()),
        vscode.commands.registerCommand('cforge.runSelection', () => runSelection()),
        vscode.commands.registerCommand('cforge.formatDocument', () => formatDocument()),
        vscode.commands.registerCommand('cforge.restartLsp', () => restartLSP(context)),
        vscode.commands.registerCommand('cforge.openPlayground', () => openPlayground(context)),
    );

    // ── Format on save ───────────────────────────────────────────────────────
    context.subscriptions.push(
        vscode.workspace.onWillSaveTextDocument(e => {
            if (e.document.languageId !== 'cforge') return;
            const cfg = vscode.workspace.getConfiguration('cforge');
            if (!cfg.get<boolean>('formatOnSave', true)) return;
            // Delegate to LSP formatter if available, else use local formatter
            if (client && client.isRunning()) return; // LSP handles it
            const edit = formatDocumentLocally(e.document);
            if (edit) e.waitUntil(Promise.resolve([edit]));
        })
    );

    // ── Status bar ────────────────────────────────────────────────────────────
    const statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    statusBar.text = '$(play) C-Forge';
    statusBar.tooltip = 'Ejecutar archivo C-Forge';
    statusBar.command = 'cforge.runFile';
    statusBar.show();
    context.subscriptions.push(statusBar);

    vscode.window.onDidChangeActiveTextEditor(ed => {
        if (ed && ed.document.languageId === 'cforge') {
            statusBar.show();
        } else {
            statusBar.hide();
        }
    }, null, context.subscriptions);
}

// ── LSP client setup ──────────────────────────────────────────────────────────
function startLSP(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('cforge');
    let lspPath = config.get<string>('lspServerPath', '').trim();

    // Find lsp_server.py: user config → bundled in extension → tools/ sibling
    if (!lspPath || !fs.existsSync(lspPath)) {
        const bundled = path.join(context.extensionPath, 'lsp_server.py');
        if (fs.existsSync(bundled)) {
            lspPath = bundled;
        } else {
            // Try sibling tools/ directory (development layout)
            const sibling = path.join(context.extensionPath, '..', 'tools', 'lsp_server.py');
            if (fs.existsSync(sibling)) {
                lspPath = sibling;
            }
        }
    }

    if (!lspPath) {
        vscode.window.showWarningMessage(
            'C-Forge LSP: No se encontró lsp_server.py. Configura cforge.lspServerPath.'
        );
        return;
    }

    const stdlibPath = config.get<string>('stdlibPath', '');
    const env: NodeJS.ProcessEnv = { ...process.env };
    if (stdlibPath) env['CFORGE_STDLIB'] = stdlibPath;

    const serverExecutable: Executable = {
        command: 'python3',
        args: [lspPath],
        options: { env }
    };

    const serverOptions: ServerOptions = {
        run: serverExecutable,
        debug: serverExecutable,
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'cforge' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cfv'),
        },
        outputChannelName: 'C-Forge LSP',
        traceOutputChannel: vscode.window.createOutputChannel('C-Forge LSP Trace'),
        middleware: {
            // Enrich completion items with C-Forge specific info
            provideCompletionItem: async (document, position, context, token, next) => {
                const result = await next(document, position, context, token);
                return result;
            }
        }
    };

    client = new LanguageClient(
        'cforge',
        'C-Forge Language Server',
        serverOptions,
        clientOptions
    );

    client.start().then(() => {
        console.log('C-Forge LSP iniciado');
    }).catch(err => {
        vscode.window.showErrorMessage(`C-Forge LSP falló al iniciar: ${err.message}`);
    });

    context.subscriptions.push({ dispose: () => client?.stop() });
}

async function restartLSP(context: vscode.ExtensionContext) {
    if (client) {
        await client.stop();
        client = undefined;
    }
    startLSP(context);
    vscode.window.showInformationMessage('C-Forge LSP reiniciado');
}

// ── Run commands ──────────────────────────────────────────────────────────────
function runFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showErrorMessage('No hay archivo activo');
        return;
    }
    if (!editor.document.fileName.endsWith('.cfv')) {
        vscode.window.showErrorMessage('Solo se pueden ejecutar archivos .cfv');
        return;
    }
    editor.document.save().then(() => {
        const config = vscode.workspace.getConfiguration('cforge');
        const interp = config.get<string>('interpreterPath', 'cforgev');
        const stdlibPath = config.get<string>('stdlibPath', '');
        const filePath = editor.document.fileName;
        const wf = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const cwd = wf ? wf.uri.fsPath : path.dirname(filePath);

        const terminal = vscode.window.createTerminal({
            name: `C-Forge: ${path.basename(filePath)}`,
            cwd,
            env: stdlibPath ? { CFORGE_STDLIB: stdlibPath } : undefined,
        });
        terminal.show();
        terminal.sendText(`${interp} "${filePath}"`);
    });
}

function runSelection() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.selection.isEmpty) {
        vscode.window.showErrorMessage('No hay texto seleccionado');
        return;
    }
    const code = editor.document.getText(editor.selection);
    const config = vscode.workspace.getConfiguration('cforge');
    const interp = config.get<string>('interpreterPath', 'cforgev');
    const stdlibPath = config.get<string>('stdlibPath', '');

    const tmpFile = path.join(os.tmpdir(), `cforge_sel_${Date.now()}.cfv`);
    fs.writeFileSync(tmpFile, code, 'utf8');

    const terminal = vscode.window.createTerminal({
        name: 'C-Forge: Selección',
        env: stdlibPath ? { CFORGE_STDLIB: stdlibPath } : undefined,
    });
    terminal.show();
    terminal.sendText(`${interp} "${tmpFile}"`);
}

function formatDocument() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'cforge') return;

    // Prefer LSP formatter
    if (client && client.isRunning()) {
        vscode.commands.executeCommand('editor.action.formatDocument');
        return;
    }

    const text = editor.document.getText();
    const formatted = formatCForge(text);
    if (formatted === text) return;

    const edit = new vscode.WorkspaceEdit();
    const fullRange = new vscode.Range(
        editor.document.positionAt(0),
        editor.document.positionAt(text.length)
    );
    edit.replace(editor.document.uri, fullRange, formatted);
    vscode.workspace.applyEdit(edit);
}

function formatDocumentLocally(document: vscode.TextDocument): vscode.TextEdit | null {
    const text = document.getText();
    const formatted = formatCForge(text);
    if (formatted === text) return null;
    return vscode.TextEdit.replace(
        new vscode.Range(document.positionAt(0), document.positionAt(text.length)),
        formatted
    );
}

// ── Playground ────────────────────────────────────────────────────────────────
function openPlayground(context: vscode.ExtensionContext) {
    const panel = vscode.window.createWebviewPanel(
        'cforgePlayground',
        'C-Forge Playground',
        vscode.ViewColumn.Beside,
        { enableScripts: true }
    );

    panel.webview.html = getPlaygroundHTML();

    panel.webview.onDidReceiveMessage(async msg => {
        if (msg.type === 'run') {
            const code = msg.code as string;
            const tmpFile = path.join(os.tmpdir(), `cfplayground_${Date.now()}.cfv`);
            fs.writeFileSync(tmpFile, code, 'utf8');

            const config = vscode.workspace.getConfiguration('cforge');
            const interp = config.get<string>('interpreterPath', 'cforgev');
            const stdlibPath = config.get<string>('stdlibPath', '');
            const env = stdlibPath ? { ...process.env, CFORGE_STDLIB: stdlibPath } : process.env;

            cp.exec(`${interp} "${tmpFile}"`, { env, timeout: 10000 }, (err, stdout, stderr) => {
                try { fs.unlinkSync(tmpFile); } catch {}
                panel.webview.postMessage({
                    type: 'result',
                    stdout: stdout || '',
                    stderr: err ? (stderr || err.message) : '',
                    exitCode: err ? (err as any).code || 1 : 0,
                });
            });
        }
    }, undefined, context.subscriptions);
}

function getPlaygroundHTML(): string {
    return `<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<title>C-Forge Playground</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'Segoe UI', sans-serif; background: #1e1e2e; color: #cdd6f4; height: 100vh; display: flex; flex-direction: column; }
  header { background: #181825; padding: 10px 16px; display: flex; align-items: center; gap: 12px; border-bottom: 1px solid #313244; }
  header h1 { font-size: 16px; color: #cba6f7; }
  button { background: #cba6f7; color: #1e1e2e; border: none; padding: 6px 14px; border-radius: 4px; cursor: pointer; font-weight: 600; font-size: 13px; }
  button:hover { background: #d0bcff; }
  #clear-btn { background: #45475a; color: #cdd6f4; }
  main { display: flex; flex: 1; overflow: hidden; }
  #editor-pane { flex: 1; display: flex; flex-direction: column; }
  textarea { flex: 1; background: #1e1e2e; color: #cdd6f4; border: none; padding: 12px; font-family: 'Cascadia Code', 'Fira Code', monospace; font-size: 14px; resize: none; outline: none; border-right: 1px solid #313244; tab-size: 4; }
  #output-pane { width: 40%; display: flex; flex-direction: column; }
  #output-header { background: #181825; padding: 8px 12px; font-size: 12px; color: #6c7086; border-bottom: 1px solid #313244; }
  #output { flex: 1; padding: 12px; font-family: monospace; font-size: 13px; white-space: pre-wrap; overflow-y: auto; }
  .stdout { color: #a6e3a1; }
  .stderr { color: #f38ba8; }
  .info { color: #89b4fa; }
  #status { font-size: 12px; color: #6c7086; }
</style>
</head>
<body>
<header>
  <h1>⚡ C-Forge Playground</h1>
  <button onclick="runCode()">▶ Ejecutar</button>
  <button id="clear-btn" onclick="clearOutput()">✕ Limpiar</button>
  <span id="status"></span>
</header>
<main>
  <div id="editor-pane">
    <textarea id="editor" placeholder="// Escribe tu código C-Forge aquí..." spellcheck="false">// ¡Bienvenido a C-Forge Playground!
funcion saludar(nombre: texto): texto {
    retornar "¡Hola, " + nombre + "!"
}

sea mensaje = saludar("Mundo")
mostrar(mensaje)

// Pattern matching
sea x = 42
match (x) {
    caso 0 { mostrar("cero") }
    caso n si (n > 0) { mostrar("positivo: " + a_texto(n)) }
    caso _ { mostrar("negativo") }
}
</textarea>
  </div>
  <div id="output-pane">
    <div id="output-header">Salida</div>
    <div id="output"><span class="info">Presiona ▶ Ejecutar para correr tu código.</span></div>
  </div>
</main>
<script>
  const vscode = acquireVsCodeApi();
  const editor = document.getElementById('editor');
  const output = document.getElementById('output');
  const status = document.getElementById('status');

  // Tab key support
  editor.addEventListener('keydown', e => {
    if (e.key === 'Tab') {
      e.preventDefault();
      const start = editor.selectionStart;
      const end = editor.selectionEnd;
      editor.value = editor.value.substring(0, start) + '    ' + editor.value.substring(end);
      editor.selectionStart = editor.selectionEnd = start + 4;
    }
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      runCode();
    }
  });

  function runCode() {
    status.textContent = 'Ejecutando...';
    output.innerHTML = '<span class="info">Ejecutando...</span>';
    vscode.postMessage({ type: 'run', code: editor.value });
  }

  function clearOutput() {
    output.innerHTML = '<span class="info">Listo.</span>';
    status.textContent = '';
  }

  window.addEventListener('message', e => {
    const msg = e.data;
    if (msg.type === 'result') {
      const lines = [];
      if (msg.stdout) lines.push('<span class="stdout">' + escHtml(msg.stdout) + '</span>');
      if (msg.stderr) lines.push('<span class="stderr">Error: ' + escHtml(msg.stderr) + '</span>');
      if (!msg.stdout && !msg.stderr) lines.push('<span class="info">(sin salida)</span>');
      output.innerHTML = lines.join('');
      status.textContent = msg.exitCode === 0 ? '✓ OK' : '✗ Error';
      status.style.color = msg.exitCode === 0 ? '#a6e3a1' : '#f38ba8';
    }
  });

  function escHtml(t) {
    return t.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  }
</script>
</body>
</html>`;
}

// ── Local formatter (fallback when LSP not running) ───────────────────────────
function formatCForge(text: string): string {
    const lines = text.split('\n');
    const result: string[] = [];
    let indent = 0;
    const TAB = '    ';
    let prevBlank = false;

    for (const line of lines) {
        const trimmed = line.trim();

        if (trimmed.startsWith('}')) indent = Math.max(0, indent - 1);

        if (trimmed === '') {
            if (!prevBlank) result.push('');
            prevBlank = true;
            continue;
        }
        prevBlank = false;

        result.push(TAB.repeat(indent) + trimmed);

        if (trimmed.endsWith('{') && !trimmed.startsWith('//')) indent++;
    }

    while (result.length > 0 && result[result.length - 1] === '') result.pop();
    return result.join('\n') + '\n';
}

// ── Deactivate ────────────────────────────────────────────────────────────────
export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
