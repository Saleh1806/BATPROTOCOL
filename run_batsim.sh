#!/bin/bash

# Vérifier si deux arguments sont passés
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <platform_filename> <workload_filename>"
    echo "Exemple : $0 cluster_energy_128.xml test_energy_minimal_load100.json"
    exit 1
fi

# Récupérer les arguments
PLATFORM_FILE="./platforms/$1"
WORKLOAD_FILE="./workloads/$2"

# Définir les variables nécessaires
LIBRARY_PATH="$EDC_LD_LIBRARY_PATH/libprobe-energy.so"
EXPE_OUT_DIR="/tmp/expe-out/out"
OUTPUT_CSV="energy_data.csv"  # Fichier généré par Batsim
PYTHON_SCRIPT="histogramme.py"

# Vérifier l'existence des fichiers
if [ ! -f "$PLATFORM_FILE" ]; then
    echo "Erreur : fichier plateforme '$PLATFORM_FILE' introuvable."
    exit 1
fi

if [ ! -f "$WORKLOAD_FILE" ]; then
    echo "Erreur : fichier workload '$WORKLOAD_FILE' introuvable."
    exit 1
fi

# Lancer Batsim avec les fichiers donnés
echo "Lancement de Batsim avec la plateforme '$PLATFORM_FILE' et le workload '$WORKLOAD_FILE'..."
batsim -p "$PLATFORM_FILE" \
       -w "$WORKLOAD_FILE" \
       -l "$LIBRARY_PATH" \
       1 '{"behavior": "wload", "inter_stop_probe_delay":0.5}' \
       -e "$EXPE_OUT_DIR" \
       --energy-host

# Vérifier si la commande batsim s'est bien exécutée
if [ $? -ne 0 ]; then
    echo "Erreur : l'exécution de Batsim a échoué."
    exit 1
fi

# Attendre que le fichier de sortie energy_data.csv soit généré
echo "Attente de la génération du fichier de données d'énergie..."
TIMEOUT=10  # Temps maximum d'attente en secondes
START_TIME=$(date +%s)

while [ ! -f "$OUTPUT_CSV" ]; do
    sleep 1
    CURRENT_TIME=$(date +%s)
    ELAPSED_TIME=$((CURRENT_TIME - START_TIME))
    if [ "$ELAPSED_TIME" -ge "$TIMEOUT" ]; then
        echo "Erreur : le fichier '$OUTPUT_CSV' n'a pas été généré après $TIMEOUT secondes."
        exit 1
    fi
done

echo "Fichier '$OUTPUT_CSV' détecté. Lancement de l'analyse..."

# Exécuter le script Python pour analyser les résultats
if [ -f "$PYTHON_SCRIPT" ]; then
    echo "Lancement de l'analyse avec le script Python..."
    python3 "$PYTHON_SCRIPT" "$OUTPUT_CSV"
else
    echo "Erreur : le script Python '$PYTHON_SCRIPT' est introuvable."
    exit 1
fi

echo "Exécution terminée avec succès."
