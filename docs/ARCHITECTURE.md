# Arquitectura Tecnica - Hermit (Ola 0)

## Objetivo
Definir la arquitectura modular base para evolucionar Hermit desde una CLI monolitica hacia un sistema tipo Docker con cliente y daemon.

## Binarios
- hermit:
	- Cliente CLI principal.
	- Inicializa contexto local de datos.
	- Despacha comandos al runtime local (temporal) o al daemon (futuro).
- hermitd:
	- Daemon residente.
	- Expone API local por Unix socket.
	- Actualmente responde como stub para validar ciclo de vida del proceso servidor.

## Modulos actuales
- include/hermit/common
	- error.h: salida fatal con contexto y errno.
	- log.h: logger estructurado con niveles.
	- fs.h: utilidades de filesystem y data root.
- include/hermit/runtime
	- run.h: ejecucion aislada de procesos.
- include/hermit/cli
	- commands.h: parser/despachador de subcomandos.
- include/hermit/daemon
	- server.h: ciclo principal del daemon.

- src/common
	- error.c
	- log.c
	- fs.c
- src/runtime
	- run.c
- src/cli
	- main.c
	- commands.c
- src/daemon
	- main.c
	- server.c

## Flujo actual
1. Usuario invoca hermit.
2. CLI inicializa data root (~/.hermit).
3. CLI despacha comando:
	 - run: usa runtime directo local.
	 - images/ps/volume/build/registry/orchestrate: operaciones locales/stub.
4. Si se invoca hermitd:
	 - arranca socket Unix en /tmp/hermit.sock.
	- acepta conexiones y procesa comandos API basicos:
	  - PING
	  - CONTAINERS.CREATE <name>
	  - CONTAINERS.DELETE <name>
	  - CONTAINERS.LIST
	  - IMAGES.LIST

## Seguridad base implementada
- Limpieza de entorno antes de exec en proceso hijo.
- Cierre de FDs heredados no esenciales.
- Pipe CLOEXEC para transportar error hijo->padre.
- Errores con contexto (errno + mensaje).

## Estado de Fase 2 en runtime
- clone con namespaces:
	- CLONE_NEWPID
	- CLONE_NEWNS
	- CLONE_NEWNET
	- CLONE_NEWUTS
- Verificacion de PID 1 en el hijo.
- sethostname aislado (opcion --hostname).
- PID 1 corre como init supervisor:
	- lanza payload en hijo dedicado
	- reenvia senales al grupo del payload
	- reapea procesos huerfanos/zombies en el namespace

## Decisiones para Ola 1
- Mantener API de runtime estable en src/runtime.
- Evitar logica de negocio en main.c.
- Preparar transicion de run local a RPC con hermitd.

## Proximos cambios estructurales
- Introducir contratos API en include/hermit/api.
- Definir protocolo request/response versionado.
- Mover comando run para ejecutarse via daemon.
