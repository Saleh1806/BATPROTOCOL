#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <limits>

// Headers Boost pour la gestion des intervalles
#include <boost/icl/interval.hpp>
#include <boost/icl/closed_interval.hpp>
#include <boost/icl/interval_bounds.hpp>

// Batsim / Protocol
#include <batprotocol.hpp>
#include <intervalset.hpp>
#include <nlohmann/json.hpp>
#include "batsim_edc.h"

using namespace batprotocol;
using json = nlohmann::json;

/* --------------------------------------------------------------------------
 * Structures de base : distributions de puissance et description d'une tâche
 * -------------------------------------------------------------------------- */

// Représente le comptage d’occurrences de puissances dans un intervalle
struct IntervalCounter {
    double start;
    double end;
    uint32_t count;
};

// Gère une distribution de puissance sous forme d’intervalles
struct PowerDistribution {
    std::vector<IntervalCounter> intervals;

    // Initialise la distribution : découpe [0, step * num_intervals] en tranches
    void initialize(double step, uint32_t num_intervals) {
        intervals.clear();
        intervals.reserve(num_intervals);
        for (uint32_t i = 0; i < num_intervals; ++i) {
            intervals.push_back({i * step, (i + 1) * step, 0});
        }
    }

    // Met à jour la distribution pour une nouvelle valeur de puissance
    void update(double value, const std::string &distribution_name) {
        double epsilon = 1e-6;
        bool matched = false;

        // Exemple de log très détaillé
        printf("Updating power distribution for value: %.6f in %s\n",
               value, distribution_name.c_str());

        for (auto &interval : intervals) {
            printf("  Checking interval [%.2f, %.2f)\n", interval.start, interval.end);
            if (value >= interval.start && value < (interval.end - epsilon)) {
                printf("  Matched interval [%.2f, %.2f) in %s\n",
                       interval.start, interval.end, distribution_name.c_str());
                interval.count++;
                matched = true;
                break;
            }
        }

        if (!matched) {
            printf("Value %.6f did not match any interval in %s.\n",
                   value, distribution_name.c_str());
        }
    }

    // Affiche la distribution
    void print(const std::string &title) const {
        printf("Power Distribution - %s:\n", title.c_str());
        for (const auto &interval : intervals) {
            printf("  [%.2f, %.2f): %u\n",
                   interval.start, interval.end, interval.count);
        }
    }

    // Calcule la puissance moyenne (pondérée par les occurrences dans chaque intervalle)
    double calculate_average_power() const {
        double total_weighted_power = 0.0;
        uint32_t total_counts = 0;
        for (const auto &interval : intervals) {
            double median_value = (interval.start + interval.end) / 2.0;
            total_weighted_power += median_value * interval.count;
            total_counts += interval.count;
        }
        return (total_counts == 0) ? 0.0 : (total_weighted_power / total_counts);
    }
};

// Représente une tâche à exécuter localement
struct LocalJob {
    std::string id;
    uint32_t nb_hosts;
    double walltime;

    // Allocation des hôtes (gérée via IntervalSet)
    IntervalSet alloc;
    double maximum_finish_time;

    // Distribution de puissance spécifique à la tâche
    PowerDistribution power_distribution;
};

/* --------------------------------------------------------------------------
 * Variables globales du scheduler
 * -------------------------------------------------------------------------- */

MessageBuilder *mb = nullptr;
bool format_binary = true;

uint32_t platform_nb_hosts = 0;
uint32_t nb_available_hosts = 0;
IntervalSet available_hosts;

std::list<LocalJob *> job_queue;
std::unordered_map<std::string, LocalJob *> running_jobs;

bool probes_running = false;
std::vector<PowerDistribution> per_host_distributions;
PowerDistribution task_power_distribution;

std::vector<double> host_energy;
std::vector<double> last_host_energy;
double all_hosts_energy = 0.0;
double last_call_time = -1;

double inter_stop_probe_delay = 0.0;
std::string behavior = "unset";

/* --------------------------------------------------------------------------
 * Fonctions utilitaires & ordonnancement
 * -------------------------------------------------------------------------- */

// Initialise la distribution de chaque hôte
void initialize_distributions(double interval_step, uint32_t num_intervals) {
    per_host_distributions.resize(platform_nb_hosts);
    for (auto &dist : per_host_distributions) {
        dist.initialize(interval_step, num_intervals);
    }
}

// Calcule la puissance moyenne consommée par tous les hôtes actuellement occupés
double calculate_running_hosts_average_power() {
    double total = 0.0;
    uint32_t active = 0;
    for (const auto& [job_id, job] : running_jobs) {
        if (job->alloc.is_empty()) continue;
        for (auto it = job->alloc.intervals_begin(); it != job->alloc.intervals_end(); ++it) {
            int lower = boost::icl::lower(*it);
            int upper = boost::icl::upper(*it);
            for (int host = lower; host <= upper; ++host) {
                total += per_host_distributions[host].calculate_average_power();
                active++;
            }
        }
    }
    return (active == 0) ? 0.0 : (total / active);
}

// Tri par temps de fin (ordre croissant) pour gérer le prochain job terminé
bool ascending_max_finish_time_job_order(const LocalJob *a, const LocalJob *b) {
    return a->maximum_finish_time < b->maximum_finish_time;
}

// Calcule la puissance moyenne attendue pour une tâche donnée en simulant l'allocation
double expected_host_power_for_job(const LocalJob *job) {
    if (nb_available_hosts < job->nb_hosts) {
        return std::numeric_limits<double>::max();
    }
    // On simule l’allocation sur les "job->nb_hosts" premiers hôtes libres
    IntervalSet alloc = available_hosts.left(job->nb_hosts);
    double sum = 0.0;
    uint32_t count = 0;

    for (auto it = alloc.intervals_begin(); it != alloc.intervals_end(); ++it) {
        int lower = boost::icl::lower(*it);
        int upper = boost::icl::upper(*it);
        for (int host = lower; host <= upper; ++host) {
            sum += per_host_distributions[host].calculate_average_power();
            count++;
        }
    }
    return (count == 0) ? std::numeric_limits<double>::max() : (sum / count);
}

// Comparateur pour trier les tâches par ordre croissant de puissance attendue
bool ascending_expected_host_power(const LocalJob *a, const LocalJob *b) {
    return expected_host_power_for_job(a) < expected_host_power_for_job(b);
}

/* --------------------------------------------------------------------------
 * Fonctions Batsim EDC (interface requise)
 * -------------------------------------------------------------------------- */

// Initialisation
uint8_t batsim_edc_init(const uint8_t *data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("Flags inconnus, impossible d'initialiser.\n");
        return 1;
    }
    mb = new MessageBuilder(!format_binary);

    std::string init_string(reinterpret_cast<const char *>(data), size);
    try {
        auto init_json = json::parse(init_string);
        behavior = init_json["behavior"];
        inter_stop_probe_delay = init_json["inter_stop_probe_delay"];
    } catch (const json::exception &e) {
        throw std::runtime_error("Erreur d'initialisation : " + std::string(e.what()));
    }
    return 0;
}

// Libération
uint8_t batsim_edc_deinit() {
    delete mb;
    mb = nullptr;

    // Libère les tâches en file d’attente
    for (auto *job : job_queue) {
        delete job;
    }
    job_queue.clear();

    // Libère les tâches en cours
    for (auto &entry : running_jobs) {
        delete entry.second;
    }
    running_jobs.clear();

    // Vider les vecteurs globaux
    host_energy.clear();
    last_host_energy.clear();
    per_host_distributions.clear();
    return 0;
}

// Point d’entrée principal : prise de décisions
uint8_t batsim_edc_take_decisions(
    const uint8_t *what_happened,
    uint32_t what_happened_size,
    uint8_t **decisions,
    uint32_t *decisions_size
) {
    auto *parsed = deserialize_message(*mb, !format_binary, what_happened);
    mb->clear(parsed->now());
    (void)what_happened_size;

    bool need_scheduling = false;
    auto nb_events = parsed->events()->size();

    // Parcours des événements
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto event = (*parsed->events())[i];
        switch (event->event_type()) {
        case fb::Event_BatsimHelloEvent:
            // Répond que l'EDC est prêt
            mb->add_edc_hello("advanced-scheduler", "1.0.0");
            break;

        case fb::Event_SimulationBeginsEvent: {
            // Récupération des infos de plateforme
            auto simu = event->event_as_SimulationBeginsEvent();
            platform_nb_hosts = simu->computation_host_number();
            nb_available_hosts = platform_nb_hosts;
            available_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);

            // Prépare les vecteurs d'énergie
            host_energy.resize(platform_nb_hosts, 0.0);
            last_host_energy.resize(platform_nb_hosts, 0.0);

            // Initialise les distributions
            initialize_distributions(100.0, 30);
            task_power_distribution.initialize(100.0, 30);

            // Création des sondes (probes)
            IntervalSet all_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
            auto when = TemporalTrigger::make_periodic(1);
            auto cp = CreateProbe::make_temporal_triggerred(when);
            cp->set_resources_as_hosts(all_hosts.to_string_hyphen());
            cp->enable_accumulation_no_reset();

            // Sondes vectorielle et agrégée
            mb->add_create_probe("hosts-vec", fb::Metrics_Power, cp);
            cp->set_resource_aggregation_as_sum();
            mb->add_create_probe("hosts-agg", fb::Metrics_Power, cp);

            probes_running = true;
            break;
        }

        case fb::Event_ProbeDataEmittedEvent: {
            // Mise à jour des distributions via les données de sondes
            auto e = event->event_as_ProbeDataEmittedEvent();
            static bool proc_vec = false, proc_agg = false;

            if (e->probe_id()->str() == "hosts-vec") {
                auto data = e->data_as_VectorialProbeData()->data();
                for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                    double diff = data->Get(i) - last_host_energy[i];
                    last_host_energy[i] = data->Get(i);
                    host_energy[i] += diff;

                    // Calcul de la puissance sur l’intervalle
                    double host_power = diff / (event->timestamp() - last_call_time);
                    per_host_distributions[i].update(host_power,
                        "Host " + std::to_string(i) + " Power Distribution");
                }
                proc_vec = true;
            }

            if (e->probe_id()->str() == "hosts-agg") {
                double prev = all_hosts_energy;
                all_hosts_energy = e->data_as_AggregatedProbeData()->data();

                double total_power = (all_hosts_energy - prev) /
                                     (event->timestamp() - last_call_time);

                // Mise à jour de la distribution globale
                task_power_distribution.update(total_power, "Task Power Distribution");

                // Répartition simple de la puissance entre les tâches en cours
                if (!running_jobs.empty()) {
                    double per_job_power = total_power / running_jobs.size();
                    for (auto &[job_id, job] : running_jobs) {
                        job->power_distribution.update(per_job_power, "Task " + job_id);
                    }
                }
                proc_agg = true;
            }

            // Actualise le marqueur de temps dès que toutes les sondes sont traitées
            if (proc_vec && proc_agg) {
                last_call_time = event->timestamp();
                proc_vec = proc_agg = false;
            }
            break;
        }

        case fb::Event_JobSubmittedEvent: {
            // Nouvelle tâche soumise
            auto parsed_job = event->event_as_JobSubmittedEvent();
            LocalJob *job = new LocalJob{
                parsed_job->job_id()->str(),
                parsed_job->job()->resource_request(),
                parsed_job->job()->walltime(),
                IntervalSet::empty_interval_set(),
                -1,
                {}
            };

            // Initialise la distribution spécifique à la tâche
            job->power_distribution.initialize(100.0, 20);

            // Valide la requête
            if (job->nb_hosts > platform_nb_hosts || job->walltime <= 0) {
                mb->add_reject_job(job->id);
                delete job;
            } else {
                job_queue.push_back(job);
                need_scheduling = true;
            }
            break;
        }

        case fb::Event_JobCompletedEvent: {
            // Libère les ressources quand une tâche se termine
            auto job_id = event->event_as_JobCompletedEvent()->job_id()->str();
            auto it = running_jobs.find(job_id);
            if (it != running_jobs.end()) {
                LocalJob *job = it->second;
                nb_available_hosts += job->nb_hosts;
                available_hosts += job->alloc;

                // Affiche la distribution de puissance finale pour la tâche
                job->power_distribution.print("Final Power Distribution for Task " + job_id);

                delete job;
                running_jobs.erase(it);
                need_scheduling = true;
            }
            break;
        }

        default:
            // Autres événements ignorés
            break;
        }
    }

    /* ----------------------------------------------------------------------
     * Ordonnancement des tâches si nécessaire
     * ---------------------------------------------------------------------- */
    if (need_scheduling) {
        // Tri de la file d’attente par puissance attendue
        job_queue.sort(ascending_expected_host_power);

        printf("\nOrdre proposé d'ordonnancement (par puissance attendue) :\n");
        for (auto &job : job_queue) {
            double exp = expected_host_power_for_job(job);
            printf("  Tâche %s - Puissance attendue : %.2f, Nb Hosts : %u, Walltime : %.2f\n",
                   job->id.c_str(), exp, job->nb_hosts, job->walltime);
        }

        // On stocke l’ordre effectif d’exécution à des fins d’affichage
        std::vector<std::string> execution_order;

        // Première passe : exécution immédiate
        auto it_job = job_queue.begin();
        while (it_job != job_queue.end()) {
            LocalJob *job = *it_job;
            if (job->nb_hosts <= nb_available_hosts) {
                // Assez de ressources pour l’exécuter
                running_jobs[job->id] = job;
                job->maximum_finish_time = parsed->now() + job->walltime;
                job->alloc = available_hosts.left(job->nb_hosts);

                mb->add_execute_job(job->id, job->alloc.to_string_hyphen());
                available_hosts -= job->alloc;
                nb_available_hosts -= job->nb_hosts;

                execution_order.push_back(job->id);
                printf("La tâche %s sera exécutée immédiatement.\n", job->id.c_str());

                it_job = job_queue.erase(it_job);
            } else {
                // Pas assez de ressources, on considère cette tâche « prioritaire »
                printf("La tâche %s est mise en attente (prioritaire).\n", job->id.c_str());
                ++it_job;
                break;  // on ne tente pas d’allouer plus loin
            }
        }

        // Backfill : on essaie d’exécuter les tâches suivantes si possible
        while (it_job != job_queue.end() && nb_available_hosts > 0) {
            LocalJob *job = *it_job;
            if (job->nb_hosts <= nb_available_hosts) {
                running_jobs[job->id] = job;
                job->maximum_finish_time = parsed->now() + job->walltime;
                job->alloc = available_hosts.left(job->nb_hosts);

                mb->add_execute_job(job->id, job->alloc.to_string_hyphen());
                available_hosts -= job->alloc;
                nb_available_hosts -= job->nb_hosts;

                execution_order.push_back(job->id);
                printf("La tâche %s sera backfillée.\n", job->id.c_str());

                it_job = job_queue.erase(it_job);
            } else {
                ++it_job;
            }
        }

        // Affichage final de l’ordre effectif
        printf("\nOrdre effectif d'exécution des tâches :\n");
        for (size_t i = 0; i < execution_order.size(); ++i) {
            printf("  %zu. Tâche %s\n", i + 1, execution_order[i].c_str());
        }
    }

    // Si tout est vide et qu’on avait des sondes, on les arrête
    if (probes_running && job_queue.empty() && running_jobs.empty()) {
        mb->add_stop_probe("hosts-vec");
        mb->add_stop_probe("hosts-agg");
        probes_running = false;
    }

    // Finalisation du message
    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary,
                      const_cast<const uint8_t **>(decisions),
                      decisions_size);

    // Exemple d’affichage succinct des puissances par hôte
    printf("\nConsommation moyenne par hôte (jusqu'à présent) :\n");
    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
        double avg = per_host_distributions[i].calculate_average_power();
        printf("  Hôte %u : %.2f\n", i, avg);
    }

    // Distribution globale
    task_power_distribution.print("Global Task Power Distribution");
    return 0;
}
