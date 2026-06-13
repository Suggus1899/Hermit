# Fase 2 - Namespaces

## Objetivo
Aislar proceso con namespaces de PID, mount, network y UTS.

## Syscalls usadas
- clone con flags:
  - CLONE_NEWPID
  - CLONE_NEWNS
  - CLONE_NEWNET
  - CLONE_NEWUTS
- sethostname para hostname aislado.
- getpid para validar PID 1 en namespace nuevo.

## Estado implementado
- run crea proceso hijo con namespaces de Fase 2.
- hijo verifica PID 1 antes de ejecutar el comando.
- hostname configurable via:
  - hermit run --hostname demo /bin/sh
- errores de setup del hijo se reportan por pipe al padre.

## Criterios de aceptacion
- Proceso en contenedor reporta PID 1.
- Cambiar hostname dentro del contenedor no afecta al host.
- Namespace de red y mounts separados del host (sin configuracion avanzada aun).

## Pruebas automaticas
- Script de integracion: `tests/test_phase2.sh`
- Script integral fases 2-5: `tests/test_all_phases.sh`
- Script de interrupcion/cleanup: `tests/test_interrupt_cleanup.sh`
- Script init/reaper (F001): `tests/test_init_reaper.sh`
- Script userns (F002): `tests/test_userns.sh`
- Runner Make:
  - `make test_phase2`
  - `make test_all_phases`
  - `make test_interrupt_cleanup`
  - `make test_init_reaper`
  - `make test_userns`
  - `make test` (incluye unitarias + fase2 + integral + interrupcion)
- Cobertura del script:
  - Validacion de PID 1 dentro del contenedor.
  - Validacion de hostname aislado con `--hostname`.
  - Verificacion de net namespace distinto al host.
  - Verificacion de mount namespace distinto al host.

## Requisitos de ejecucion de pruebas
- Linux.
- Privilegios root en la configuracion actual del runtime (por `CLONE_NEWNS` y `CLONE_NEWNET`).

## Debug recomendado
- strace -f -e clone,sethostname,execve ./hermit run --hostname demo /bin/true
- Revisar errno reportado en logs si falla setup del hijo.

## Siguiente fase
- Fase 3: rootfs aislado con pivot_root y montaje de /proc.

## Estado actual del runtime
- Fase 3 implementada en runtime:
  - `--rootfs <path>`
  - `pivot_root` + `mount proc`
- Fase 4 implementada en runtime:
  - `--memory-max <bytes>`
  - `--pids-max <count>`
  - asignacion a cgroup v2 por proceso
- Fase 5 implementada en runtime:
  - `--net-veth`
  - configuracion host/child CIDR y gateway
  - teardown automatico del veth en host al terminar el contenedor
  - limpieza del cgroup efimero por contenedor
  - limpieza y finalizacion del hijo ante `SIGINT`/`SIGTERM` en el proceso padre

## Features en avance
- F001: init PID1 supervisor/reaper con forwarding de senales.
- F002: user namespace opcional via `--userns` con mapeo uid/gid 0->host uid/gid.
- F003/F004/F005:
  - `--mount-sys` (monta /sys)
  - `--sys-rw` (desactiva readonly por defecto en /sys)
  - `/proc` montado con flags seguros y `/dev` minimo por defecto
  - `--no-minimal-dev` para desactivar setup minimo de /dev
- F006/F007:
  - `--cpu-max <quota period>`
  - `--io-max <rule>`
- F008:
  - `--net-nat --out-if <iface>` para NAT basico sobre veth

## Diagnostico rapido
- Comando CLI: `hermit inspect`
- Muestra contadores basicos de posibles residuos:
  - entradas bajo `/sys/fs/cgroup/hermit`
  - interfaces del host que contienen `@` en `ip -o link show`
