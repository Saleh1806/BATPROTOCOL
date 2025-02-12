#!/usr/bin/env bash
# run_visualization.sh

# Répertoire de sortie (doit être le même que celui utilisé par la simulation)
OUTPUT_DIR="/tmp/expe-out"

# Vérifier que le répertoire de sortie existe
if [ ! -d "$OUTPUT_DIR" ]; then
    echo "Erreur : le répertoire de sortie '$OUTPUT_DIR' n'existe pas."
    exit 1
fi

# Chemins des fichiers CSV produits par EASY
JOBS_CSV="$OUTPUT_DIR/easyjobs.csv"
ENERGY_CSV="$OUTPUT_DIR/easyconsumed_energy.csv"
MSTATES_CSV="$OUTPUT_DIR/easymachine_states.csv"
PSTATES_CSV="$OUTPUT_DIR/easypstate_changes.csv"

# Vérifier l'existence des fichiers CSV obligatoires
for file in "$JOBS_CSV" "$ENERGY_CSV" "$PSTATES_CSV"; do
    if [ ! -f "$file" ]; then
        echo "Erreur : fichier '$file' introuvable."
        exit 1
    fi
done

# Préparer les arguments pour resource usage (RU) si le fichier MSTATES_CSV existe
RU_ARGS=""
if [ -f "$MSTATES_CSV" ]; then
    RU_ARGS="--ru --mstatesCSV $MSTATES_CSV"
else
    echo "Attention : le fichier easymachine_states.csv n'a pas été trouvé. Les graphiques de resource usage (RU) ne seront pas générés."
fi

echo "Lancement de la visualisation..."
./plot_energy_info.py \
  --gantt --power --energy --llh $RU_ARGS \
  --names easy_experiment \
  --off 13 --switchon 15 --switchoff 14 \
  --jobsCSV "$JOBS_CSV" \
  --energyCSV "$ENERGY_CSV" \
  --pstatesCSV "$PSTATES_CSV"

if [ $? -eq 0 ]; then
    echo "Visualisation terminée avec succès."
else
    echo "Erreur lors de la visualisation."
fi

exit $?

