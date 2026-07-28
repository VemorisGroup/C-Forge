# C-Forge Benchmark Suite

Esta suite mide el intérprete y el backend nativo de C-Forge sin confundir una
prueba segura con una prueba destructiva. Los tamaños predeterminados permiten
comprobar estabilidad en un computador personal.

## Archivos

- `01_cpu_primos.cfv`: búsqueda de primos mediante división de prueba.
- `02_cpu_pi.cfv`: aproximación de π con la serie de Leibniz.
- `03_memoria_mandelbrot.cfv`: genera una imagen PGM fractal.
- `04_memoria_arboles.cfv`: crea y libera árboles binarios recursivos.
- `05_concurrencia.cfv`: distribuye trabajos con `paralelo`.
- `06_io_masiva.cfv`: escribe, lee y valida un archivo de varios MiB.
- `ejecutar_suite.sh`: compila y ejecuta cada prueba de manera controlada.

## Ejecutar

Desde la raíz del repositorio:

```bash
./benchmarks/ejecutar_suite.sh
```

Los ejecutables y archivos temporales se guardan en `build/benchmarks`.

Para producir un resultado JSON reproducible con calentamiento, muestras crudas y
mediana:

```bash
python3 benchmarks/run_reproducible.py --runs 7
```

Una comparación contra otro lenguaje solo es válida si usa el mismo algoritmo, entrada,
hardware, configuración energética y número de repeticiones. El repositorio no publica
cifras inventadas ni mezcla tiempo de compilación con tiempo de ejecución.

## Límites importantes

- No se crean un millón de hilos del sistema. El benchmark usa 500 trabajos;
  un hilo por tarea agotaría la RAM y mediría el fallo del sistema operativo.
- No se genera automáticamente un archivo de varios GiB. La prueba usa cerca
  de 4 MiB y puede aumentarse gradualmente.
- La prueba de primos no es todavía una Criba de Eratóstenes: C-Forge aún no
  admite asignación directa de elementos (`lista[indice] = valor`). Esta prueba
  deja documentada esa limitación y mide los bucles disponibles actualmente.
- Antes de publicar cifras, ejecuta cada prueba al menos cinco veces, descarta
  la primera y reporta mediana, hardware, sistema operativo y versión.
