# Política de completitud de C-Forge

C-Forge publica únicamente afirmaciones respaldadas por evidencia reproducible.
La honestidad sobre el alcance es parte de la calidad del lenguaje.

## Estados oficiales

- **Verificado en Developer Preview:** funciona dentro del alcance y plataformas
  cubiertos por pruebas, pero todavía no posee garantía de estabilidad.
- **Experimental:** existe y puede probarse; su API o semántica puede cambiar.
- **Parcial:** solo está implementado el subconjunto expresamente documentado.
- **Planeado:** es una decisión de hoja de ruta, no una función disponible.
- **No certificado:** no existe evidencia externa suficiente para la afirmación.

“Estable” se reservará para capacidades con especificación congelada,
compatibilidad publicada, pruebas multiplataforma, política de soporte y
artefactos reproducibles.

## Definición de terminado

Una capacidad solo puede anunciarse como terminada cuando:

1. su semántica está documentada sin ambigüedades;
2. posee pruebas positivas, negativas y de regresión;
3. funciona en cada backend y plataforma que afirma soportar;
4. sus errores se muestran como diagnósticos C-Forge sin tracebacks internos;
5. sus ejemplos y comandos existen y se ejecutan;
6. sus límites, permisos y dependencias están documentados;
7. existe evidencia reproducible enlazada desde `capabilities.json`;
8. CI verifica esa evidencia en cada cambio.

Una función en progreso se marca como experimental o parcial. Una idea futura se
marca como planeada. Ningún README, anuncio o paquete puede elevar su estado sin
actualizar primero las pruebas y la evidencia.

## Umbral cuantitativo de Developer Preview verificado

Una capacidad solo puede usar el estado `verified-preview` si:

1. sus pruebas positivas, negativas y de regresión terminan sin fallos ni errores;
2. cada comportamiento público nuevo incorpora al menos una prueba de regresión;
3. el fuzzing determinista del núcleo ejecuta como mínimo 20.000 casos en CI;
4. el comando, número de pasadas y total están declarados en `capabilities.json`;
5. cada plataforma incluida en su alcance produce evidencia verde en CI.

El gate comprueba que README, manifiesto y workflows utilicen la misma orden
oficial. Un cambio de umbral debe actualizar los tres en el mismo commit.

## Núcleo autónomo

La implementación Python es el oráculo histórico de referencia actual. Stage 0
es un compilador mínimo C++17 usado únicamente para arrancar Stage 1. No se
declarará autónomo hasta que el compilador escrito en C-Forge compile y ejecute
el núcleo sin depender de Python, C++, JVM, .NET, Node ni LLVM y pase el contrato
Stage 2/3.

El contrato de Stage 0/1/2/3 y el subconjunto congelado están definidos en
[`BOOTSTRAP.md`](BOOTSTRAP.md). Un componente escrito en `.cfv` no convierte por
sí solo al lenguaje en autoalojado: la afirmación exige recompilación Stage 2/3
reproducible. Los adaptadores extranjeros quedan fuera del núcleo.

## Producción crítica

C-Forge no está certificado para bancos ni producción crítica. Esa clasificación
requiere auditoría profesional independiente, corrección verificada de hallazgos,
versiones LTS, builds firmados, recuperación, observabilidad y cumplimiento
regulatorio aplicable.
