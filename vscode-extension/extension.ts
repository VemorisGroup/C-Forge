import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';

export function activate(context: vscode.ExtensionContext) {
    console.log('C-Forge extension activada');

    // ── Comando: Ejecutar archivo ──────────────────────────────────────────────
    const runFile = vscode.commands.registerCommand('cforge.runFile', () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showErrorMessage('No hay archivo activo');
            return;
        }

        const filePath = editor.document.fileName;
        if (!filePath.endsWith('.cfv')) {
            vscode.window.showErrorMessage('Solo se pueden ejecutar archivos .cfv');
            return;
        }

        // Guardar antes de ejecutar
        editor.document.save().then(() => {
            const config = vscode.workspace.getConfiguration('cforge');
            const interpreterPath = config.get<string>('interpreterPath', 'cforgev');
            const workspaceFolder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
            const cwd = workspaceFolder ? workspaceFolder.uri.fsPath : path.dirname(filePath);

            const terminal = vscode.window.createTerminal({
                name: `C-Forge: ${path.basename(filePath)}`,
                cwd
            });
            terminal.show();
            terminal.sendText(`${interpreterPath} "${filePath}"`);
        });
    });

    // ── Comando: Ejecutar selección ────────────────────────────────────────────
    const runSelection = vscode.commands.registerCommand('cforge.runSelection', () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.selection.isEmpty) {
            vscode.window.showErrorMessage('No hay texto seleccionado');
            return;
        }

        const selection = editor.document.getText(editor.selection);
        const config = vscode.workspace.getConfiguration('cforge');
        const interpreterPath = config.get<string>('interpreterPath', 'cforgev');

        // Escribir selección a archivo temporal y ejecutar
        const tmpFile = path.join(require('os').tmpdir(), `cforge_sel_${Date.now()}.cfv`);
        require('fs').writeFileSync(tmpFile, selection);

        const terminal = vscode.window.createTerminal({ name: 'C-Forge: Selección' });
        terminal.show();
        terminal.sendText(`${interpreterPath} "${tmpFile}"`);
    });

    // ── Comando: Formatear documento ───────────────────────────────────────────
    const formatDocument = vscode.commands.registerCommand('cforge.formatDocument', () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) { return; }

        const text = editor.document.getText();
        const formatted = formatCForge(text);

        const edit = new vscode.WorkspaceEdit();
        const fullRange = new vscode.Range(
            editor.document.positionAt(0),
            editor.document.positionAt(text.length)
        );
        edit.replace(editor.document.uri, fullRange, formatted);
        vscode.workspace.applyEdit(edit);
    });

    // ── Document Formatter ─────────────────────────────────────────────────────
    const formatter = vscode.languages.registerDocumentFormattingEditProvider('cforge', {
        provideDocumentFormattingEdits(document) {
            const text = document.getText();
            const formatted = formatCForge(text);
            const fullRange = new vscode.Range(
                document.positionAt(0),
                document.positionAt(text.length)
            );
            return [vscode.TextEdit.replace(fullRange, formatted)];
        }
    });

    // ── Hover Provider — documentación inline ─────────────────────────────────
    const hover = vscode.languages.registerHoverProvider('cforge', {
        provideHover(document, position) {
            const word = document.getText(document.getWordRangeAtPosition(position));
            const doc = BUILTIN_DOCS[word];
            if (doc) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(doc.signature, 'cforge');
                md.appendMarkdown('\n\n' + doc.description);
                if (doc.example) {
                    md.appendMarkdown('\n\n**Ejemplo:**');
                    md.appendCodeblock(doc.example, 'cforge');
                }
                return new vscode.Hover(md);
            }
            return null;
        }
    });

    // ── Completion Provider ────────────────────────────────────────────────────
    const completion = vscode.languages.registerCompletionItemProvider('cforge', {
        provideCompletionItems(document, position) {
            const items: vscode.CompletionItem[] = [];

            // Builtins
            for (const [name, doc] of Object.entries(BUILTIN_DOCS)) {
                const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
                item.documentation = new vscode.MarkdownString(doc.description);
                item.detail = doc.signature;
                if (doc.insertText) {
                    item.insertText = new vscode.SnippetString(doc.insertText);
                }
                items.push(item);
            }

            // Keywords
            const keywords = [
                'funcion', 'sea', 'constante', 'si', 'sino', 'mientras', 'para', 'en',
                'retornar', 'romper', 'continuar', 'segun', 'caso', 'defecto',
                'intentar', 'capturar', 'lanzar', 'finalmente', 'importar', 'exportar',
                'clase', 'extiende', 'this', 'super', 'nuevo', 'enum', 'tipo',
                'verdadero', 'falso', 'nulo', 'async', 'esperar',
                'numero', 'texto', 'booleano', 'lista', 'mapa', 'cualquiera'
            ];
            for (const kw of keywords) {
                const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
                items.push(item);
            }

            return items;
        }
    }, '.', '(');

    // ── Diagnósticos básicos ───────────────────────────────────────────────────
    const diagnosticCollection = vscode.languages.createDiagnosticCollection('cforge');

    function updateDiagnostics(document: vscode.TextDocument) {
        if (document.languageId !== 'cforge') { return; }
        const diagnostics: vscode.Diagnostic[] = [];
        const text = document.getText();
        const lines = text.split('\n');

        lines.forEach((line, i) => {
            // Detectar uso de var (debería ser 'sea')
            const varMatch = /^\s*var\s+/.exec(line);
            if (varMatch) {
                const range = new vscode.Range(i, 0, i, line.length);
                const diag = new vscode.Diagnostic(
                    range,
                    "En C-Forge usa 'sea' en lugar de 'var'",
                    vscode.DiagnosticSeverity.Warning
                );
                diagnostics.push(diag);
            }
        });

        diagnosticCollection.set(document.uri, diagnostics);
    }

    vscode.workspace.onDidChangeTextDocument(e => updateDiagnostics(e.document));
    vscode.window.onDidChangeActiveTextEditor(e => { if (e) updateDiagnostics(e.document); });

    context.subscriptions.push(
        runFile, runSelection, formatDocument,
        formatter, hover, completion,
        diagnosticCollection
    );
}

// ── Formateador básico ─────────────────────────────────────────────────────────
function formatCForge(text: string): string {
    const lines = text.split('\n');
    const result: string[] = [];
    let indent = 0;
    const TAB = '    ';

    for (let line of lines) {
        const trimmed = line.trim();

        // Reducir indent antes de línea que cierra bloque
        if (trimmed.startsWith('}') || trimmed === '}') {
            indent = Math.max(0, indent - 1);
        }

        if (trimmed === '') {
            result.push('');
        } else {
            result.push(TAB.repeat(indent) + trimmed);
        }

        // Aumentar indent después de línea que abre bloque
        if (trimmed.endsWith('{') && !trimmed.startsWith('//')) {
            indent++;
        }
    }

    return result.join('\n');
}

// ── Documentación de builtins ──────────────────────────────────────────────────
const BUILTIN_DOCS: Record<string, { signature: string; description: string; example?: string; insertText?: string }> = {
    'mostrar': {
        signature: 'mostrar(valor: cualquiera): nulo',
        description: 'Imprime un valor en la salida estándar.',
        example: 'mostrar("Hola mundo")',
        insertText: 'mostrar(${1:valor})'
    },
    'longitud': {
        signature: 'longitud(coleccion: lista|texto|mapa): numero',
        description: 'Retorna la longitud de una lista, texto o mapa.',
        example: 'longitud([1, 2, 3])  // 3',
        insertText: 'longitud(${1:coleccion})'
    },
    'rango': {
        signature: 'rango(n: numero): lista | rango(desde: numero, hasta: numero): lista',
        description: 'Genera una secuencia de números de 0 a n-1.',
        example: 'para i en rango(5) { mostrar(i) }',
        insertText: 'rango(${1:n})'
    },
    'tipo_de': {
        signature: 'tipo_de(valor: cualquiera): texto',
        description: 'Retorna el tipo del valor como texto.',
        example: 'tipo_de(42)  // "numero"\ntipo_de("hola")  // "texto"',
        insertText: 'tipo_de(${1:valor})'
    },
    'agregar': {
        signature: 'agregar(lista: lista, elemento: cualquiera): nulo',
        description: 'Agrega un elemento al final de la lista (in-place).',
        example: 'sea l = [1, 2]\nagregar(l, 3)  // l = [1, 2, 3]',
        insertText: 'agregar(${1:lista}, ${2:elemento})'
    },
    'json_parsear': {
        signature: 'json_parsear(texto: texto): mapa|lista',
        description: 'Parsea una cadena JSON y retorna el objeto/array.',
        example: 'sea datos = json_parsear(\'{"nombre": "Juan"}\')',
        insertText: 'json_parsear(${1:texto})'
    },
    'json_texto': {
        signature: 'json_texto(valor: cualquiera): texto',
        description: 'Serializa un valor a JSON compacto.',
        insertText: 'json_texto(${1:valor})'
    },
    'leer_archivo': {
        signature: 'leer_archivo(ruta: texto): texto',
        description: 'Lee el contenido completo de un archivo como texto.',
        example: 'sea contenido = leer_archivo("config.json")',
        insertText: 'leer_archivo(${1:ruta})'
    },
    'escribir_archivo': {
        signature: 'escribir_archivo(ruta: texto, contenido: texto): nulo',
        description: 'Escribe texto en un archivo (sobreescribe si existe).',
        insertText: 'escribir_archivo(${1:ruta}, ${2:contenido})'
    },
    'http_get': {
        signature: 'http_get(url: texto, headers?: mapa): mapa',
        description: 'Realiza una solicitud HTTP GET. Retorna {codigo, cuerpo, headers}.',
        example: 'sea resp = http_get("https://api.ejemplo.com/datos")',
        insertText: 'http_get(${1:url})'
    },
    'texto_dividir': {
        signature: 'texto_dividir(texto: texto, separador: texto): lista',
        description: 'Divide un texto por un separador y retorna lista de partes.',
        example: 'texto_dividir("a,b,c", ",")  // ["a", "b", "c"]',
        insertText: 'texto_dividir(${1:texto}, ${2:separador})'
    },
    'texto_unir': {
        signature: 'texto_unir(lista: lista, separador: texto): texto',
        description: 'Une los elementos de una lista con un separador.',
        example: 'texto_unir(["a", "b", "c"], ", ")  // "a, b, c"',
        insertText: 'texto_unir(${1:lista}, ${2:separador})'
    },
    'filtrar': {
        signature: 'filtrar(lista: lista, fn: funcion): lista',
        description: 'Filtra elementos de una lista que cumplen la condición.',
        example: 'filtrar([1,2,3,4], funcion(x) { x > 2 })  // [3, 4]',
        insertText: 'filtrar(${1:lista}, funcion(${2:x}) { ${3:condicion} })'
    },
    'mapear': {
        signature: 'mapear(lista: lista, fn: funcion): lista',
        description: 'Transforma cada elemento de una lista.',
        example: 'mapear([1,2,3], funcion(x) { x * 2 })  // [2, 4, 6]',
        insertText: 'mapear(${1:lista}, funcion(${2:x}) { ${3:transformacion} })'
    },
    'sha256': {
        signature: 'sha256(texto: texto): texto',
        description: 'Calcula el hash SHA-256 del texto (hex).',
        insertText: 'sha256(${1:texto})'
    },
    'regex_buscar': {
        signature: 'regex_buscar(texto: texto, patron: texto): texto|nulo',
        description: 'Busca la primera coincidencia del patrón regex.',
        insertText: 'regex_buscar(${1:texto}, ${2:patron})'
    },
    'fecha_ahora': {
        signature: 'fecha_ahora(): mapa',
        description: 'Retorna la fecha y hora actual como mapa {iso, timestamp, ...}.',
        insertText: 'fecha_ahora()'
    },
    'tiempo_ms': {
        signature: 'tiempo_ms(): numero',
        description: 'Retorna el timestamp en milisegundos.',
        insertText: 'tiempo_ms()'
    },
    'dormir': {
        signature: 'dormir(ms: numero): nulo',
        description: 'Pausa la ejecución por N milisegundos.',
        insertText: 'dormir(${1:ms})'
    }
};

export function deactivate() {}
