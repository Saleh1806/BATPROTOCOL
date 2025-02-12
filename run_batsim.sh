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
LIBRARY_PATH="$EDC_LD_LIBRARY_PATH/libstatistic-easy.so"
<<<<<<< Updated upstream
EXPE_OUT_DIR="/tmp/expe-out/out"
OUTPUT_CSV="energy_data.csv"  # Fichier généré par Batsim
PYTHON_SCRIPT="histogramme.py"
=======
EXPE_OUT_DIR="/tmp/expe-out/statistic"
>>>>>>> Stashed changes

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
       1 '{"behavior": "wload", "inter_stop_probe_delay":10}' \
       -e "$EXPE_OUT_DIR" \
       --mmax-workload \
       --energy-host

# Vérifier si la commande batsim s'est bien exécutée
if [ $? -ne 0 ]; then
    echo "Erreur : l'exécution de Batsim a échoué."
    exit 1
fi

echo "Exécution terminée avec succès."

