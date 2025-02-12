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

#include <batprotocol.hpp>
#include <intervalset.hpp>
#include <nlohmann/json.hpp>
#include "batsim_edc.h"
using namespace batprotocol;
using json = nlohmann::json;

<<<<<<< Updated upstream
=======
// --- Structures de base ---
>>>>>>> Stashed changes

// Structure de comptage pour une plage de puissance
struct IntervalCounter {
    double start;
    double end;
    uint32_t count;
};

// Structure de distribution de puissance
struct PowerDistribution {
    std::vector<IntervalCounter> intervals;

<<<<<<< Updated upstream
=======
    // Initialisation de la distribution en découpant l'intervalle de puissance en plusieurs sous-intervalles
>>>>>>> Stashed changes
    void initialize(double step, uint32_t num_intervals) {
        intervals.clear();
        for (uint32_t i = 0; i < num_intervals; ++i) {
            intervals.push_back({i * step, (i + 1) * step, 0});
        }
    }

<<<<<<< Updated upstream
    void update(double value, const std::string &distribution_name) {
        double epsilon = 1e-6;
        bool matched = false;

        printf("Updating power distribution for value: %.6f in %s\n", value, distribution_name.c_str());
        for (auto &interval : intervals) {
            printf("Checking interval [%.2f, %.2f)\n", interval.start, interval.end);
            if (value >= interval.start && value < (interval.end - epsilon)) {
                printf("Matched interval [%.2f, %.2f) in %s\n", interval.start, interval.end, distribution_name.c_str());
                interval.count++;
                matched = true;
=======
    // Mise à jour de la distribution pour une valeur donnée (sans affichage détaillé ici)
    void update(double value, const std::string & /*unused*/) {
        double epsilon = 1e-6;
        for (auto &interval : intervals) {
            if (value >= interval.start && value < (interval.end - epsilon)) {
                interval.count++;
>>>>>>> Stashed changes
                break;
            }
        }

        if (!matched) {
            printf("Value %.6f did not match any interval in %s.\n", value, distribution_name.c_str());
        }
    }

<<<<<<< Updated upstream
    void print(const std::string &title) const {
        printf("Power Distribution - %s:\n", title.c_str());
=======
    // Affichage succinct de la distribution
    void print(const std::string &title) const {
        printf("%s:\n", title.c_str());
>>>>>>> Stashed changes
        for (const auto &interval : intervals) {
            printf("  [%.2f, %.2f): %u\n", interval.start, interval.end, interval.count);
        }
    }
    
<<<<<<< Updated upstream
    double calculate_average_power() const {
        double total_weighted_power = 0.0;
        uint32_t total_counts = 0;

        for (const auto &interval : intervals) {
            double median_value = (interval.start + interval.end) / 2.0;
            total_weighted_power += median_value * interval.count;
            total_counts += interval.count;
        }

        if (total_counts == 0) {
            return 0.0;
        }

        return total_weighted_power / total_counts;
    }
};
struct LocalJob {
    std::string id;
    uint32_t nb_hosts;
    double walltime;

    IntervalSet alloc;
    double maximum_finish_time;
    PowerDistribution power_distribution; // Distribution de puissance spécifique à chaque tâche
};
=======
    // Calcul de la puissance moyenne sur cette distribution
    double calculate_average_power() const {
        double total = 0.0;
        uint32_t count = 0;
        for (const auto &interval : intervals) {
            double median = (interval.start + interval.end) / 2.0;
            total += median * interval.count;
            count += interval.count;
        }
        return (count == 0) ? 0.0 : (total / count);
    }
};

// Structure représentant une tâche (job) locale à ordonnancer
struct LocalJob {
    std::string id;
    uint32_t nb_hosts;
    double walltime;
    IntervalSet alloc;  // Allocation des hôtes
    double maximum_finish_time;
    PowerDistribution power_distribution; // Distribution de puissance propre à la tâche
};

// --- Variables globales du scheduler ---
>>>>>>> Stashed changes
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
double epsilon = 1e-3;

double min_power = 95.0;
double max_power = 190.738;
double inter_stop_probe_delay = 0.0;
std::string behavior = "unset";

<<<<<<< Updated upstream
bool ascending_max_finish_time_job_order(const LocalJob *a, const LocalJob *b) {
    return a->maximum_finish_time < b->maximum_finish_time;
}
bool ascending_power_consumption_job_order(const LocalJob *a, const LocalJob *b) {
    double power_a = a->power_distribution.calculate_average_power();
    double power_b = b->power_distribution.calculate_average_power();
    return power_a < power_b;
}
=======
// --- Fonctions d'ordonnancement basées sur la puissance moyenne des hôtes ---

// Calcule la puissance moyenne attendue sur les hôtes pour un job en simulant son allocation
double expected_host_power_for_job(const LocalJob *job) {
    if (nb_available_hosts < job->nb_hosts) {
        return std::numeric_limits<double>::max();
    }
    // Simulation : récupérer une allocation virtuelle depuis available_hosts
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

// Comparateur pour trier les jobs par ordre croissant de puissance moyenne attendue
bool ascending_expected_host_power(const LocalJob *a, const LocalJob *b) {
    return expected_host_power_for_job(a) < expected_host_power_for_job(b);
}

// --- Fonctions utilitaires ---
>>>>>>> Stashed changes

// Tri par temps de fin croissant (pour déterminer le prochain job terminé)
bool ascending_max_finish_time_job_order(const LocalJob *a, const LocalJob *b) {
    return a->maximum_finish_time < b->maximum_finish_time;
}

// Initialise la distribution de puissance pour chaque hôte
void initialize_distributions(double interval_step, uint32_t num_intervals) {
    per_host_distributions.resize(platform_nb_hosts);
    for (auto &distribution : per_host_distributions) {
        distribution.initialize(interval_step, num_intervals);
    }
}
double calculate_running_hosts_average_power() {
    double total_power = 0.0;
    uint32_t active_hosts = 0;

    for (const auto& [job_id, job] : running_jobs) {
        
        std::string alloc_str = job->alloc.to_string_hyphen();
        std::vector<std::string> intervals;
        std::stringstream ss(alloc_str);
        std::string interval;

        // Split the string into individual intervals using commas
        while (std::getline(ss, interval, ',')) {
            intervals.push_back(interval);
        }

<<<<<<< Updated upstream
        // Process each interval to extract start and end hosts
        for (const auto& interval_str : intervals) {
            size_t hyphen_pos = interval_str.find('-');
            if (hyphen_pos != std::string::npos) {
                uint32_t start = std::stoul(interval_str.substr(0, hyphen_pos));
                uint32_t end = std::stoul(interval_str.substr(hyphen_pos + 1));

                // Iterate through each host in the interval
                for (uint32_t host = start; host <= end; ++host) {
                    total_power += per_host_distributions[host].calculate_average_power();
                    active_hosts++;
                }
            }
        }
    }

    return (active_hosts == 0) ? 0.0 : (total_power / active_hosts);
}
uint8_t batsim_edc_init(const uint8_t *data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("Unknown flags used, cannot initialize myself.\n");
=======
// Calcule la puissance moyenne consommée par les hôtes exécutant des jobs en cours
double calculate_running_hosts_average_power() {
    double total = 0.0;
    uint32_t active = 0;
    for (const auto& [job_id, job] : running_jobs) {
        if (job->alloc.is_empty())
            continue;
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

// --- Fonctions d'initialisation et de déinitialisation ---

uint8_t batsim_edc_init(const uint8_t *data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("Flags inconnus, impossible d'initialiser.\n");
>>>>>>> Stashed changes
        return 1;
    }
    mb = new MessageBuilder(!format_binary);
<<<<<<< Updated upstream

    std::string init_string((const char *)data, static_cast<size_t>(size));
=======
    std::string init_string(reinterpret_cast<const char *>(data), size);
>>>>>>> Stashed changes
    try {
        auto init_json = json::parse(init_string);
        behavior = init_json["behavior"];
        inter_stop_probe_delay = init_json["inter_stop_probe_delay"];
    } catch (const json::exception &e) {
<<<<<<< Updated upstream
        throw std::runtime_error("scheduler called with bad init string: " + std::string(e.what()));
    }

=======
        throw std::runtime_error("Erreur d'initialisation : " + std::string(e.what()));
    }
>>>>>>> Stashed changes
    return 0;
}

uint8_t batsim_edc_deinit() {
    delete mb;
    mb = nullptr;
<<<<<<< Updated upstream

    for (auto *job : job_queue) {
        delete job;
    }
    job_queue.clear();

    for (auto &entry : running_jobs) {
        delete entry.second;
    }
    running_jobs.clear();

    host_energy.clear();
    last_host_energy.clear();
    per_host_distributions.clear();

=======
    for (auto *job : job_queue)
        delete job;
    job_queue.clear();
    for (auto &entry : running_jobs)
        delete entry.second;
    running_jobs.clear();
    host_energy.clear();
    last_host_energy.clear();
    per_host_distributions.clear();
>>>>>>> Stashed changes
    return 0;
}

// --- Fonction principale de prise de décisions ---
//
// Cette fonction réalise les étapes suivantes :
// 1. Traitement des événements (simulation, soumission, complétion de tâches)
// 2. Calcul de la puissance attendue pour chaque tâche et tri de la file d'attente
// 3. Ordonnancement effectif (exécution immédiate ou backfill) en fonction des ressources disponibles
// 4. Affichage de l'ordre effectif d'exécution des tâches
//
uint8_t batsim_edc_take_decisions(
    const uint8_t *what_happened,
    uint32_t what_happened_size,
    uint8_t **decisions,
    uint32_t *decisions_size) {

    auto *parsed = deserialize_message(*mb, !format_binary, what_happened);
    mb->clear(parsed->now());
    (void)what_happened_size;

<<<<<<< Updated upstream
    (void)what_happened_size;

=======
>>>>>>> Stashed changes
    bool need_scheduling = false;

    // --- Traitement des événements entrants ---
    auto nb_events = parsed->events()->size();
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto event = (*parsed->events())[i];
        switch (event->event_type()) {
<<<<<<< Updated upstream
            case fb::Event_BatsimHelloEvent: {
                mb->add_edc_hello("advanced-scheduler", "1.0.0");
            } break;

            case fb::Event_SimulationBeginsEvent: {
    		auto simu_begins = event->event_as_SimulationBeginsEvent();
    		platform_nb_hosts = simu_begins->computation_host_number();
    		nb_available_hosts = platform_nb_hosts;
    		available_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);

   		 host_energy.resize(platform_nb_hosts, 0.0);
    		last_host_energy.resize(platform_nb_hosts, 0.0);

    		initialize_distributions(100.0, 30);

    		// Initialisez la distribution de puissance pour toutes les tâches
    		task_power_distribution.initialize(100.0, 30);

    		IntervalSet all_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
    		auto when = TemporalTrigger::make_periodic(1);
    		auto cp = CreateProbe::make_temporal_triggerred(when);
    		cp->set_resources_as_hosts(all_hosts.to_string_hyphen());
    		cp->enable_accumulation_no_reset();
    		mb->add_create_probe("hosts-vec", fb::Metrics_Power, cp);

    		cp->set_resource_aggregation_as_sum();
    		mb->add_create_probe("hosts-agg", fb::Metrics_Power, cp);

    		probes_running = true;
		} break;

           case fb::Event_ProbeDataEmittedEvent: {
                auto e = event->event_as_ProbeDataEmittedEvent();
                static bool has_processed_vec = false, has_processed_agg = false;

                if (e->probe_id()->str() == "hosts-vec") {
                    auto data = e->data_as_VectorialProbeData()->data();
                    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                        double host_energy_diff = data->Get(i) - last_host_energy[i];
                        last_host_energy[i] = data->Get(i);
                        host_energy[i] += host_energy_diff;

                        double host_power = host_energy_diff / (event->timestamp() - last_call_time);
                        per_host_distributions[i].update(host_power, "Host " + std::to_string(i) + " Power Distribution");
                    }
                    has_processed_vec = true;
                }

                if (e->probe_id()->str() == "hosts-agg") {
                    double prev_all_hosts_energy = all_hosts_energy;
                    all_hosts_energy = e->data_as_AggregatedProbeData()->data();

                    double total_power = (all_hosts_energy - prev_all_hosts_energy) / (event->timestamp() - last_call_time);
                    task_power_distribution.update(total_power, "Task Power Distribution");

                    for (auto &[job_id, job] : running_jobs) {
                        double job_power = total_power / running_jobs.size();
                        job->power_distribution.update(job_power, "Task " + job_id);
                    }

                    has_processed_agg = true;
                }

                if (has_processed_vec && has_processed_agg) {
                    last_call_time = event->timestamp();
                    has_processed_vec = has_processed_agg = false;
                }
            } break;


            case fb::Event_JobSubmittedEvent: {
                auto parsed_job = event->event_as_JobSubmittedEvent();
                LocalJob *job = new LocalJob{
                    parsed_job->job_id()->str(),
                    parsed_job->job()->resource_request(),
                    parsed_job->job()->walltime(),
                    IntervalSet::empty_interval_set(),
                    -1,
                    {}
                };

                job->power_distribution.initialize(100.0, 20); // Initialisation de la distribution de puissance par tâche

                if (job->nb_hosts > platform_nb_hosts || job->walltime <= 0) {
                    mb->add_reject_job(job->id);
                    delete job;
                } else {
                    job_queue.push_back(job);
                    need_scheduling = true;
                }
            } break;

            case fb::Event_JobCompletedEvent: {
                auto job_id = event->event_as_JobCompletedEvent()->job_id()->str();
                auto it = running_jobs.find(job_id);
                if (it != running_jobs.end()) {
                    LocalJob *job = it->second;
                    nb_available_hosts += job->nb_hosts;
                    available_hosts += job->alloc;

                    job->power_distribution.print("Final Power Distribution for Task " + job_id);

                    delete job;
                    running_jobs.erase(it);
                    need_scheduling = true;
                }
            } break;

            default: break;
        }
    }

  if (need_scheduling) {
    // Calculer la puissance moyenne pour chaque tâche dans la file d'attente
    for (auto &job : job_queue) {
        double average_power = job->power_distribution.calculate_average_power();
        printf("Task %s has an average power of %.2f\n", job->id.c_str(), average_power);
    }

    // Trier la file d'attente en fonction de la puissance moyenne des tâches
    job_queue.sort(ascending_power_consumption_job_order);

    // Variables pour gérer la tâche prioritaire
    LocalJob *priority_job = nullptr;
    uint32_t nb_available_hosts_at_priority_job_start = 0;
    float priority_job_start_time = -1;

    // Parcourir la file d'attente triée pour allouer les ressources
    auto job_it = job_queue.begin();
    for (; job_it != job_queue.end();) {
        auto job = *job_it;

        // Vérifier si les ressources disponibles sont suffisantes pour la tâche
        if (job->nb_hosts <= nb_available_hosts) {
            // Allouer les ressources à la tâche
            running_jobs[job->id] = *job_it;
            job->maximum_finish_time = parsed->now() + job->walltime;
            job->alloc = available_hosts.left(job->nb_hosts);
            mb->add_execute_job(job->id, job->alloc.to_string_hyphen());

            // Mettre à jour les ressources disponibles
            available_hosts -= job->alloc;
            nb_available_hosts -= job->nb_hosts;

            // Supprimer la tâche de la file d'attente
            job_it = job_queue.erase(job_it);
        } else {
            // Si la tâche ne peut pas être exécutée maintenant, elle devient une tâche prioritaire
            priority_job = *job_it;
            ++job_it;

            // Calculer le temps de démarrage de la tâche prioritaire
            std::vector<LocalJob *> running_jobs_asc_maximum_finish_time;
            running_jobs_asc_maximum_finish_time.reserve(running_jobs.size());
            for (const auto &it : running_jobs)
                running_jobs_asc_maximum_finish_time.push_back(it.second);

            // Trier les tâches en cours d'exécution par ordre croissant de temps de fin
            std::sort(running_jobs_asc_maximum_finish_time.begin(), running_jobs_asc_maximum_finish_time.end(), ascending_max_finish_time_job_order);

            // Calculer les ressources disponibles au moment où la tâche prioritaire pourra démarrer
            nb_available_hosts_at_priority_job_start = nb_available_hosts;
            for (const auto &job : running_jobs_asc_maximum_finish_time) {
                nb_available_hosts_at_priority_job_start += job->nb_hosts;
                if (nb_available_hosts_at_priority_job_start >= priority_job->nb_hosts) {
                    nb_available_hosts_at_priority_job_start -= priority_job->nb_hosts;
                    priority_job_start_time = job->maximum_finish_time;
                    break;
                }
            }

            break;
        }
    }

    // Gestion des tâches backfill
    for (; job_it != job_queue.end(); ) {
        auto job = *job_it;

        // Vérifier si la tâche peut être backfillée
        float job_finish_time = parsed->now() + job->walltime;
        if (job->nb_hosts <= nb_available_hosts && // assez de ressources maintenant ?
            (job->nb_hosts <= nb_available_hosts_at_priority_job_start || job_finish_time <= priority_job_start_time)) {  // ne retarde pas la tâche prioritaire ?
            // Allouer les ressources à la tâche backfill
            running_jobs[job->id] = *job_it;
            job->maximum_finish_time = job_finish_time;
            job->alloc = available_hosts.left(job->nb_hosts);
            mb->add_execute_job(job->id, job->alloc.to_string_hyphen());

            // Mettre à jour les ressources disponibles
            available_hosts -= job->alloc;
            nb_available_hosts -= job->nb_hosts;

            // Si la tâche backfill termine après le démarrage de la tâche prioritaire, ajuster les ressources disponibles
            if (job_finish_time > priority_job_start_time)
                nb_available_hosts_at_priority_job_start -= job->nb_hosts;

            // Supprimer la tâche de la file d'attente
            job_it = job_queue.erase(job_it);
        }
        else if (nb_available_hosts == 0) {
            // Si aucune ressource n'est disponible, arrêter le backfill
            break;
        }
        else {
            // Passer à la tâche suivante dans la file d'attente
            ++job_it;
=======
            case fb::Event_BatsimHelloEvent:
                mb->add_edc_hello("advanced-scheduler", "1.0.0");
                break;
            case fb::Event_SimulationBeginsEvent: {
                auto simu = event->event_as_SimulationBeginsEvent();
                platform_nb_hosts = simu->computation_host_number();
                nb_available_hosts = platform_nb_hosts;
                available_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
                host_energy.resize(platform_nb_hosts, 0.0);
                last_host_energy.resize(platform_nb_hosts, 0.0);
                initialize_distributions(100.0, 30);
                task_power_distribution.initialize(100.0, 30);
                IntervalSet all_hosts = IntervalSet::ClosedInterval(0, platform_nb_hosts - 1);
                auto when = TemporalTrigger::make_periodic(1);
                auto cp = CreateProbe::make_temporal_triggerred(when);
                cp->set_resources_as_hosts(all_hosts.to_string_hyphen());
                cp->enable_accumulation_no_reset();
                mb->add_create_probe("hosts-vec", fb::Metrics_Power, cp);
                cp->set_resource_aggregation_as_sum();
                mb->add_create_probe("hosts-agg", fb::Metrics_Power, cp);
                probes_running = true;
                break;
            }
            case fb::Event_ProbeDataEmittedEvent: {
                auto e = event->event_as_ProbeDataEmittedEvent();
                static bool proc_vec = false, proc_agg = false;
                if (e->probe_id()->str() == "hosts-vec") {
                    auto data = e->data_as_VectorialProbeData()->data();
                    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
                        double diff = data->Get(i) - last_host_energy[i];
                        last_host_energy[i] = data->Get(i);
                        host_energy[i] += diff;
                        per_host_distributions[i].update(diff / (event->timestamp() - last_call_time), "");
                    }
                    proc_vec = true;
                }
                if (e->probe_id()->str() == "hosts-agg") {
                    double prev = all_hosts_energy;
                    all_hosts_energy = e->data_as_AggregatedProbeData()->data();
                    double tot = (all_hosts_energy - prev) / (event->timestamp() - last_call_time);
                    task_power_distribution.update(tot, "");
                    for (auto &[job_id, job] : running_jobs) {
                        job->power_distribution.update(tot / running_jobs.size(), "");
                    }
                    proc_agg = true;
                }
                if (proc_vec && proc_agg) {
                    last_call_time = event->timestamp();
                    proc_vec = proc_agg = false;
                }
                break;
            }
            case fb::Event_JobSubmittedEvent: {
                auto parsed_job = event->event_as_JobSubmittedEvent();
                LocalJob *job = new LocalJob{
                    parsed_job->job_id()->str(),
                    parsed_job->job()->resource_request(),
                    parsed_job->job()->walltime(),
                    IntervalSet::empty_interval_set(),
                    -1,
                    {}
                };
                job->power_distribution.initialize(100.0, 20);
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
                auto job_id = event->event_as_JobCompletedEvent()->job_id()->str();
                auto it = running_jobs.find(job_id);
                if (it != running_jobs.end()) {
                    LocalJob *job = it->second;
                    nb_available_hosts += job->nb_hosts;
                    available_hosts += job->alloc;
                    job->power_distribution.print("Distribution finale pour la tâche " + job_id);
                    delete job;
                    running_jobs.erase(it);
                    need_scheduling = true;
                }
                break;
            }
            default:
                break;
        }
    }

    // --- Ordonnancement des tâches ---
    if (need_scheduling) {
        // Tri selon la puissance attendue sur les hôtes
        job_queue.sort(ascending_expected_host_power);
        printf("\nOrdre proposé d'ordonnancement (par puissance attendue) :\n");
        for (auto &job : job_queue) {
            double exp = expected_host_power_for_job(job);
            printf("  Tâche %s - Puissance attendue : %.2f, Hosts : %u, Walltime : %.2f\n",
                   job->id.c_str(), exp, job->nb_hosts, job->walltime);
        }

        // Stocker l'ordre effectif d'exécution
        std::vector<std::string> execution_order;

        // Allocation des ressources pour les tâches immédiates ou backfill
        LocalJob *priority_job = nullptr;
        uint32_t avail_for_priority = nb_available_hosts;
        float priority_start_time = -1;
        auto it_job = job_queue.begin();
        while (it_job != job_queue.end()) {
            LocalJob *job = *it_job;
            if (job->nb_hosts <= nb_available_hosts) {
                // Exécution immédiate
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
                // Si les ressources ne suffisent pas, on marque la tâche comme prioritaire et on arrête ce passage
                priority_job = *it_job;
                printf("La tâche %s ne peut être exécutée immédiatement; elle est mise en attente (prioritaire).\n", priority_job->id.c_str());
                ++it_job;
                break;
            }
        }

        // Backfill : traiter les tâches restantes si des ressources se libèrent
        while (it_job != job_queue.end() && nb_available_hosts > 0) {
            LocalJob *job = *it_job;
            float finish_time = parsed->now() + job->walltime;
            if (job->nb_hosts <= nb_available_hosts) {
                running_jobs[job->id] = job;
                job->maximum_finish_time = finish_time;
                job->alloc = available_hosts.left(job->nb_hosts);
                mb->add_execute_job(job->id, job->alloc.to_string_hyphen());
                available_hosts -= job->alloc;
                nb_available_hosts -= job->nb_hosts;
                execution_order.push_back(job->id);
                printf("La tâche %s sera backfillée (fin estimée : %.2f).\n", job->id.c_str(), finish_time);
                it_job = job_queue.erase(it_job);
            } else {
                ++it_job;
            }
        }

        // Affichage final de l'ordre effectif d'exécution
        printf("\nOrdre effectif d'exécution des tâches :\n");
        for (size_t i = 0; i < execution_order.size(); ++i) {
            printf("  %zu. La tâche %s va s'exécuter.\n", i + 1, execution_order[i].c_str());
>>>>>>> Stashed changes
        }
    }
}

    if (probes_running && job_queue.empty() && running_jobs.empty()) {
        mb->add_stop_probe("hosts-vec");
        mb->add_stop_probe("hosts-agg");
        probes_running = false;
    }

<<<<<<< Updated upstream
    // Scheduling logic remains unchanged

    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);

    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
        per_host_distributions[i].print("Host " + std::to_string(i) + " Power Distribution");
    }
    task_power_distribution.print("Task Power Distribution");
=======
    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);

    // Affichage succinct de la consommation moyenne par hôte
    printf("\nConsommation moyenne par hôte :\n");
    for (uint32_t i = 0; i < platform_nb_hosts; ++i) {
        double avg = per_host_distributions[i].calculate_average_power();
        printf("  Hôte %u : %.2f\n", i, avg);
    }
>>>>>>> Stashed changes

    return 0;
}

