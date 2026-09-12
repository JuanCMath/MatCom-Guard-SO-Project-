#!/bin/bash

# Script principal para ejecutar todos los casos de prueba de MatCom Guard
# Ejecuta todos los tests de forma secuencial y organizada

echo "======================================"
echo "       MATCOM GUARD - TEST SUITE"
echo "======================================"
echo ""

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función para imprimir con colores
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Verificar que MatCom Guard esté ejecutándose
check_matcom_guard() {
    print_status "Verificando que MatCom Guard esté ejecutándose..."
    
    if pgrep -f "matcom-guard" > /dev/null; then
        print_success "MatCom Guard detectado ejecutándose"
        return 0
    else
        print_warning "MatCom Guard no parece estar ejecutándose"
        echo ""
        echo "Por favor, inicia MatCom Guard en otra terminal:"
        echo "  cd MatCom-Guard-SO-Project"
        echo "  make clean && make"
        echo "  ./matcom-guard"
        echo ""
        read -p "¿Continuar de todos modos? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            return 1
        fi
    fi
}

# Función para ejecutar un test
run_test() {
    local test_script="$1"
    local test_name="$2"
    
    echo ""
    echo "======================================"
    print_status "Ejecutando: $test_name"
    echo "======================================"
    
    if [ -f "$test_script" ]; then
        chmod +x "$test_script"
        bash "$test_script"
        local exit_code=$?
        
        if [ $exit_code -eq 0 ]; then
            print_success "Test completado: $test_name"
        else
            print_error "Test falló: $test_name (código: $exit_code)"
        fi
        
        echo ""
        read -p "Presiona Enter para continuar al siguiente test..." -r
        return $exit_code
    else
        print_error "Script no encontrado: $test_script"
        return 1
    fi
}

# Función para mostrar menú
show_menu() {
    echo ""
    echo "Selecciona los tests a ejecutar:"
    echo "1) Todos los tests (recomendado)"
    echo "2) Solo tests de USB"
    echo "3) Solo tests de Procesos"
    echo "4) Solo tests de Puertos"
    echo "5) Test individual"
    echo "6) Salir"
    echo ""
    read -p "Opción (1-6): " -n 1 -r
    echo
}

# Tests disponibles
declare -A USB_TESTS=(
    ["usb_tests/test_usb_insertion.sh"]="USB: Inserción de dispositivo"
    ["usb_tests/test_usb_file_modification.sh"]="USB: Modificación de archivo"
    ["usb_tests/test_usb_new_file.sh"]="USB: Archivo nuevo sospechoso"
    ["usb_tests/test_usb_file_deletion.sh"]="USB: Eliminación de archivo"
    ["usb_tests/test_usb_permissions.sh"]="USB: Cambio de permisos"
)

declare -A PROCESS_TESTS=(
    ["process_tests/test_legitimate_process.sh"]="Procesos: Proceso legítimo"
    ["process_tests/test_malicious_cpu.sh"]="Procesos: Uso excesivo de CPU"
    ["process_tests/test_memory_leak.sh"]="Procesos: Fuga de memoria"
    ["process_tests/test_process_cleanup.sh"]="Procesos: Limpieza de procesos"
)

declare -A PORT_TESTS=(
    ["port_tests/test_ssh_port.sh"]="Puertos: Puerto SSH legítimo"
    ["port_tests/test_suspicious_port.sh"]="Puertos: Puerto sospechoso"
    ["port_tests/test_high_port.sh"]="Puertos: Puerto alto HTTP"
    ["port_tests/test_closed_port.sh"]="Puertos: Escaneo de puertos cerrados"
)

# Función para ejecutar tests de una categoría
run_category_tests() {
    local -n tests_ref=$1
    local category_name="$2"
    
    print_status "Iniciando tests de $category_name..."
    local passed=0
    local total=0
    
    for script in "${!tests_ref[@]}"; do
        local test_name="${tests_ref[$script]}"
        ((total++))
        
        if run_test "$script" "$test_name"; then
            ((passed++))
        fi
    done
    
    echo ""
    echo "======================================"
    print_status "Resumen de $category_name:"
    print_success "Tests pasados: $passed/$total"
    echo "======================================"
}

# Función para mostrar tests individuales
show_individual_tests() {
    echo ""
    echo "Tests individuales disponibles:"
    echo ""
    
    local i=1
    local -A all_tests
    
    # Combinar todos los tests
    for script in "${!USB_TESTS[@]}"; do
        all_tests[$i]="$script|${USB_TESTS[$script]}"
        echo "$i) ${USB_TESTS[$script]}"
        ((i++))
    done
    
    for script in "${!PROCESS_TESTS[@]}"; do
        all_tests[$i]="$script|${PROCESS_TESTS[$script]}"
        echo "$i) ${PROCESS_TESTS[$script]}"
        ((i++))
    done
    
    for script in "${!PORT_TESTS[@]}"; do
        all_tests[$i]="$script|${PORT_TESTS[$script]}"
        echo "$i) ${PORT_TESTS[$script]}"
        ((i++))
    done
    
    echo ""
    read -p "Selecciona el número del test (1-$((i-1))): " -r
    
    if [[ $REPLY =~ ^[0-9]+$ ]] && [ "$REPLY" -ge 1 ] && [ "$REPLY" -lt "$i" ]; then
        local test_info="${all_tests[$REPLY]}"
        local script="${test_info%|*}"
        local name="${test_info#*|}"
        
        run_test "$script" "$name"
    else
        print_error "Selección inválida"
    fi
}

# Main execution
main() {
    # Verificar directorio de trabajo
    if [ ! -d "tests" ]; then
        print_error "Debe ejecutarse desde el directorio raíz del proyecto MatCom Guard"
        exit 1
    fi
    
    cd tests
    
    # Verificar MatCom Guard
    if ! check_matcom_guard; then
        exit 1
    fi
    
    print_success "Iniciando suite de tests..."
    print_warning "IMPORTANTE: Asegúrate de que MatCom Guard esté visible y funcionando"
    echo ""
    
    while true; do
        show_menu
        
        case $REPLY in
            1)
                print_status "Ejecutando TODOS los tests..."
                run_category_tests USB_TESTS "USB"
                run_category_tests PROCESS_TESTS "Procesos"
                run_category_tests PORT_TESTS "Puertos"
                
                echo ""
                print_success "¡Todos los tests completados!"
                print_status "Revisa MatCom Guard para verificar todas las alertas detectadas"
                break
                ;;
            2)
                run_category_tests USB_TESTS "USB"
                ;;
            3)
                run_category_tests PROCESS_TESTS "Procesos"
                ;;
            4)
                run_category_tests PORT_TESTS "Puertos"
                ;;
            5)
                show_individual_tests
                ;;
            6)
                print_status "Saliendo..."
                break
                ;;
            *)
                print_error "Opción inválida"
                ;;
        esac
    done
    
    echo ""
    print_status "Limpieza final..."
    
    # Limpiar procesos residuales
    pkill -f "test_usb_mount" 2>/dev/null
    pkill -f "nc -l" 2>/dev/null
    pkill -f "python3 -m http.server" 2>/dev/null
    
    # Limpiar archivos temporales
    rm -rf /tmp/test_usb_mount 2>/dev/null
    rm -f /tmp/http_server*.log 2>/dev/null
    rm -f /tmp/memory_leak.py 2>/dev/null
    rm -f /tmp/malicious_cpu.sh 2>/dev/null
    
    print_success "Limpieza completada"
    print_status "¡Gracias por usar la suite de tests de MatCom Guard!"
}

# Manejo de señales para limpieza
cleanup() {
    echo ""
    print_warning "Interrupción detectada, limpiando..."
    
    # Terminar procesos de test
    pkill -f "nc -l" 2>/dev/null
    pkill -f "python3 -m http.server" 2>/dev/null
    pkill -f "sleep 60" 2>/dev/null
    
    # Limpiar archivos temporales
    rm -rf /tmp/test_usb_mount 2>/dev/null
    rm -f /tmp/http_server*.log 2>/dev/null
    rm -f /tmp/memory_leak.py 2>/dev/null
    rm -f /tmp/malicious_cpu.sh 2>/dev/null
    
    print_status "Limpieza de emergencia completada"
    exit 1
}

trap cleanup SIGINT SIGTERM

# Ejecutar main
main "$@"
