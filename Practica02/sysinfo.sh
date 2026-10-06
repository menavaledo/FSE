#!/bin/bash

LOG="sysinfo_$(date +%Y%m%d_%H%M%S).log"

{
    echo "=== Reporte generado el $(date) ==="

    echo "--- Sistema ---"
    uname -a

    echo "--- CPU ---"
    lscpu | head -n 10

    echo "--- Memoria ---"
    free -h

    echo "--- Disco ---"
    df -h /

    echo "--- Procesos ---"
    ps aux --sort=-%cpu | head -n 6

} | tee "$LOG"

echo "Reporte guardado en: $LOG"
