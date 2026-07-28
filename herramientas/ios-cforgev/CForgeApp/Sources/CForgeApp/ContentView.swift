// ContentView.swift — UI SwiftUI que usa el runtime C-Forge
import SwiftUI

struct ContentView: View {
    @StateObject private var vm = CForgeViewModel()

    var body: some View {
        NavigationView {
            VStack(spacing: 0) {
                // Output
                ScrollView {
                    Text(vm.output)
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.green)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding()
                }
                .background(Color(red: 0.05, green: 0.05, blue: 0.1))
                .frame(maxHeight: .infinity)

                Divider()

                // Editor
                TextEditor(text: $vm.code)
                    .font(.system(.body, design: .monospaced))
                    .frame(height: 140)
                    .padding(4)
                    .background(Color(red: 0.06, green: 0.15, blue: 0.25))

                // Botones
                HStack {
                    Button("Ejecutar") { vm.run() }
                        .buttonStyle(.borderedProminent)
                        .tint(.red)

                    Button("Acción") { vm.callAction() }
                        .buttonStyle(.borderedProminent)
                        .tint(Color(red: 0.06, green: 0.2, blue: 0.38))

                    Spacer()

                    Button("Limpiar") { vm.output = "" }
                        .buttonStyle(.bordered)
                }
                .padding()
                .background(Color(red: 0.08, green: 0.08, blue: 0.12))
            }
            .navigationTitle("C-Forge v\(vm.version)")
            .navigationBarTitleDisplayMode(.inline)
            .preferredColorScheme(.dark)
        }
        .onAppear { vm.start() }
    }
}

// ── ViewModel ─────────────────────────────────────────────────────────────

class CForgeViewModel: ObservableObject {
    @Published var output: String = "Cargando..."
    @Published var code: String   = "mostrar(\"Hola iOS!\")\n2 + 2"

    var version: String { CForgeRuntime.version() }

    func start() {
        DispatchQueue.global(qos: .userInitiated).async {
            // Cargar script principal del bundle
            var err: NSError?
            CForgeRuntime.runBundleScript("main", error: &err)
            DispatchQueue.main.async {
                if let e = err {
                    self.output = "Error: \(e.localizedDescription)"
                } else {
                    self.output = "C-Forge v\(CForgeRuntime.version()) listo.\nOpenSSL: \(CForgeRuntime.hasOpenSSL())"
                }
            }
        }
    }

    func run() {
        let src = code
        DispatchQueue.global(qos: .userInitiated).async {
            // Evalua ultima expresion como JSON, ejecuta el resto
            let lines = src.split(separator: "\n", omittingEmptySubsequences: false)
            var result = ""
            if lines.count == 1 {
                result = CForgeRuntime.evalJson(src)
            } else {
                let body = lines.dropLast().joined(separator: "\n")
                let last = String(lines.last ?? "")
                var err: NSError?
                CForgeRuntime.runCode(body, error: &err)
                result = CForgeRuntime.evalJson(last)
            }
            DispatchQueue.main.async { self.output = result }
        }
    }

    func callAction() {
        DispatchQueue.global(qos: .userInitiated).async {
            let result = CForgeRuntime.evalString("on_boton_presionado()")
            DispatchQueue.main.async { self.output = result }
        }
    }
}

#Preview {
    ContentView()
}
