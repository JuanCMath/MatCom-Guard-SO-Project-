CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -Iinclude -DDEBUG
GTK_FLAGS = `pkg-config --cflags --libs gtk+-3.0`
CAIRO_FLAGS = `pkg-config --cflags --libs cairo cairo-pdf`
LIBS = -lcrypto -lpthread -ludev

TARGET = matcom-guard
SRC = src/main.c \
		src/port_scanner.c \
		src/threadpool.c \
		src/process_monitor.c \
		src/device_monitor.c \
		src/gui/gui_main.c \
		src/gui/window/gui_logging.c \
		src/gui/window/gui_stats.c \
		src/gui/window/gui_status.c \
		src/gui/window/gui_usb_panel.c \
		src/gui/window/gui_process_panel.c \
		src/gui/window/gui_ports_panel.c \
		src/gui/window/gui_config_dialog.c \
		src/gui/integration/gui_system_coordinator.c \
		src/gui/integration/gui_backend_adapters.c \
		src/gui/integration/gui_process_integration.c \
		src/gui/integration/gui_ports_integration.c \
		src/gui/integration/gui_usb_integration.c
		
.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(GTK_FLAGS) $(CAIRO_FLAGS) $(LIBS)

clean:
	rm -f $(TARGET) *.o

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

test: $(TARGET)
	./$(TARGET)

debug: $(SRC)
	$(CC) $(CFLAGS) -DDEBUG -o $(TARGET)_debug $(SRC) $(GTK_FLAGS) $(CAIRO_FLAGS) $(LIBS)

check-deps:
	@echo "Verificando dependencias..."
	@pkg-config --exists gtk+-3.0 && echo "✓ GTK+3 encontrado" || echo "✗ GTK+3 no encontrado"
	@ldconfig -p | grep -q libcrypto && echo "✓ OpenSSL encontrado" || echo "✗ OpenSSL no encontrado"
	@ldconfig -p | grep -q libudev && echo "✓ libudev encontrado" || echo "✗ libudev no encontrado"