# Hermit Roadmap Completo (Version Propia de Docker)

## Objetivo de producto
Construir Hermit como plataforma completa de contenedores en C, con arquitectura cliente/daemon, runtime de bajo nivel sobre syscalls del kernel Linux, builder de imagenes, registry, volumenes, red, orquestacion local y controles de seguridad listos para uso real.

## Plan maestro de ejecucion
- Ver backlog operativo en `docs/PLAN_43_FEATURES.md`.

## Arquitectura final objetivo
- hermit: cliente CLI.
- hermitd: daemon residente.
- hermit-shim: proceso intermedio por contenedor para desacoplar lifecycle.
- API local por Unix socket: /run/hermit/hermit.sock.
- Data root: /var/lib/hermit (rootful) y ~/.local/share/hermit (rootless).
- Estado en disco:
  - containers/
  - images/
  - layers/
  - volumes/
  - networks/
  - registry/
  - orchestrator/

## Principios de implementacion
- C puro y llamadas al sistema explicitas.
- Sin fuga de FDs, entorno, mounts ni namespaces del host.
- Seguridad por defecto: minimo privilegio.
- Compatibilidad Linux moderna (cgroups v2, namespaces, overlayfs cuando aplique).
- Observabilidad y testing desde etapas tempranas.

## Ruta completa por olas

### Ola 0 - Fundacion de codigo y disciplina (Semana 1)
Entregables:
- Reorganizar codigo en modulos:
  - src/cli/
  - src/runtime/
  - src/daemon/
  - src/storage/
  - src/net/
  - src/cgroups/
  - src/security/
  - src/common/
- Estilo y normas de errores (errno + contexto + codigo de salida).
- Logging estructurado basico (nivel, modulo, mensaje).
- Makefile multi-binario (hermit, hermitd, pruebas).

Criterios de aceptacion:
- Compila sin warnings en -Wall -Wextra -Wpedantic.
- Todos los modulos exportan interfaces claras y testeables.

### Ola 1 - Runtime aislado completo (Semanas 2-4)
Incluye Fases 2, 3, 4 y 5 del runtime original.

Entregables:
- Namespaces:
  - CLONE_NEWPID
  - CLONE_NEWNS
  - CLONE_NEWNET
  - CLONE_NEWUTS
  - opcional: CLONE_NEWIPC, CLONE_NEWUSER
- Proceso init interno (PID 1) con recoleccion de zombies.
- sethostname aislado por UTS namespace.
- Rootfs:
  - mount propagation privada.
  - pivot_root seguro.
  - desmontaje de raiz vieja.
  - montaje de /proc, /sys (segun politica), /dev minimo.
- cgroups v2:
  - pids.max
  - memory.max
  - cpu.max
  - io.max (si esta disponible)
- Red:
  - par veth host-contenedor.
  - configuracion IP interna.
  - forwarding y NAT opcionales por perfil.

Criterios de aceptacion:
- El proceso dentro del contenedor ve PID 1.
- No puede ver procesos del host.
- No accede al rootfs del host.
- Limites de memoria/pids/cpu efectivos en stress tests.
- Red aislada con conectividad controlada.

### Ola 2 - Daemonizacion y API (Semanas 5-6)
Entregables:
- Binario hermitd con loop de eventos.
- API RPC local por Unix socket:
  - /v1/containers/create
  - /v1/containers/start
  - /v1/containers/stop
  - /v1/containers/delete
  - /v1/containers/list
  - /v1/containers/logs
  - /v1/images/list
- CLI hermit como cliente RPC (sin logica pesada local).
- Control de concurrencia y lockfiles de estado.

Criterios de aceptacion:
- CLI y daemon separados funcionalmente.
- Reinicio del daemon conserva estado de contenedores.
- Socket con permisos estrictos y validacion de payload.

### Ola 3 - Motor de imagenes y build (Semanas 7-10)
Entregables:
- Especificacion de imagen Hermit v1:
  - manifest.json
  - config.json
  - layers tar + sha256
- Hermitfile parser:
  - FROM
  - WORKDIR
  - COPY
  - RUN
  - ENV
  - CMD
  - ENTRYPOINT
- Builder por capas con cache por digest.
- hermit build, hermit image inspect, hermit save/load.

Criterios de aceptacion:
- Builds reproducibles en mismo contexto.
- Reutilizacion de cache medible.
- Imagen exportable/importable sin perdida de metadata.

### Ola 4 - Storage driver y volumenes (Semanas 11-12)
Entregables:
- Driver de capas:
  - overlayfs rootful.
  - fallback rootless cuando overlay no aplique.
- Volumenes:
  - create, ls, inspect, rm.
  - bind mounts declarativos en run.
  - politicas de permisos y ownership.
- Garbage collection de capas no referenciadas.

Criterios de aceptacion:
- Persistencia de datos entre reinicios de contenedor.
- Sin corrupcion bajo ejecucion concurrente.

### Ola 5 - Registry completo (Semanas 13-14)
Entregables:
- Cliente registry:
  - login/logout
  - push
  - pull
  - tag
- Soporte de manifest y blob upload/download.
- Verificacion estricta de digest.
- Almacenamiento seguro de credenciales:
  - helper externo o cifrado local.

Criterios de aceptacion:
- Push/pull correcto contra registry de prueba.
- Deteccion de blobs corruptos o truncados.

### Ola 6 - Orquestacion local tipo compose (Semanas 15-17)
Entregables:
- Formato hermit-compose.yml:
  - services
  - volumes
  - networks
  - depends_on
  - healthcheck
  - restart
- Comandos:
  - hermit compose up
  - hermit compose down
  - hermit compose ps
  - hermit compose logs
- Scheduler local single-host:
  - orden de arranque
  - reconciliacion basica
  - restart policies

Criterios de aceptacion:
- Stack multi-servicio estable en host unico.
- Recuperacion basica ante caidas de procesos.

### Ola 7 - Seguridad avanzada y cumplimiento (Semanas 18-20)
Entregables:
- Rootless mode estable.
- no_new_privs por defecto.
- Capabilities minimas por perfil.
- Seccomp baseline profile.
- Integracion opcional AppArmor/SELinux.
- Politica de secretos (sin exponer en env por defecto).

Criterios de aceptacion:
- Pruebas de escape comunes mitigadas.
- Auditoria de superficie de privilegios documentada.

### Ola 8 - Observabilidad y operacion (Semanas 21-22)
Entregables:
- Eventos del daemon y timeline de contenedor.
- hermit stats/top/events.
- Logs rotativos por contenedor.
- Inspect profundo (mounts, netns, cgroup paths, limits).

Criterios de aceptacion:
- Diagnostico rapido de fallos sin herramientas externas.

### Ola 9 - Calidad, compatibilidad y release (Semanas 23-24)
Entregables:
- Suite de pruebas:
  - unitarias
  - integracion
  - e2e de aislamiento
  - regresion de seguridad
- CI:
  - build matrix kernel versions
  - lint estatico
  - sanitizers (ASan/UBSan)
- Paquetizacion:
  - binarios
  - systemd unit para hermitd
  - instalador/documentacion de operaciones
- Version 1.0.0 de Hermit.

Criterios de aceptacion:
- Tasa de exito e2e >= 95% en CI definida.
- Guia de despliegue reproducible para entornos Linux target.

## Backlog detallado por areas

### Runtime
- Init interno con manejo de senales.
- Reaper de zombies.
- Politica de mount flags seguros (nodev,nosuid,noexec donde aplique).

### Red
- IPAM simple para subred local.
- Bridge virtual hermit0.
- NAT e ingress basico con reglas controladas.

### API/Daemon
- Versionado de API y compatibilidad.
- Control de sesiones y timeout por request.
- Recuperacion de estado tras crash.

### Build
- Context filtering (.hermitignore).
- Cache remota opcional.
- Build args y secrets de build.

### Registry
- Soporte de mirrors.
- Retries y resume de transferencia.

### Compose
- Escalado por replicas.
- Update strategy rolling simple.

### Seguridad
- Escaneo de configuracion insegura en runtime.
- Politicas default-deny en perfiles endurecidos.

## Hitos de demostracion (milestones)
- M1: Runtime aislado completo (fin Ola 1).
- M2: CLI + daemon + API estable (fin Ola 2).
- M3: Build e imagenes productivas (fin Ola 3).
- M4: Storage y volumenes robustos (fin Ola 4).
- M5: Registry operativo (fin Ola 5).
- M6: Orquestacion local usable (fin Ola 6).
- M7: Seguridad endurecida (fin Ola 7).
- M8: Observabilidad operativa (fin Ola 8).
- M9: Release 1.0.0 (fin Ola 9).

## Riesgos y mitigaciones
- Riesgo: complejidad de rootless y user namespaces.
  - Mitigacion: habilitar rootful primero y rootless por perfiles.
- Riesgo: diferencias entre kernels/distribuciones.
  - Mitigacion: matriz CI por versiones y feature detection runtime.
- Riesgo: seguridad en interfaces de red y mounts.
  - Mitigacion: defaults restrictivos + auditorias periodicas.
- Riesgo: corrupcion de estado en daemon.
  - Mitigacion: journaling simple + fsync en puntos criticos.

## Definicion de terminado (Definition of Done)
- Cada feature con pruebas unitarias + integracion.
- Cada syscall critica con ruta de error validada.
- Sin fugas de FDs/mounts/namespaces en pruebas e2e.
- Telemetria basica y logs accionables.
- Documentacion de uso y operacion actualizada.
