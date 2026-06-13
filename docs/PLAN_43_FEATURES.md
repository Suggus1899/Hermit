# Plan Maestro de 43 Features - Hermit

## Objetivo
Ejecutar una ruta completa para llevar Hermit desde runtime avanzado a plataforma estilo Docker, con trazabilidad feature por feature.

## Convenciones
- Prioridad:
  - P0: bloquea arquitectura o seguridad base.
  - P1: funcionalidad de producto principal.
  - P2: mejora operativa.
- Estado inicial de todas: Planned.

## Estado actual (delta)
- F001: In Review (implementacion de init supervisor + reaper + signal forwarding en codigo; test dedicado agregado, pendiente ejecucion en Linux).
- F002: In Review (user namespace opcional implementado con mapeo uid/gid; test dedicado agregado, pendiente ejecucion en Linux).
- F003: In Progress (policy de mounts seguros aplicada en rootfs, incluyendo /sys con flags restrictivos por defecto).
- F004: In Progress (setup de /dev minimo en rootfs con nodos basicos y /dev/fd).
- F005: In Progress (mount de /sys habilitable con modo readonly por defecto y opcion rw).
- F006: In Progress (soporte de cpu.max en cgroup v2 via --cpu-max).
- F007: In Progress (soporte de io.max en cgroup v2 via --io-max).
- F008: In Progress (NAT opcional con veth via --net-nat --out-if).
- F010: In Progress (API local basica en hermitd con protocolo de comandos y respuestas JSON).
- F011: In Progress (operaciones create/delete/list de contenedores en estado local del daemon).
- F012: In Progress (listado de imagenes y base para inspect/logs via API local).
- F013: In Progress (journaling simple append+fsync y replay al startup del daemon).
- F014: In Progress (locking de estado con flock en mutaciones de metadata).
- F015: In Progress (CLI con cliente RPC local para images/ps/container create-rm-ls).
- F016: In Progress (socket con permisos 0600 y validacion de peer/payload basica).

## Backlog de 43 Features

1. F001 - Init PID1 robusto (reaper + señales) | Ola 1 | P0 | Dep: ninguna | DoD: contenedor no deja zombies bajo carga.
2. F002 - Namespace user opcional (CLONE_NEWUSER) | Ola 1 | P0 | Dep: F001 | DoD: mapeo uid/gid funcional con pruebas.
3. F003 - Política mount segura nodev/nosuid/noexec | Ola 1 | P0 | Dep: F001 | DoD: mounts críticos cumplen flags definidas.
4. F004 - Mount de /dev mínimo controlado | Ola 1 | P1 | Dep: F003 | DoD: /dev usable sin exposición excesiva.
5. F005 - Montaje /sys por perfil (ro/restringido) | Ola 1 | P1 | Dep: F003 | DoD: /sys aislado y validado.
6. F006 - Límite cpu.max en cgroup v2 | Ola 1 | P1 | Dep: F001 | DoD: throttling reproducible en benchmark.
7. F007 - Límite io.max en cgroup v2 | Ola 1 | P1 | Dep: F006 | DoD: I/O cap efectivo en pruebas.
8. F008 - Perfil de red NAT básico estable | Ola 1 | P1 | Dep: red actual | DoD: salida a internet controlada.
9. F009 - Teardown transaccional runtime (reintentos idempotentes) | Ola 1 | P0 | Dep: F001 | DoD: cero fugas en 100 ciclos run/kill.
10. F010 - API HTTP/Unix v1 contratos base | Ola 2 | P0 | Dep: F009 | DoD: esquema request/response versionado.
11. F011 - Endpoint create/start/stop/delete container | Ola 2 | P0 | Dep: F010 | DoD: CRUD lifecycle por API + tests.
12. F012 - Endpoint list/inspect/logs | Ola 2 | P1 | Dep: F011 | DoD: datos consistentes tras reinicio.
13. F013 - Persistencia de estado con journaling simple | Ola 2 | P0 | Dep: F011 | DoD: recuperación tras crash del daemon.
14. F014 - Locking de concurrencia por contenedor | Ola 2 | P0 | Dep: F013 | DoD: no hay corrupción con operaciones paralelas.
15. F015 - CLI cliente RPC (run pasa por daemon) | Ola 2 | P0 | Dep: F011 | DoD: CLI sin lógica pesada local.
16. F016 - Socket permissions + autenticación local básica | Ola 2 | P0 | Dep: F010 | DoD: acceso restringido por usuario/grupo.
17. F017 - Especificación de imagen Hermit v1 | Ola 3 | P0 | Dep: F015 | DoD: manifest/config/layers documentados y validados.
18. F018 - Import/export image save/load | Ola 3 | P1 | Dep: F017 | DoD: roundtrip sin pérdida de metadata.
19. F019 - Parser Hermitfile (FROM, WORKDIR, COPY, RUN) | Ola 3 | P0 | Dep: F017 | DoD: builds multi-step básicas estables.
20. F020 - Parser Hermitfile (ENV, CMD, ENTRYPOINT) | Ola 3 | P1 | Dep: F019 | DoD: ejecución final respeta config.
21. F021 - Builder por capas con digest sha256 | Ola 3 | P0 | Dep: F019 | DoD: capas deduplicadas por hash.
22. F022 - Cache de build local por step | Ola 3 | P1 | Dep: F021 | DoD: mejora de tiempo medible >=30% en rebuild.
23. F023 - .hermitignore y filtrado de contexto | Ola 3 | P1 | Dep: F019 | DoD: contexto excluye archivos definidos.
24. F024 - Comando image inspect completo | Ola 3 | P2 | Dep: F017 | DoD: muestra metadata y capas.
25. F025 - Driver overlayfs rootful | Ola 4 | P0 | Dep: F021 | DoD: RW layer estable y limpiable.
26. F026 - Fallback rootless sin overlayfs | Ola 4 | P1 | Dep: F025, F002 | DoD: ejecución funcional rootless.
27. F027 - Volume inspect/rm con referencias seguras | Ola 4 | P1 | Dep: base volumenes | DoD: no borra volumen en uso.
28. F028 - Bind mounts declarativos con validación anti-traversal | Ola 4 | P0 | Dep: F025 | DoD: paths saneados y auditables.
29. F029 - Garbage collector de capas huérfanas | Ola 4 | P1 | Dep: F025 | DoD: cleanup automático sin romper imágenes activas.
30. F030 - Registro local de uso de capas/volúmenes | Ola 4 | P2 | Dep: F029 | DoD: contadores coherentes tras reinicios.
31. F031 - Registry auth login/logout real | Ola 5 | P0 | Dep: F017 | DoD: auth funcional contra registry de prueba.
32. F032 - Push/pull de blobs y manifests | Ola 5 | P0 | Dep: F031 | DoD: transferencia completa de imagen.
33. F033 - Verificación estricta de digest y firma básica | Ola 5 | P0 | Dep: F032 | DoD: rechazo de blobs alterados.
34. F034 - Reintentos y resume de transferencias | Ola 5 | P1 | Dep: F032 | DoD: recuperación en redes inestables.
35. F035 - Mirrors de registry configurables | Ola 5 | P2 | Dep: F032 | DoD: fallback a mirror operativo.
36. F036 - Parser hermit-compose.yml v1 | Ola 6 | P0 | Dep: F015 | DoD: parsea services/networks/volumes.
37. F037 - Compose up/down/ps/logs | Ola 6 | P0 | Dep: F036 | DoD: stack multi-servicio usable.
38. F038 - depends_on + healthcheck + restart policy | Ola 6 | P1 | Dep: F037 | DoD: orden y auto-recuperación verificados.
39. F039 - Scheduler local reconcile loop | Ola 6 | P1 | Dep: F038 | DoD: converge al estado deseado.
40. F040 - Seguridad baseline: no_new_privs + capabilities mínimas + seccomp | Ola 7 | P0 | Dep: F015 | DoD: perfil default endurecido activo.
41. F041 - Rootless mode estable end-to-end | Ola 7 | P0 | Dep: F002, F026, F040 | DoD: run/build/pull/push sin root.
42. F042 - Observabilidad completa (stats/top/events + logs rotativos) | Ola 8 | P1 | Dep: F015 | DoD: diagnóstico operativo sin herramientas externas.
43. F043 - Calidad de release (CI matrix kernels, ASan/UBSan, empaquetado, systemd, 1.0.0) | Ola 9 | P0 | Dep: F001-F042 | DoD: pipeline verde y release reproducible.

## Orden de ejecución recomendado
- Sprint A: F001-F009
- Sprint B: F010-F016
- Sprint C: F017-F024
- Sprint D: F025-F030
- Sprint E: F031-F035
- Sprint F: F036-F039
- Sprint G: F040-F043

## Regla de avance
No iniciar una feature P1/P2 si la P0 de su ola está incompleta.

## Métrica de progreso
Progreso global = (features completadas / 43) * 100.
