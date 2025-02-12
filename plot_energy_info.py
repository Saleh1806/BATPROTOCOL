#!/usr/bin/env python3

import sys
import argparse

import seaborn
from evalys import *
from evalys.jobset import *
from evalys.pstates import *
from evalys.visu.legacy import *

import pandas as pd
import matplotlib
# Utiliser un backend interactif : TkAgg
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt

def main():
    # Argument parsing
    parser = argparse.ArgumentParser(description='Trace les indicateurs des jobs et de l\'énergie dans le temps')
    parser.add_argument('--jobsCSV', '-j', nargs='+',
                        help='Nom du fichier CSV contenant les informations sur les jobs')
    parser.add_argument('--pstatesCSV', '-p', nargs='+',
                        help='Nom du fichier CSV contenant les informations sur les pstates (pour le Gantt)')
    parser.add_argument('--energyCSV', '-e', nargs='+',
                        help='Nom du fichier CSV contenant les informations sur la consommation d\'énergie')
    parser.add_argument('--llhCSV', '-l', nargs='+',
                        help='Nom du fichier CSV contenant les informations LLH')

    parser.add_argument('--llh-bound',
                        type=float,
                        help='Si défini, trace une ligne horizontale pour ce seuil LLH')
    parser.add_argument('--priority-job-waiting-time-bound',
                        type=float,
                        help='Si défini, trace une ligne horizontale pour ce temps d\'attente limite')

    parser.add_argument('--time-window', nargs='+',
                        type=float,
                        help="Limite la fenêtre temporelle d'étude (exemple: 0 4200)")
    parser.add_argument('--force-right-adjust',
                        type=float,
                        help='Force l\'ajustement à droite du graphique.')

    parser.add_argument('--off', nargs='+',
                        help='États de puissance correspondant à OFF')
    parser.add_argument('--switchon', nargs='+',
                        help='États de puissance correspondant à l\'allumage')
    parser.add_argument('--switchoff', nargs='+',
                        help='États de puissance correspondant à l\'extinction')

    parser.add_argument('--names', nargs='+',
                         default=['Unnamed'],
                         help='Noms des instances à tracer')
    parser.add_argument('--output', '-o',
                        help='Fichier de sortie (ex: figure.pdf)')

    parser.add_argument("--gantt", action='store_true',
                        help="Active le tracé du diagramme de Gantt. Nécessite jobsCSV, et éventuellement pstatesCSV (--off, --switchon, --switchoff)")
    parser.add_argument("--power", action='store_true',
                        help="Active le tracé de la puissance instantanée. Nécessite energyCSV")
    parser.add_argument("--energy", action='store_true',
                        help="Active le tracé de l'énergie cumulée. Nécessite energyCSV")
    parser.add_argument('--llh', action='store_true',
                        help='Active le tracé du LLH. Nécessite llhCSV (optionnel, sinon le tracé LLH sera ignoré)')
    parser.add_argument('--load-in-queue', action='store_true',
                        help='Active le tracé de la charge en file (requiert llhCSV)')
    parser.add_argument('--nb-jobs-in-queue', action='store_true',
                        help='Active le tracé du nombre de jobs en file (requiert llhCSV)')
    parser.add_argument('--priority-job-size', action='store_true',
                        help='Active le tracé de la taille du job prioritaire (requiert llhCSV)')
    parser.add_argument('--priority-job-expected-waiting-time', action='store_true',
                        help='Active le tracé du temps d\'attente attendu du job prioritaire (requiert llhCSV)')
    parser.add_argument('--priority-job-starting-expected-soon', action='store_true',
                        help='Active le tracé indiquant si le job prioritaire est attendu prochainement (requiert llhCSV)')

    args = parser.parse_args()

    ###################
    # Création de la figure #
    ###################
    nb_instances = None
    nb_subplots = 0
    left_adjust = 0.05
    top_adjust = 0.95
    bottom_adjust = 0.05
    right_adjust = 0.95

    if args.gantt:
        assert(args.jobsCSV), "jobsCSV est requis pour tracer le Gantt !"
        nb_jobs_csv = len(args.jobsCSV)
        if args.pstatesCSV:
            nb_pstates_csv = len(args.pstatesCSV)
            assert(nb_jobs_csv == nb_pstates_csv), "Le nombre de jobsCSV ({}) doit être égal au nombre de pstatesCSV ({})".format(nb_jobs_csv, nb_pstates_csv)
        nb_gantt = nb_jobs_csv
        nb_subplots += nb_gantt
        nb_instances = nb_gantt

    if args.power:
        assert(args.energyCSV), "energyCSV est requis pour tracer la puissance !"
        nb_subplots += 1
        right_adjust = min(right_adjust, 0.85)

    if args.energy:
        assert(args.energyCSV), "energyCSV est requis pour tracer l'énergie !"
        nb_energy = 1
        nb_subplots += nb_energy
        right_adjust = min(right_adjust, 0.85)

    if args.energyCSV:
        nb_energy_csv = len(args.energyCSV)
        if nb_instances is not None:
            assert(nb_instances == nb_energy_csv), "Incohérence: energyCSV={} vs nb_instances={}".format(nb_energy_csv, nb_instances)
        else:
            nb_instances = nb_energy_csv

    if args.llh:
        if not args.llhCSV:
            print("Warning: le flag --llh est activé mais aucun fichier llhCSV n'est fourni. Le tracé LLH sera ignoré.")
            args.llh = False
        else:
            right_adjust = min(right_adjust, 0.85)
            nb_subplots += 1

    if args.load_in_queue:
        assert(args.llhCSV), "llhCSV est requis pour tracer la charge en file !"
        nb_subplots += 1

    if args.nb_jobs_in_queue:
        assert(args.llhCSV), "llhCSV est requis pour tracer le nombre de jobs en file !"
        nb_subplots += 1

    if args.priority_job_size:
        assert(args.llhCSV), "llhCSV est requis pour tracer la taille du job prioritaire !"
        nb_subplots += 1

    if args.priority_job_expected_waiting_time:
        assert(args.llhCSV), "llhCSV est requis pour tracer le temps d'attente attendu du job prioritaire !"
        nb_subplots += 1

    if args.priority_job_starting_expected_soon:
        assert(args.llhCSV), "llhCSV est requis pour tracer le démarrage attendu du job prioritaire !"
        nb_subplots += 1

    if args.llhCSV:
        nb_llh_csv = len(args.llhCSV)
        if nb_instances is not None:
            assert(nb_instances == nb_llh_csv), "Incohérence: llhCSV={} vs nb_instances={}".format(nb_llh_csv, nb_instances)
        else:
            nb_instances = nb_llh_csv

    if nb_subplots == 0:
        print("Il n'y a rien à tracer !")
        sys.exit(0)

    names = args.names
    assert(nb_instances == len(names)), "Le nombre de noms ({} dans {}) doit être égal au nombre d'instances ({})".format(len(names), names, nb_instances)

    if args.force_right_adjust:
        right_adjust = args.force_right_adjust

    fig, ax_list = plt.subplots(nb_subplots, sharex=True, sharey=False)
    fig.subplots_adjust(bottom=bottom_adjust, right=right_adjust, top=top_adjust, left=left_adjust)
    if nb_subplots < 2:
        ax_list = [ax_list]

    ##########################################
    # Création des structures de données #
    ##########################################
    time_min = None
    time_max = None
    if args.time_window:
        time_min, time_max = [float(f) for f in args.time_window]

    jobs = []
    if args.jobsCSV and (args.gantt or args.llh):
        for csv_filename in args.jobsCSV:
            jobs.append(JobSet.from_csv(csv_filename))

    pstates = []
    if args.pstatesCSV and args.gantt:
        for csv_filename in args.pstatesCSV:
            pstates.append(PowerStatesChanges(csv_filename))

    # La partie machine states a été supprimée

    llh = []
    if args.llh:
        for csv_filename in args.llhCSV:
            llh_data = pd.read_csv(csv_filename)
            if time_min is not None:
                llh_data = llh_data.loc[llh_data['date'] >= time_min]
            if time_max is not None:
                llh_data = llh_data.loc[llh_data['date'] <= time_max]
            llh.append(llh_data)

    energy = []
    power = []
    if args.energyCSV:
        for csv_filename in args.energyCSV:
            energy_data = pd.read_csv(csv_filename)
            if time_min is not None:
                energy_data = energy_data.loc[energy_data['time'] >= time_min]
            if time_max is not None:
                energy_data = energy_data.loc[energy_data['time'] <= time_max]
            energy.append(energy_data)
            if args.power:
                df = energy_data.drop_duplicates(subset='time')
                df = df.drop(['event_type', 'wattmin', 'epower'], axis=1)
                diff = df.diff(1)
                diff.rename(columns={'time':'time_diff', 'energy':'energy_diff'}, inplace=True)
                joined = pd.concat([df, diff], axis=1)
                joined['power'] = joined['energy_diff'] / joined['time_diff']
                power.append(joined)

    off_pstates = set()
    son_pstates = set()
    soff_pstates = set()
    if args.off:
        off_pstates = set(int(x) for x in args.off)
    if args.switchon:
        son_pstates = set(int(x) for x in args.switchon)
    if args.switchoff:
        soff_pstates = set(int(x) for x in args.switchoff)
    assert(off_pstates & son_pstates == set()), "Collision d'états: off et switchon"
    assert(off_pstates & soff_pstates == set()), "Collision d'états: off et switchoff"
    assert(son_pstates & soff_pstates == set()), "Collision d'états: switchon et switchoff"

    ############
    # Tracé     #
    ############
    ax_id = 0
    if args.gantt:
        for i, name in enumerate(names):
            if args.pstatesCSV:
                plot_gantt_pstates(jobs[i], pstates[i], ax_list[ax_id],
                                   title="Gantt chart: {}".format(name),
                                   labels=False,
                                   off_pstates=off_pstates,
                                   son_pstates=son_pstates,
                                   soff_pstates=soff_pstates)
            else:
                plot_gantt(jobs[i], ax=ax_list[ax_id],
                           title="Gantt chart: {}".format(name))
            ax_id += 1

    if args.power:
        for i, power_data in enumerate(power):
            ax_list[ax_id].plot(power_data['time'], power_data['power'],
                                label=names[i],
                                drawstyle='steps-pre')
        ax_list[ax_id].set_title('Power (W)')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.energy:
        for i, energy_data in enumerate(energy):
            ax_list[ax_id].plot(energy_data['time'], energy_data['energy'],
                                label=names[i])
        ax_list[ax_id].set_title('Energy (J)')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.llh:
        if args.llh_bound:
            min_x = float('inf')
            max_x = float('-inf')
        for i, llh_data in enumerate(llh):
            ax_list[ax_id].plot(llh_data['date'],
                                llh_data['liquid_load_horizon'],
                                label='{} LLH (s)'.format(names[i]))
            if args.jobsCSV:
                ax_list[ax_id].scatter(jobs[i].df['submission_time'],
                                       jobs[i].df['waiting_time'],
                                       label='{} Waiting Time (s)'.format(names[i]))
            if args.llh_bound:
                min_x = min(min_x, llh_data['date'].min())
                max_x = max(max_x, llh_data['date'].max())
        if args.llh_bound:
            llh_bound = args.llh_bound
            ax_list[ax_id].plot([min_x, max_x],
                                [llh_bound, llh_bound],
                                linestyle='-', linewidth=2,
                                label="LLH bound ({})".format(llh_bound))
        ax_list[ax_id].set_title('Unresponsiveness estimation')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.load_in_queue:
        for i, llh_data in enumerate(llh):
            ax_list[ax_id].plot(llh_data['date'],
                                llh_data['load_in_queue'],
                                label='{}'.format(names[i]),
                                drawstyle="steps-post")
        ax_list[ax_id].set_title('Load in queue (nb_res * seconds)')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.nb_jobs_in_queue:
        for i, llh_data in enumerate(llh):
            ax_list[ax_id].plot(llh_data['date'],
                                llh_data['nb_jobs_in_queue'],
                                label='{}'.format(names[i]),
                                drawstyle="steps-post")
        ax_list[ax_id].set_title('Number of jobs in queue')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.priority_job_size:
        for i, llh_data in enumerate(llh):
            ax_list[ax_id].plot(llh_data['date'],
                                llh_data['first_job_size'],
                                label='{}'.format(names[i]),
                                drawstyle="steps-post")
        ax_list[ax_id].set_title('Number of requested resources of the priority job')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.priority_job_expected_waiting_time:
        if args.priority_job_waiting_time_bound:
            min_x = float('inf')
            max_x = float('-inf')
        for i, llh_data in enumerate(llh):
            ax_list[ax_id].plot(llh_data['date'],
                                llh_data['priority_job_expected_waiting_time'],
                                label='{}'.format(names[i]),
                                drawstyle="steps-post")
            if args.priority_job_waiting_time_bound:
                min_x = min(min_x, llh_data['date'].min())
                max_x = max(max_x, llh_data['date'].max())
        if args.priority_job_waiting_time_bound:
            priority_job_waiting_time_bound = args.priority_job_waiting_time_bound
            ax_list[ax_id].plot([min_x, max_x],
                                [priority_job_waiting_time_bound, priority_job_waiting_time_bound],
                                linestyle='-', linewidth=2,
                                label="Bound ({})".format(priority_job_waiting_time_bound))
        ax_list[ax_id].set_title('Expected waiting time of the priority job (s)')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    if args.priority_job_starting_expected_soon:
        for i, llh_data in enumerate(llh):
            ax_list[ax_id].plot(llh_data['date'],
                                llh_data['priority_job_starting_expected_soon'],
                                label='{}'.format(names[i]),
                                drawstyle="steps-post")
        ax_list[ax_id].set_title('Is the priority job expected to start soon?')
        ax_list[ax_id].legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax_id += 1

    #####################
    # Figure outputting #
    #####################
    if args.output is not None:
        plt.savefig(args.output)
    else:
        plt.show()

if __name__ == "__main__":
    main()

