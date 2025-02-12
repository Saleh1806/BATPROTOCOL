#!/bin/bash

# Vérifier si deux arguments sont passés
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <platform_filename> <workload_filename>"
    echo "Exemple : $0 cluster_energy_128.xml test_workload.json"
    exit 1
fi

# Récupérer les arguments
PLATFORM_FILE="./platforms/$1"
WORKLOAD_FILE="./workloads/$2"

# Définir les variables nécessaires
LIBRARY_PATH="$EDC_LD_LIBRARY_PATH/libeasy.so"
EXPE_OUT_DIR="/tmp/expe-out/easy"

# Vérifier l'existence des fichiers
if [ ! -f "$PLATFORM_FILE" ]; then
    echo "Erreur : fichier plateforme '$PLATFORM_FILE' introuvable."
    exit 1
fi

if [ ! -f "$WORKLOAD_FILE" ]; then
    echo "Erreur : fichier workload '$WORKLOAD_FILE' introuvable."
    exit 1
fi

# Vérifier (et créer si nécessaire) le répertoire de sortie
if [ ! -d "$EXPE_OUT_DIR" ]; then
    mkdir -p "$EXPE_OUT_DIR" || { echo "Erreur : impossible de créer le répertoire $EXPE_OUT_DIR."; exit 1; }
fi

# Lancer Batsim avec l'option --energy-host (et sans -E)
echo "Lancement de Batsim avec la plateforme '$PLATFORM_FILE' et le workload '$WORKLOAD_FILE'..."
batsim -p "$PLATFORM_FILE" \
       -w "$WORKLOAD_FILE" \
       -l "$LIBRARY_PATH" \
       1 '{"behavior": "wload", "inter_stop_probe_delay":0.5}' \
       -e "$EXPE_OUT_DIR" \
       --mmax-workload \
       --energy-host

# Vérifier si la commande batsim s'est bien exécutée
if [ $? -ne 0 ]; then
    echo "Erreur : l'exécution de Batsim a échoué."
    exit 1
fi

echo "Exécution terminée avec succès."

