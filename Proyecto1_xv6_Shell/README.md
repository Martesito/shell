# Proyecto 1 - xv6 Shell

## Objetivo

Implementar un interprete de comandos de espacio de usuario para xv6-riscv,
capaz de ejecutar comandos simples con argumentos, redireccionar entrada y
salida mediante `<` y `>`, y conectar comandos mediante tuberias `|`.

El programa se llama `mish` y su fuente es `user/mish.c`.

## Relacion con la presentacion

La implementacion usa directamente las abstracciones explicadas en el
Capitulo 1:

- `fork()` para crear el proceso que ejecutara el comando.
- `exec()` para reemplazar la imagen del hijo por el programa solicitado.
- `wait()` para que el shell espere al comando.
- `open()` y `close()` para montar redirecciones.
- `dup()` para conectar descriptores estandar con una tuberia.
- `pipe()` para crear el canal entre procesos.

La idea central es aprovechar que los descriptores 0, 1 y 2 representan,
respectivamente, entrada estandar, salida estandar y error estandar.

## Instalacion en xv6

1. Copiar `mish.c` a:

   `user/mish.c`

2. Abrir el `Makefile` de xv6.

3. En la lista `UPROGS`, agregar:

   `_mish\`

   respetando el formato de los demas programas.

4. Desde la raiz de xv6 ejecutar:

   `make qemu`

5. En el prompt de xv6 escribir:

   `mish`

## Arquitectura

### Comando simple

El proceso principal del shell hace:

`fork()`

El hijo hace:

`exec(argv[0], argv)`

El padre hace:

`wait(0)`

### Redireccion

Para:

`cat < archivo`

el proceso que ejecuta `cat` hace conceptualmente:

`close(0)`

`open("archivo", O_RDONLY)`

Como 0 acaba de quedar libre, `open` lo obtiene como descriptor.

Para:

`echo hola > archivo`

se hace lo mismo con descriptor 1:

`close(1)`

`open("archivo", O_CREATE|O_WRONLY|O_TRUNC)`

### Tuberia

Para:

`a | b`

se crea:

`pipe(p)`

Luego:

- el proceso de `a` conecta `stdout` con `p[1]`;
- el proceso de `b` conecta `stdin` con `p[0]`;
- todos los procesos cierran los extremos que no necesitan.

La ultima parte es fundamental para que los lectores reciban EOF y no queden
bloqueados.

### Varias etapas

Una expresion como:

`a | b | c`

se representa como una estructura recursiva de tuberias:

`a | (b | c)`

Esto permite reutilizar la misma logica para cualquier numero de etapas,
dentro del limite de recursos de xv6.

## Decisiones de diseno

- Se acepta `<`, `>` y `|` sin exigir espacios alrededor de ellos.
- Se admite redireccion antes o despues de los argumentos.
- Se admite combinar redireccion con tuberias.
- `>` trunca el archivo existente mediante `O_TRUNC`.
- Los errores de `fork`, `pipe`, `open`, `dup` y `exec` se comprueban.
- El shell padre no altera sus propios descriptores al ejecutar comandos.

## Pruebas

El archivo `shell_tests.txt` contiene pruebas funcionales y casos de error.

Las pruebas 1-12 cubren el nucleo del enunciado.
Las pruebas 13-16 verifican robustez adicional, especialmente tuberias
multiples y combinaciones de redireccion.

## Nota sobre la version de xv6

La presentacion del curso advierte que las referencias de lineas de `user/sh.c`
dependen de la revision concreta de xv6. Antes de evaluar, conviene verificar
que el repositorio local corresponde a la version usada por el profesor.

## Nota sobre la salida de wc

El conteo exacto de bytes depende del contenido del archivo. Por ejemplo:

`alfa beta gamma\n`

tiene 16 bytes:

- alfa = 4
- espacio = 1
- beta = 4
- espacio = 1
- gamma = 5
- salto de linea = 1

Total: 16.
