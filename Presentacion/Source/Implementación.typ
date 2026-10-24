#import "@preview/touying:0.6.1": *
#import themes.metropolis: *

#slide(title: "¿Cómo funciona por detrás?")[
  - Para la comunicación entre placas usamos *ESP-NOW*, un protocolo de Espressif que usa la radio WiFi.
  - Para el sonido, la placa crea su propia red WiFi (modo AP): el celular se conecta a ella y abre una página web que escucha por WebSocket los tonos del juego. *Sin app ni router*.
  - Todas las *funcionalidades* están en archivos separados dentro de la carpeta `main`.
]
