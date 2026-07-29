// ContentView.swift — Vista principal de la app iOS demo con C-Forge
// Muestra un editor de código C-Forge con ejecución en tiempo real

import SwiftUI

@main
struct CForgeIOSApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    @StateObject private var vm = CForgeViewModel()

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // ── Editor de código ─────────────────────────────────────────
                VStack(alignment: .leading, spacing: 4) {
                    Label("Código C-Forge", systemImage: "chevron.left.forwardslash.chevron.right")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.horizontal)
                        .padding(.top, 8)

                    TextEditor(text: $vm.code)
                        .font(.system(.body, design: .monospaced))
                        .frame(minHeight: 200)
                        .padding(8)
                        .background(Color(.systemGray6))
                        .cornerRadius(8)
                        .padding(.horizontal)
                }

                // ── Botones ──────────────────────────────────────────────────
                HStack(spacing: 12) {
                    Button(action: vm.run) {
                        Label("Ejecutar", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.green)

                    Button(action: vm.clear) {
                        Label("Limpiar", systemImage: "trash")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .tint(.red)

                    Button(action: vm.loadExample) {
                        Label("Ejemplo", systemImage: "doc.text")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                }
                .padding()

                // ── Salida ───────────────────────────────────────────────────
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Label("Salida", systemImage: "terminal")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Spacer()
                        if vm.isRunning {
                            ProgressView().scaleEffect(0.7)
                        }
                        Text("C-Forge v\(CForgeInterpreter.version)")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }
                    .padding(.horizontal)

                    ScrollView {
                        Text(vm.output.isEmpty ? "(sin salida)" : vm.output)
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(vm.hasError ? .red : .primary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(8)
                    }
                    .frame(minHeight: 120)
                    .background(Color.black.opacity(0.05))
                    .cornerRadius(8)
                    .padding(.horizontal)
                    .padding(.bottom)
                }
            }
            .navigationTitle("C-Forge Studio")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Menu {
                        Button("Hola mundo") { vm.loadHola() }
                        Button("Fibonacci") { vm.loadFibonacci() }
                        Button("Lista y mapa") { vm.loadColecciones() }
                        Button("Clases") { vm.loadClases() }
                        Button("JSON demo") { vm.loadJSON() }
                    } label: {
                        Image(systemName: "doc.on.doc")
                    }
                }
            }
        }
    }
}

// ── ViewModel ─────────────────────────────────────────────────────────────────
@MainActor
final class CForgeViewModel: ObservableObject {
    @Published var code: String = ""
    @Published var output: String = ""
    @Published var isRunning = false
    @Published var hasError = false

    private var interp: CForgeInterpreter?

    init() {
        do {
            interp = try CForgeInterpreter()
            // Registrar función nativa: log_ios → NSLog
            interp?.registerNative("log_ios") { args in
                NSLog("[C-Forge] %@", args)
                return "\"ok\""
            }
        } catch {
            output = "Error inicializando intérprete: \(error.localizedDescription)"
        }
        loadHola()
    }

    func run() {
        guard let interp = interp else { return }
        isRunning = true
        hasError = false
        output = ""

        Task {
            do {
                let result = try interp.run(code)
                output = result.isEmpty ? "(sin salida)" : result
                hasError = false
            } catch {
                output = error.localizedDescription
                hasError = true
            }
            isRunning = false
        }
    }

    func clear() {
        output = ""
        hasError = false
    }

    func loadExample() {
        loadHola()
    }

    func loadHola() {
        code = """
// Hola mundo en C-Forge
mostrar("¡Hola desde iOS!")

sea nombre = "C-Forge"
sea version = 2.3
mostrar("Lenguaje: {nombre} v{version}")

// Lista y bucle
sea frutas = ["manzana", "naranja", "kiwi"]
para f en frutas {
    mostrar("  • " + f)
}
"""
    }

    func loadFibonacci() {
        code = """
// Fibonacci recursivo con memoización
sea memo = {}

funcion fib(n: numero): numero {
    si (n <= 1) { retornar n }
    sea k = n + ""
    si (tiene_clave(memo, k)) { retornar memo[k] }
    sea r = fib(n - 1) + fib(n - 2)
    memo[k] = r
    retornar r
}

para i en rango(15) {
    mostrar("fib(" + i + ") = " + fib(i))
}
"""
    }

    func loadColecciones() {
        code = """
// Colecciones en C-Forge
sea nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

// Filtrar pares
sea pares = filtrar(nums, funcion(x) { x % 2 == 0 })
mostrar("Pares: " + pares)

// Cuadrados
sea cuadrados = mapear(nums, funcion(x) { x * x })
mostrar("Cuadrados: " + cuadrados)

// Suma total
sea suma = reducir(nums, 0, funcion(acc, x) { acc + x })
mostrar("Suma: " + suma)

// Mapa
sea persona = {"nombre": "Ana", "edad": 28, "ciudad": "Lima"}
mostrar("Nombre: " + persona["nombre"])
mostrar("Edad:   " + persona["edad"])
"""
    }

    func loadClases() {
        code = """
// Clases en C-Forge
clase Punto {
    constructor(x: numero, y: numero) {
        this.x = x
        this.y = y
    }

    funcion distancia(otro: Punto): numero {
        sea dx = this.x - otro.x
        sea dy = this.y - otro.y
        retornar raiz(dx * dx + dy * dy)
    }

    funcion a_texto(): texto {
        retornar "({this.x}, {this.y})"
    }
}

sea p1 = nuevo Punto(0, 0)
sea p2 = nuevo Punto(3, 4)
mostrar("P1: " + p1.a_texto())
mostrar("P2: " + p2.a_texto())
mostrar("Distancia: " + p1.distancia(p2))
"""
    }

    func loadJSON() {
        code = """
// JSON en C-Forge
sea datos = {
    "nombre": "C-Forge",
    "version": 2.3,
    "features": ["closures", "clases", "JSON", "HTTP"],
    "activo": verdadero
}

sea json_str = json_texto(datos)
mostrar("JSON: " + json_str)

sea parsed = json_parsear(json_str)
mostrar("Nombre: " + parsed["nombre"])
mostrar("Features: " + longitud(parsed["features"]))
para f en parsed["features"] {
    mostrar("  ✓ " + f)
}
"""
    }
}

// ── Preview ────────────────────────────────────────────────────────────────────
#Preview {
    ContentView()
}
