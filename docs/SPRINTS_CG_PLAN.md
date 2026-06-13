# Plan de Implementación Sprints C-G (F017-F043)

## Sprint C: Features de Imágenes (F017-F024)

### F017 - Especificación de imagen Hermit v1
- Crear estructura de directorio para imágenes
- Implementar formato de manifest JSON
- Validar estructura de layers
- Soporte para config.json estándar

### F018 - Import/export image save/load
- `hermit save <image> -o <file>`
- `hermit load -i <file>`
- Serialización de metadata y layers
- Compresión de blobs

### F019 - Parser Hermitfile (FROM, WORKDIR, COPY, RUN)
- Parser línea por línea
- Validación de sintaxis
- Ejecución secuencial de steps
- Manejo de contexto de build

### F020 - Parser Hermitfile (ENV, CMD, ENTRYPOINT)
- Variables de entorno
- Comandos por defecto
- Puntos de entrada
- Override de runtime

### F021 - Builder por capas con digest sha256
- Cálculo de hash por capa
- Deduplicación por contenido
- Almacenamiento eficiente
- Referencias cruzadas

### F022 - Cache de build local por step
- Identificación de steps cacheables
- Validación de inputs
- Reutilización inteligente
- Invalidación por cambios

### F023 - .hermitignore y filtrado de contexto
- Soporte para patrones .dockerignore
- Exclusión de archivos sensibles
- Optimización de contexto
- Validación de patrones

### F024 - Comando image inspect completo
- Metadata completa
- Lista de layers con digests
- Historial de builds
- Información de tamaño

## Sprint D: Storage y Volúmenes (F025-F030)

### F025 - Driver overlayfs rootful
- Montaje overlayfs para RW layer
- Gestión de lower/upper/work dirs
- Commit de cambios
- Limpieza de temporales

### F026 - Fallback rootless sin overlayfs
- Copy-on-write manual
- Gestión de permisos
- Modo compatible sin privilegios
- Performance aceptable

### F027 - Volume inspect/rm con referencias seguras
- Conteo de referencias activas
- Prevención de borrado en uso
- Inspección de metadatos
- Operaciones atómicas

### F028 - Bind mounts declarativos con validación
- Especificación en config
- Validación anti-path traversal
- Sanitización de rutas
- Auditoría de accesos

### F029 - Garbage collector de capas huérfanas
- Identificación de layers no referenciadas
- Análisis de dependencias
- Limpieza segura
- Liberación de espacio

### F030 - Registro local de uso de capas/volúmenes
- Contadores persistentes
- Estadísticas de uso
- Métricas de rendimiento
- Reportes de consumo

## Sprint E: Registry y Red (F031-F035)

### F031 - Registry auth login/logout real
- Soporte OAuth2 Bearer tokens
- Almacenamiento seguro de credenciales
- Refresh automático de tokens
- Multi-registry support

### F032 - Push/pull de blobs y manifests
- HTTP/2 chunked transfers
- Resumen de progreso
- Reintentos automáticos
- Validación de integridad

### F033 - Verificación estricta de digest y firma
- Validación SHA256
- Soporte para firmas (cosign)
- Chain of trust
- Rechazo de blobs alterados

### F034 - Reintentos y resume de transferencias
- Lógica exponencial backoff
- Reanudación de descargas
- Manejo de timeouts
- Recuperación de errores

### F035 - Mirrors de registry configurables
- Fallback automático
- Balanceo de carga
- Configuración por registry
- Health checks

## Sprint F: Orquestación (F036-F039)

### F036 - Parser hermit-compose.yml v1
- Soporte YAML completo
- Validación de esquema
- Variables de entorno
- Templates y substitución

### F037 - Compose up/down/ps/logs
- Gestión del ciclo de vida
- Orden de dependencies
- Logs agregados
- Status en tiempo real

### F038 - depends_on + healthcheck + restart policy
- Grafos de dependencias
- Checks de salud
- Políticas de reinicio
- Recuperación automática

### F039 - Scheduler local reconcile loop
- Estado deseado vs actual
- Reconciliación continua
- Event-driven updates
- Convergencia garantizada

## Sprint G: Seguridad y Calidad (F040-F043)

### F040 - Seguridad baseline
- no_new_privileges por defecto
- Capabilities mínimas
- Seccomp profiles
- AppArmor/SELinux integration

### F041 - Rootless mode estable end-to-end
- Testing completo sin root
- Feature parity con modo rootful
- Performance comparativa
- Documentación de limitaciones

### F042 - Observabilidad completa
- Metrics Prometheus
- Logs estructurados
- Events streaming
- Health endpoints

### F043 - Calidad de release
- CI matrix kernels (Ubuntu, Alpine, Busybox)
- ASan/UBSan builds
- Empaquetado (deb, rpm, tar.gz)
- Systemd service files
- SemVer 1.0.0

## Orden de Dependencias

```
Sprint C (F017-F024) ← Sprint D (F025-F030) ← Sprint E (F031-F035)
                ↓                      ↓                    ↓
           Sprint F (F036-F039) ← Sprint G (F040-F043)
```

## Métricas de Progreso

- **Sprint C**: 8/43 features (18.6%)
- **Sprint D**: 6/43 features (13.9%) 
- **Sprint E**: 5/43 features (11.6%)
- **Sprint F**: 4/43 features (9.3%)
- **Sprint G**: 4/43 features (9.3%)

**Total planeado**: 27/43 features (62.8%)

## Próximos Pasos

1. **Inmediato**: Comenzar Sprint C con F017 (especificación de imagen)
2. **Paralelo**: Investigar formatos OCI para compatibilidad
3. **Validación**: Crear tests para cada feature antes de implementar
4. **Documentación**: Actualizar docs con cada feature completada
