import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

# Nom du fichier d'entrée
input_filename = 'energy_data.csv'

# Lire et traiter les données
try:
    # Lire le fichier en tant que texte brut
    with open(input_filename, 'r') as file:
        lines = file.readlines()

    # Extraire les valeurs d'énergie après "Event: ProbeDataEmittedEvent"
    energy_values = []
    recording = False
    for line in lines:
        line = line.strip()
        if line.startswith("Event: ProbeDataEmittedEvent"):
            recording = True  # Détecter le début des données pertinentes
        elif recording:
            # Vérifier si la ligne contient un nombre
            if line.replace('.', '', 1).isdigit():
                value = float(line)
                if value != 0:  # Exclure les valeurs égales à zéro
                    energy_values.append(value)
            elif not line:  # Arrêter l'enregistrement si une ligne vide est rencontrée
                recording = False

    # Convertir les valeurs en DataFrame pour faciliter l'analyse
    df = pd.DataFrame(energy_values, columns=['Energy'])

    # Définir les limites des barres (bins) pour une échelle précise
    unique_values = sorted(set(energy_values))  # Obtenir les valeurs uniques triées
    if len(unique_values) > 1:
        bin_edges = np.linspace(min(unique_values), max(unique_values), len(unique_values) + 1)
    else:
        bin_edges = [min(unique_values) - 1, max(unique_values) + 1]

    # Tracer l'histogramme avec des barres personnalisées
    plt.figure(figsize=(10, 6))
    plt.hist(df['Energy'], bins=bin_edges, color='blue', edgecolor='black', alpha=0.7)
    plt.title("Distribution des valeurs d'énergie consommées (sans zéros)", fontsize=16)
    plt.xlabel("Valeurs d'énergie (Joules)", fontsize=14)
    plt.ylabel("Fréquence", fontsize=14)
    plt.xticks(bin_edges, rotation=45, fontsize=10)  # Afficher les valeurs exactes sur l'axe des abscisses
    plt.grid(axis='y', linestyle='--', alpha=0.7)

    # Afficher l'histogramme
    plt.tight_layout()
    plt.show()

except FileNotFoundError:
    print(f"Erreur : le fichier '{input_filename}' est introuvable.")
except Exception as e:
    print(f"Erreur : {e}")
