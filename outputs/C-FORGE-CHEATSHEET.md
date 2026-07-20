# C-Forge 1.3 — Guía rápida oficial

## Ejecutar y compilar

```bash
cforge programa.cfv
cforge --compilar programa.cfv -o build/programa
./build/programa
cforge fmt programa.cfv
cforge test programa.cfv
cforge --wasm programa.cfv -o build/programa.wat
cforge --vigilar programa.cfv
```

Sin archivo, `cforge` abre el REPL. Usa `salir` para cerrarlo.

## Variables y tipos

```cfv
nombre = "Javier";                 // tipo inferido
sea edad = 20;
sea saldo: numero = 1500;
sea activo: booleano = verdadero;
sea etiquetas: lista = ["forge", "vm"];
sea datos: mapa = {"version": 1.3};
sea vacio = nulo;
```

Tipos: `numero`, `texto`, `booleano`, `lista`, `mapa`, `cualquiera` y `nulo`.
El punto y coma es opcional al final de una instrucción.

## Salida, entrada y conversiones

```cfv
mostrar("Hola");
print(42);
nombre = leer("Nombre: ");
numero = a_numero("25");
texto = a_texto(25);
cantidad = longitud([1, 2, 3]);
```

## Operadores y control

```cfv
resultado = (10 + 5) * 2;
valido = resultado >= 20 y no falso;

si (valido) {
    mostrar("correcto");
} sino {
    mostrar("incorrecto");
}

contador = 0;
mientras (contador < 3) {
    contador = contador + 1;
}
```

## Funciones, estructuras y clases

```cfv
funcion sumar(a, b) {
    retornar a + b;
}

estructura Persona {
    nombre: texto;
    edad: numero;
}

clase Cuenta {
    campo saldo: numero;

    metodo depositar(cantidad) {
        este.saldo = este.saldo + cantidad;
        retornar este.saldo;
    }
}

persona = Persona("Javier", 20);
cuenta = Cuenta(100);
mostrar(cuenta.depositar(50));
```

## Listas, mapas y cálculo rápido

```cfv
lista = [10, 20, 30];
agregar(lista, 40);
mostrar(lista[0]);

mapa = {"nombre": "C-Forge", "version": 1.3};
mostrar(mapa.nombre);

vector = array_fast([1, 2, 3, 4]);
matriz = matrix(3, 4, 0);
mostrar(matriz[1][2]);
```

En programas compilados, `array_fast` usa `std::vector<double>` y `matrix` usa
almacenamiento denso contiguo.

## GPU, concurrencia, JIT y cluster

```cfv
cluster version = "1.3";

cluster funcion cuadrado(n) {
    retornar n * n;
}

gpu {
    resultados = paralelo("cuadrado", [2, 3, 4, 5]);
    mostrar(resultados);
}

mostrar(jit_estado("cuadrado"));
mostrar(jit_caliente("cuadrado"));
mostrar(cluster_estado());
```

`gpu` es un bloque acelerable. La versión 1.3 usa el backend paralelo de CPU;
Metal/CUDA requiere un backend específico para el hardware.

## Sistema y archivos

```cfv
info = sys_info();
mostrar(info.cpu);
mostrar(info.nucleos);
mostrar(info.ram_bytes);

proceso = sys_run("printf hola");
mostrar(proceso.estado);
mostrar(proceso.salida);
mostrar(proceso.error);

file_write("datos.txt", "Hola");
file_append("datos.txt", " C-Forge");
mostrar(file_read("datos.txt"));
```

`sys_run` ejecuta comandos reales del sistema: no construyas el comando con datos
externos que no sean confiables.

## TCP nativo

Servidor de una conexión con timeout en milisegundos:

```cfv
paquete = net_listen(8080, 5000);
mostrar(paquete.host);
mostrar(paquete.datos);
```

Cliente:

```cfv
bytes = net_send("127.0.0.1", 8080, "Hola por TCP");
mostrar(bytes);
```

`net_listen` escucha en loopback en la versión 1.3; no es un servidor HTTP.

## Errores y pruebas

```cfv
intentar {
    mostrar(10 / 0);
} capturar(error) {
    mostrar(error);
}

test "suma" {
    afirmar(sumar(20, 22) == 42, "resultado incorrecto");
}
```

## Módulos e interoperabilidad

```cfv
usar "utilidades.cfv";
import pip:math;
import npm:path;
import nuget:CSharpNative;
import maven:paquete;

mostrar(math.sqrt(81));
mostrar(path.extname("app.ts"));
mostrar(use_python("math", "pow", [2, 10]));
mostrar(use_javascript("path", "basename", ["/tmp/app.js"]));
mostrar(use_csharp("biblioteca.dylib", "sumar", [20, 22]));
mostrar(use_cpp("funcion_registrada", [20, 22]));
mostrar(use_java("app.jar", "MiClase", "metodo", [42]));
```

`use_cpp` se enlaza automáticamente si la implementación registrada se encuentra
en `interop/`, `native/`, `cpp/` o `ejemplos/interop/`.

## Código extranjero literal

```cfv
extern("python") {
    print("Python real")
}

extern("javascript") {
    console.log("JavaScript real");
}

extern("typescript") {
    const valor: number = 42;
    console.log(valor);
}

extern("java") {
    System.out.println("Java real");
}

extern("cpp") {
    std::cout << "C++ real" << std::endl;
}
```

## Instalación y diagnóstico

```bash
cforge --version
cforge --setup
sudo ./outputs/cforge-master --install
```
