"use strict";

const vscode = require("vscode");
const { execFile, spawn } = require("child_process");

let languageServer;

class CForgeLanguageServer {
  constructor(diagnostics) {
    this.diagnostics = diagnostics;
    this.sequence = 1;
    this.available = true;
    this.stopped = false;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.process = spawn("cforge", ["lsp"], { stdio: ["pipe", "pipe", "pipe"] });
    this.process.stdout.on("data", chunk => this.consume(chunk));
    this.process.stderr.on("data", chunk => console.error(`[C-Forge LSP] ${chunk}`));
    this.process.on("error", error => {
      this.available = false;
      for (const value of this.pending.values()) value.reject(error);
      this.pending.clear();
      console.error(`[C-Forge LSP] ${error.message}`);
    });
    this.process.on("exit", () => {
      for (const value of this.pending.values()) value.reject(new Error("C-Forge LSP finalizó"));
      this.pending.clear();
    });
    this.ready = this.request("initialize", {
      processId: process.pid, rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
      capabilities: {}
    }).then(() => this.notify("initialized", {})).catch(() => { this.available = false; });
  }
  send(message) {
    if (!this.available) return;
    const body = Buffer.from(JSON.stringify({ jsonrpc: "2.0", ...message }), "utf8");
    this.process.stdin.write(Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "ascii"));
    this.process.stdin.write(body);
  }
  request(method, params) {
    if (!this.available) return Promise.reject(new Error("C-Forge LSP no está disponible"));
    const id = this.sequence++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.send({ id, method, params });
    });
  }
  async initializedRequest(method, params) {
    await this.ready;
    if (!this.available || this.stopped) throw new Error("C-Forge LSP no está disponible");
    return this.request(method, params);
  }
  notify(method, params) { this.send({ method, params }); }
  consume(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const end = this.buffer.indexOf("\r\n\r\n");
      if (end < 0) return;
      const header = this.buffer.subarray(0, end).toString("ascii");
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) { this.buffer = this.buffer.subarray(end + 4); continue; }
      const length = Number(match[1]);
      if (this.buffer.length < end + 4 + length) return;
      const message = JSON.parse(this.buffer.subarray(end + 4, end + 4 + length).toString("utf8"));
      this.buffer = this.buffer.subarray(end + 4 + length);
      if (message.id !== undefined && this.pending.has(message.id)) {
        const pending = this.pending.get(message.id); this.pending.delete(message.id);
        if (message.error) pending.reject(new Error(message.error.message)); else pending.resolve(message.result);
      } else if (message.method === "textDocument/publishDiagnostics") {
        const uri = vscode.Uri.parse(message.params.uri);
        this.diagnostics.set(uri, (message.params.diagnostics || []).map(item => {
          const diagnostic = new vscode.Diagnostic(asRange(item.range), item.message,
            item.severity === 1 ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning);
          diagnostic.source = item.source || "C-Forge"; diagnostic.code = item.code; return diagnostic;
        }));
      }
    }
  }
  async open(document) {
    await this.ready;
    if (!this.available) return;
    this.notify("textDocument/didOpen", { textDocument: {
      uri: document.uri.toString(), languageId: "cforgev", version: document.version, text: document.getText()
    }});
  }
  async change(document) {
    await this.ready;
    if (!this.available) return;
    this.notify("textDocument/didChange", { textDocument: {
      uri: document.uri.toString(), version: document.version
    }, contentChanges: [{ text: document.getText() }] });
  }
  async stop() {
    if (this.stopped) return;
    this.stopped = true;
    try { await this.request("shutdown", {}); this.notify("exit", {}); } catch (_) {}
    this.available = false;
    this.process.kill();
  }
}

function asPosition(value) { return new vscode.Position(value.line, value.character); }
function asRange(value) { return new vscode.Range(asPosition(value.start), asPosition(value.end)); }
function textParams(document, position) {
  return { textDocument: { uri: document.uri.toString() }, position: { line: position.line, character: position.character } };
}
function asLocation(value) { return new vscode.Location(vscode.Uri.parse(value.uri), asRange(value.range)); }

const words = [
  "sea", "si", "sino", "mientras", "funcion", "retornar", "estructura",
  "clase", "interfaz", "implementa", "campo", "metodo", "intentar", "capturar", "gpu", "cluster",
  "test", "mostrar", "print", "verdadero", "falso", "nulo", "file_read",
  "file_write", "json_parse", "sys_fetch", "forge_hash", "forge_bench"
];

function runCForge(args, callback) {
  execFile("cforge", args, { timeout: 30000, maxBuffer: 4 * 1024 * 1024 }, callback);
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection("cforge");
  context.subscriptions.push(diagnostics);
  try {
    languageServer = new CForgeLanguageServer(diagnostics);
    context.subscriptions.push({ dispose: () => languageServer?.stop() });
    for (const document of vscode.workspace.textDocuments) {
      if (document.languageId === "cforgev") languageServer.open(document);
    }
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(document => {
      if (document.languageId === "cforgev") languageServer.open(document);
    }));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {
      if (event.document.languageId === "cforgev") languageServer.change(event.document);
    }));
  } catch (error) {
    languageServer = undefined;
    console.error(`[C-Forge] No se pudo iniciar LSP: ${error}`);
  }

  function check(document) {
    if (document.languageId !== "cforgev" || document.isUntitled) return;
    runCForge(["check", document.uri.fsPath, "--json"], (error, stdout) => {
      let values = [];
      try { values = JSON.parse(stdout || "[]"); } catch (_) { return; }
      diagnostics.set(document.uri, values.map(item => {
        const line = Math.max(0, Number(item.line || 1) - 1);
        const column = Math.max(0, Number(item.column || 1) - 1);
        const diagnostic = new vscode.Diagnostic(
          new vscode.Range(line, column, line, column + 1),
          `${item.code}: ${item.message}`,
          item.severity === "error" ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning
        );
        diagnostic.source = "C-Forge";
        diagnostic.code = item.code;
        return diagnostic;
      }));
    });
  }

  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(check));
  if (vscode.window.activeTextEditor) check(vscode.window.activeTextEditor.document);
  context.subscriptions.push(vscode.languages.registerCompletionItemProvider("cforgev", {
    async provideCompletionItems(document, position) {
      if (languageServer) {
        try { return await languageServer.initializedRequest("textDocument/completion", textParams(document, position)); }
        catch (_) {}
      }
      return words.map(word => new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword));
    }
  }));
  context.subscriptions.push(vscode.languages.registerHoverProvider("cforgev", {
    async provideHover(document, position) {
      if (!languageServer) return undefined;
      const result = await languageServer.initializedRequest("textDocument/hover", textParams(document, position));
      if (!result) return undefined;
      const value = typeof result.contents === "string" ? result.contents : result.contents.value;
      return new vscode.Hover(new vscode.MarkdownString(value));
    }
  }));
  context.subscriptions.push(vscode.languages.registerDefinitionProvider("cforgev", {
    async provideDefinition(document, position) {
      if (!languageServer) return [];
      const result = await languageServer.initializedRequest("textDocument/definition", textParams(document, position));
      return (result || []).map(asLocation);
    }
  }));
  context.subscriptions.push(vscode.languages.registerReferenceProvider("cforgev", {
    async provideReferences(document, position, contextValue) {
      if (!languageServer) return [];
      const result = await languageServer.initializedRequest("textDocument/references", {
        ...textParams(document, position), context: { includeDeclaration: contextValue.includeDeclaration }
      });
      return (result || []).map(asLocation);
    }
  }));
  context.subscriptions.push(vscode.languages.registerRenameProvider("cforgev", {
    async provideRenameEdits(document, position, newName) {
      if (!languageServer) return undefined;
      const result = await languageServer.initializedRequest("textDocument/rename", {
        ...textParams(document, position), newName
      });
      const edit = new vscode.WorkspaceEdit();
      for (const [uri, edits] of Object.entries(result?.changes || {})) {
        for (const item of edits) edit.replace(vscode.Uri.parse(uri), asRange(item.range), item.newText);
      }
      return edit;
    }
  }));
  context.subscriptions.push(vscode.languages.registerDocumentFormattingEditProvider("cforgev", {
    async provideDocumentFormattingEdits(document, options) {
      if (!languageServer) return [];
      const result = await languageServer.initializedRequest("textDocument/formatting", {
        textDocument: { uri: document.uri.toString() }, options
      });
      return (result || []).map(item => vscode.TextEdit.replace(asRange(item.range), item.newText));
    }
  }));
  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("cforge", {
    createDebugAdapterDescriptor() {
      return new vscode.DebugAdapterExecutable("cforge", ["dap"]);
    }
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.checkFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (editor) { check(editor.document); vscode.window.showInformationMessage("C-Forge: comprobación finalizada"); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.runFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    const terminal = vscode.window.createTerminal("C-Forge");
    terminal.show();
    terminal.sendText(`cforge ${JSON.stringify(editor.document.uri.fsPath)}`);
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.debugBreakpoint", async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    await vscode.debug.startDebugging(undefined, {
      type: "cforge", request: "launch", name: "Depurar C-Forge",
      program: editor.document.uri.fsPath
    });
  }));
}

async function deactivate() { if (languageServer) await languageServer.stop(); }

module.exports = { activate, deactivate };
