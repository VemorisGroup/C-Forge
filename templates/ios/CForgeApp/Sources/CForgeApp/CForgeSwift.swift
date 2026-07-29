// CForgeSwift.swift — API Swift de alto nivel para el intérprete C-Forge
// Envuelve la API C (cforge_ios.h) en tipos Swift idiomáticos

import Foundation

// ── Errors ────────────────────────────────────────────────────────────────────
public enum CForgeError: Error, LocalizedError {
    case contextCreationFailed
    case executionFailed(String)
    case fileNotFound(String)
    case syntaxError(String)
    case typeMismatch(expected: String, got: String)

    public var errorDescription: String? {
        switch self {
        case .contextCreationFailed:
            return "No se pudo crear el contexto C-Forge"
        case .executionFailed(let msg):
            return "Error de ejecución: \(msg)"
        case .fileNotFound(let path):
            return "Archivo no encontrado: \(path)"
        case .syntaxError(let msg):
            return "Error de sintaxis: \(msg)"
        case .typeMismatch(let expected, let got):
            return "Tipo incorrecto: esperaba \(expected), recibió \(got)"
        }
    }
}

// ── Valor C-Forge ─────────────────────────────────────────────────────────────
public enum CForgeValue {
    case null
    case number(Double)
    case string(String)
    case bool(Bool)
    case list([CForgeValue])

    /// Convierte a Swift Double
    public var asDouble: Double? {
        if case .number(let n) = self { return n }
        return nil
    }
    /// Convierte a Swift Int
    public var asInt: Int? {
        if case .number(let n) = self { return Int(n) }
        return nil
    }
    /// Convierte a Swift String
    public var asString: String? {
        if case .string(let s) = self { return s }
        if case .number(let n) = self { return String(n) }
        return nil
    }
    /// Convierte a Swift Bool
    public var asBool: Bool? {
        if case .bool(let b) = self { return b }
        return nil
    }
    /// Convierte a lista Swift
    public var asList: [CForgeValue]? {
        if case .list(let l) = self { return l }
        return nil
    }
}

// ── Intérprete C-Forge ────────────────────────────────────────────────────────
@MainActor
public final class CForgeInterpreter: ObservableObject {

    private var ctx: OpaquePointer?

    /// Salida acumulada del intérprete
    @Published public private(set) var output: String = ""

    /// Último error producido
    @Published public private(set) var lastError: String? = nil

    /// Versión del intérprete
    public static var version: String {
        return String(cString: cforge_version())
    }

    public init() throws {
        ctx = OpaquePointer(cforge_context_create())
        guard ctx != nil else {
            throw CForgeError.contextCreationFailed
        }
    }

    deinit {
        if let ctx = ctx {
            cforge_context_destroy(UnsafeMutablePointer(ctx))
        }
    }

    // ── Ejecución ──────────────────────────────────────────────────────────────

    /// Ejecuta código C-Forge y retorna la salida.
    @discardableResult
    public func run(_ code: String) throws -> String {
        guard let ctx = ctx else { throw CForgeError.contextCreationFailed }

        // Validar sintaxis primero
        let valid = code.withCString { cforge_validate_syntax($0) }
        if !valid {
            throw CForgeError.syntaxError("Llaves/paréntesis no balanceados")
        }

        let success = code.withCString { cforge_run_string(UnsafeMutablePointer(ctx), $0) }

        let outStr = String(cString: cforge_last_output(UnsafeMutablePointer(ctx)))
        cforge_clear_output(UnsafeMutablePointer(ctx))

        if !success {
            let err = String(cString: cforge_last_error(UnsafeMutablePointer(ctx)))
            lastError = err
            throw CForgeError.executionFailed(err)
        }

        lastError = nil
        output += outStr
        return outStr
    }

    /// Ejecuta un archivo .cfv en el bundle o en Documents.
    @discardableResult
    public func runFile(named name: String, bundle: Bundle = .main) throws -> String {
        guard let ctx = ctx else { throw CForgeError.contextCreationFailed }

        // Buscar en bundle
        var filePath: String
        if let bundlePath = bundle.path(forResource: name, ofType: "cfv") {
            filePath = bundlePath
        } else {
            // Buscar en Documents
            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            let url = docs.appendingPathComponent(name + ".cfv")
            guard FileManager.default.fileExists(atPath: url.path) else {
                throw CForgeError.fileNotFound(name + ".cfv")
            }
            filePath = url.path
        }

        let success = filePath.withCString { cforge_run_file(UnsafeMutablePointer(ctx), $0) }
        let outStr = String(cString: cforge_last_output(UnsafeMutablePointer(ctx)))
        cforge_clear_output(UnsafeMutablePointer(ctx))

        if !success {
            let err = String(cString: cforge_last_error(UnsafeMutablePointer(ctx)))
            lastError = err
            throw CForgeError.executionFailed(err)
        }

        output += outStr
        return outStr
    }

    // ── Variables globales ─────────────────────────────────────────────────────

    public func setGlobal(_ name: String, value: Double) {
        guard let ctx = ctx else { return }
        name.withCString { cforge_set_number(UnsafeMutablePointer(ctx), $0, value) }
    }

    public func setGlobal(_ name: String, value: String) {
        guard let ctx = ctx else { return }
        name.withCString { n in
            value.withCString { v in
                cforge_set_string(UnsafeMutablePointer(ctx), n, v)
            }
        }
    }

    public func setGlobal(_ name: String, value: Bool) {
        guard let ctx = ctx else { return }
        name.withCString { cforge_set_bool(UnsafeMutablePointer(ctx), $0, value) }
    }

    public func getGlobal(_ name: String) -> CForgeValue? {
        guard let ctx = ctx else { return nil }
        guard let raw = name.withCString({ cforge_get_global(UnsafeMutablePointer(ctx), $0) }) else {
            return nil
        }
        return convertValue(raw)
    }

    private func convertValue(_ raw: OpaquePointer?) -> CForgeValue {
        guard let raw = raw else { return .null }
        let ptr = UnsafeMutablePointer(raw)
        switch cforge_value_type(ptr) {
        case CFORGE_NUMBER:
            return .number(cforge_value_number(ptr))
        case CFORGE_STRING:
            return .string(String(cString: cforge_value_string(ptr)))
        case CFORGE_BOOL:
            return .bool(cforge_value_bool(ptr))
        case CFORGE_LIST:
            let count = Int(cforge_value_list_length(ptr))
            let items = (0..<count).compactMap { i -> CForgeValue? in
                let elem = cforge_value_list_get(ptr, Int32(i))
                return convertValue(elem.map { OpaquePointer($0) })
            }
            return .list(items)
        default:
            return .null
        }
    }

    // ── Funciones nativas (Swift → C-Forge) ───────────────────────────────────

    /// Registra una función Swift que puede ser llamada desde C-Forge.
    /// La función recibe los argumentos como JSON string y retorna JSON string.
    public func registerNative(_ name: String, handler: @escaping (String) -> String) {
        guard let ctx = ctx else { return }
        // En modo producción, usar un wrapper estático para el callback C
        // Por limitaciones del closure en C, usar tabla de dispatch global
        CForgeNativeRegistry.shared.register(name: name, handler: handler)
        name.withCString { n in
            cforge_register_native(UnsafeMutablePointer(ctx), n) { jsonArgs in
                guard let args = jsonArgs else { return nil }
                let argsStr = String(cString: args)
                let result = CForgeNativeRegistry.shared.call(name: String(cString: n!), args: argsStr)
                return (result as NSString).utf8String
            }
        }
    }

    /// Limpia la salida acumulada.
    public func clearOutput() {
        output = ""
    }

    /// Valida sintaxis sin ejecutar.
    public func validateSyntax(_ code: String) -> Bool {
        return code.withCString { cforge_validate_syntax($0) }
    }
}

// ── Registry de funciones nativas ─────────────────────────────────────────────
final class CForgeNativeRegistry {
    static let shared = CForgeNativeRegistry()
    private var handlers: [String: (String) -> String] = [:]

    func register(name: String, handler: @escaping (String) -> String) {
        handlers[name] = handler
    }

    func call(name: String, args: String) -> String {
        return handlers[name]?(args) ?? "null"
    }
}
