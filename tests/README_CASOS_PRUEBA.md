# Casos de Prueba - MatCom Guard

Este directorio contiene casos de prueba independientes que simulan actividades sospechosas para verificar la detección del sistema MatCom Guard.

## Ejecución Rápida

**Para ejecutar todos los tests de forma interactiva:**
```bash
cd tests
./run_all_tests.sh
```

**Para ejecutar un test específico:**
```bash
cd tests/usb_tests
./test_usb_insertion.sh
```

## Estructura

```
tests/
├── README_CASOS_PRUEBA.md          # Este archivo
├── run_all_tests.sh               # Script principal interactivo
├── usb_tests/                      # Pruebas de dispositivos USB
│   ├── test_usb_insertion.sh       # Inserción USB con baseline
│   ├── test_usb_file_modification.sh  # Modificación de archivos
│   ├── test_usb_new_file.sh        # Archivos sospechosos nuevos  
│   ├── test_usb_file_deletion.sh   # Eliminación de archivos
│   └── test_usb_permissions.sh     # Cambios de permisos
├── process_tests/                  # Pruebas de procesos
│   ├── test_legitimate_process.sh  # Procesos legítimos
│   ├── test_malicious_cpu.sh       # Uso excesivo de CPU
│   ├── test_memory_leak.sh         # Fuga de memoria
│   └── test_process_cleanup.sh     # Limpieza de procesos
└── port_tests/                     # Pruebas de puertos
    ├── test_ssh_port.sh            # Puerto SSH legítimo
    ├── test_suspicious_port.sh     # Puerto 31337 sospechoso
    ├── test_high_port.sh           # HTTP en puerto alto
    └── test_closed_port.sh         # Escaneo de puertos cerrados
```

## Requisitos Previos

1. **MatCom Guard debe estar ejecutándose** en otra terminal:
   ```bash
   cd MatCom-Guard-SO-Project
   make clean && make
   ./matcom-guard
   ```

2. **Permisos de root** para algunas pruebas (montaje USB, apertura de puertos privilegiados)

3. **Herramientas necesarias**:
   - `netcat` (nc)
   - `python3`
   - Acceso a un dispositivo USB real o imagen de disco

## Cómo Ejecutar los Tests

**IMPORTANTE**: Ejecuta cada test individualmente con MatCom Guard activo para observar las alertas específicas.

### 1. Iniciar MatCom Guard
```bash
# En una terminal separada
cd MatCom-Guard-SO-Project
make clean && make
./matcom-guard
```

### 2. Ejecutar Tests Individuales

**Pruebas de USB** (ejecutar en orden - **requiere sudo**):
```bash
cd tests/usb_tests
chmod +x *.sh

# 1. Crear baseline de dispositivo USB
sudo ./test_usb_insertion.sh

# 2. Modificar archivo existente
./test_usb_file_modification.sh

# 3. Crear archivos sospechosos
./test_usb_new_file.sh

# 4. Eliminar archivos legítimos
./test_usb_file_deletion.sh

# 5. Cambiar permisos de archivos
./test_usb_permissions.sh
```

**Pruebas de Procesos** (independientes):
```bash
cd tests/process_tests
chmod +x *.sh

./test_legitimate_process.sh    # Proceso en whitelist
./test_malicious_cpu.sh        # Alto uso de CPU
./test_memory_leak.sh          # Alto uso de RAM
./test_process_cleanup.sh      # Verificar limpieza
```

**Pruebas de Puertos** (independientes):
```bash
cd tests/port_tests
chmod +x *.sh

./test_ssh_port.sh             # Puerto SSH legítimo
./test_suspicious_port.sh      # Puerto backdoor (31337)
./test_high_port.sh           # Puerto no estándar (4444)
./test_closed_port.sh         # Puerto cerrado (9999)
```

## Interpretación de Resultados

- **Verificar en MatCom Guard**: Todas las alertas deben aparecer en la interfaz gráfica
- **Logs del sistema**: Revisar logs en `/var/log/` si están habilitados
- **Tiempo de detección**: Las alertas deberían aparecer dentro de 5-10 segundos

## Notas Importantes

- **Ejecutar con precaución**: Algunos tests pueden afectar el rendimiento del sistema
- **Limpiar después**: Los tests se limpian automáticamente al terminar
- **Monitoreo continuo**: Mantener MatCom Guard ejecutándose durante todas las pruebas
- **Permisos**: Algunos tests USB requieren `sudo`
- **Limpieza de procesos**: Si hay procesos de test antiguos ejecutándose, usar:
  ```bash
  pkill -f test_port_advanced_exec  # Limpiar tests antiguos
  pkill -f "tests/test_"           # Limpiar otros tests
  ```

## Troubleshooting

- **Test no detectado**: Verificar que MatCom Guard esté ejecutándose y configurado correctamente
- **Permisos denegados**: Ejecutar con `sudo` cuando sea necesario para tests USB
- **Puerto ocupado**: Verificar que los puertos no estén siendo usados por otros servicios
- **USB no detectado**: Verificar que el dispositivo USB esté montado correctamente
- **GUI no actualiza puertos**: Si los puertos no cambian en la GUI, limpiar procesos de test antiguos
- **Procesos zombi**: Usar `pkill -9 -f test_` para terminar procesos problemáticos

## Casos de Prueba Detallados

### Tests de USB
1. **Inserción USB** (`test_usb_insertion.sh`)
   - Simula montaje de USB con archivos conocidos
   - Establece baseline de hashes
   - **Resultado esperado**: Detección y registro del dispositivo

2. **Modificación de archivo** (`test_usb_file_modification.sh`)
   - Cambia contenido de archivo.txt
   - **Resultado esperado**: "Cambio detectado en archivo.txt (hash modificado)"

3. **Archivo nuevo sospechoso** (`test_usb_new_file.sh`)
   - Crea malware.exe, keylogger.dll, etc.
   - **Resultado esperado**: "Archivo sospechoso añadido: malware.exe"

4. **Eliminación de archivo** (`test_usb_file_deletion.sh`)
   - Elimina documento.pdf legítimo
   - **Resultado esperado**: "Archivo eliminado: documento.pdf"

5. **Cambio de permisos** (`test_usb_permissions.sh`)
   - chmod 777 en script.sh
   - **Resultado esperado**: "Permisos modificados en script.sh (ahora 777)"

### Tests de Procesos
1. **Proceso legítimo** (`test_legitimate_process.sh`)
   - Ejecuta gzip, find (alta CPU legítima)
   - **Resultado esperado**: Sin alertas si están en lista blanca

2. **Proceso malicioso** (`test_malicious_cpu.sh`)
   - while true; do :; done
   - **Resultado esperado**: "Proceso 'bash' usa 99% CPU (umbral: 70%)"

3. **Fuga de memoria** (`test_memory_leak.sh`)
   - Script Python que consume RAM, tail /dev/zero
   - **Resultado esperado**: "Proceso usa X% RAM (umbral: 50%)"

4. **Limpieza de procesos** (`test_process_cleanup.sh`)
   - Crea y termina procesos
   - **Resultado esperado**: Procesos desaparecen del monitor

### Tests de Puertos
1. **Puerto SSH** (`test_ssh_port.sh`)
   - Inicia SSH en puerto 22 o simula en 2222
   - **